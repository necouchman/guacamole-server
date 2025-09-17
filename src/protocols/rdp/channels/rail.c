/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "channels/rail.h"
#include "gdi.h"
#include "plugins/channels.h"
#include "rdp.h"
#include "settings.h"

#include <freerdp/client/rail.h>
#include <freerdp/event.h>
#include <freerdp/freerdp.h>
#include <freerdp/rail.h>
#include <freerdp/window.h>
#include <guacamole/client.h>
#include <guacamole/display.h>
#include <guacamole/mem.h>
#include <guacamole/rect.h>
#include <winpr/wtypes.h>
#include <winpr/wtsapi.h>

#include <stddef.h>
#include <string.h>

guac_common_list_element* get_rail_window(guac_common_list* window_list, UINT64 window_id) {

    guac_common_list_element* item = window_list->head;
    while (item != NULL) {
        if (((guac_rdp_rail_window*)item->data)->window_id == window_id)
            return item;

        item = item->next;

    }

    return NULL;

}


/**
 * Completes initialization of the RemoteApp session, responding to the server
 * handshake, sending client status and system parameters, and executing the
 * desired RemoteApp command. This is accomplished using the Handshake PDU,
 * Client Information PDU, one or more Client System Parameters Update PDUs,
 * and the Client Execute PDU respectively. These PDUs MUST be sent for the
 * desired RemoteApp to run, and MUST NOT be sent until after a Handshake or
 * HandshakeEx PDU has been received. See:
 *
 * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-rdperp/cec4eb83-b304-43c9-8378-b5b8f5e7082a (Handshake PDU)
 * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-rdperp/743e782d-f59b-40b5-a0f3-adc74e68a2ff (Client Information PDU)
 * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-rdperp/60344497-883f-4711-8b9a-828d1c580195 (System Parameters Update PDU)
 * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-rdperp/98a6e3c3-c2a9-42cc-ad91-0d9a6c211138 (Client Execute PDU)
 *
 * @param rail
 *     The RailClientContext structure used by FreeRDP to handle the RAIL
 *     channel for the current RDP session.
 *
 * @return
 *     CHANNEL_RC_OK (zero) if the PDUs were sent successfully, an error code
 *     (non-zero) otherwise.
 */
static UINT guac_rdp_rail_complete_handshake(RailClientContext* rail) {

    UINT status;

    guac_rdp_rail_data* rail_data = (guac_rdp_rail_data*) rail->custom;
    guac_client* client = rail_data->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;

    RAIL_CLIENT_STATUS_ORDER client_status = {
        .flags =
                TS_RAIL_CLIENTSTATUS_ALLOWLOCALMOVESIZE
              | TS_RAIL_CLIENTSTATUS_APPBAR_REMOTING_SUPPORTED
    };

    /* Send client status */
    guac_client_log(client, GUAC_LOG_TRACE, "Sending RAIL client status.");
    pthread_mutex_lock(&(rdp_client->message_lock));
    status = rail->ClientInformation(rail, &client_status);
    pthread_mutex_unlock(&(rdp_client->message_lock));

    if (status != CHANNEL_RC_OK)
        return status;

    RAIL_SYSPARAM_ORDER sysparam = {

        .dragFullWindows = FALSE,

        .highContrast = {
            .flags =
                  HCF_AVAILABLE
                | HCF_CONFIRMHOTKEY
                | HCF_HOTKEYACTIVE
                | HCF_HOTKEYAVAILABLE
                | HCF_HOTKEYSOUND
                | HCF_INDICATOR,
            .colorScheme = {
                .string = NULL,
                .length = 0
            }
        },

        .keyboardCues = FALSE,
        .keyboardPref = FALSE,
        .mouseButtonSwap = FALSE,

        .workArea = {
            .left   = 0,
            .top    = 0,
            .right  = rdp_client->settings->width,
            .bottom = rdp_client->settings->height
        },

        .params =
              SPI_MASK_SET_HIGH_CONTRAST
            | SPI_MASK_SET_KEYBOARD_CUES
            | SPI_MASK_SET_KEYBOARD_PREF
            | SPI_MASK_SET_MOUSE_BUTTON_SWAP
            | SPI_MASK_SET_WORK_AREA

    };

    /* Send client system parameters */
    guac_client_log(client, GUAC_LOG_TRACE, "Sending RAIL client system parameters.");
    pthread_mutex_lock(&(rdp_client->message_lock));
    status = rail->ClientSystemParam(rail, &sysparam);
    pthread_mutex_unlock(&(rdp_client->message_lock));

    if (status != CHANNEL_RC_OK)
        return status;

    RAIL_EXEC_ORDER exec = {
        .flags = RAIL_EXEC_FLAG_EXPAND_ARGUMENTS,
        .RemoteApplicationProgram = rdp_client->settings->remote_app,
        .RemoteApplicationWorkingDir = rdp_client->settings->remote_app_dir,
        .RemoteApplicationArguments = rdp_client->settings->remote_app_args,
    };

    /* Execute desired RemoteApp command */
    guac_client_log(client, GUAC_LOG_TRACE, "Executing remote application.");
    pthread_mutex_lock(&(rdp_client->message_lock));
    status = rail->ClientExecute(rail, &exec);
    pthread_mutex_unlock(&(rdp_client->message_lock));

    return status;

}

/**
 * A callback function that is invoked when the RDP server sends the result
 * of the Remote App (RAIL) execution command back to the client, so that the
 * client can handle any required actions associated with the result.
 * 
 * @param context
 *     A pointer to the RAIL data structure associated with the current
 *     RDP connection.
 *
 * @param execResult
 *     A data structure containing the result of the RAIL command.
 *
 * @return
 *     CHANNEL_RC_OK (zero) if the result was handled successfully, otherwise
 *     a non-zero error code. This implementation always returns
 *     CHANNEL_RC_OK.
 */
static UINT guac_rdp_rail_execute_result(RailClientContext* context,
        const RAIL_EXEC_RESULT_ORDER* execResult) {

    guac_rdp_rail_data* rail_data = (guac_rdp_rail_data*) context->custom;
    guac_client* client = rail_data->client;

    if (execResult->execResult != RAIL_EXEC_S_OK) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Failed to execute RAIL command on server: %d", execResult->execResult);
        guac_client_abort(client, GUAC_PROTOCOL_STATUS_UPSTREAM_UNAVAILABLE, "Failed to execute RAIL command.");
    }

    return CHANNEL_RC_OK;

}

/**
 * Callback which is invoked when a Handshake PDU is received from the RDP
 * server. No communication for RemoteApp may occur until the Handshake PDU
 * (or, alternatively, the HandshakeEx PDU) is received. See:
 *
 * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-rdperp/cec4eb83-b304-43c9-8378-b5b8f5e7082a
 *
 * @param rail
 *     The RailClientContext structure used by FreeRDP to handle the RAIL
 *     channel for the current RDP session.
 *
 * @param handshake
 *     The RAIL_HANDSHAKE_ORDER structure representing the Handshake PDU that
 *     was received.
 *
 * @return
 *     CHANNEL_RC_OK (zero) if the PDU was handled successfully, an error code
 *     (non-zero) otherwise.
 */
static UINT guac_rdp_rail_handshake(RailClientContext* rail,
        RAIL_CONST RAIL_HANDSHAKE_ORDER* handshake) {
    guac_rdp_rail_data* rail_data = (guac_rdp_rail_data*) rail->custom;
    guac_client* client = rail_data->client;
    guac_client_log(client, GUAC_LOG_TRACE, "RAIL handshake callback.");
    return guac_rdp_rail_complete_handshake(rail);
}

/**
 * Callback which is invoked when a HandshakeEx PDU is received from the RDP
 * server. No communication for RemoteApp may occur until the HandshakeEx PDU
 * (or, alternatively, the Handshake PDU) is received. See:
 *
 * https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-rdperp/5cec5414-27de-442e-8d4a-c8f8b41f3899
 *
 * @param rail
 *     The RailClientContext structure used by FreeRDP to handle the RAIL
 *     channel for the current RDP session.
 *
 * @param handshake_ex
 *     The RAIL_HANDSHAKE_EX_ORDER structure representing the HandshakeEx PDU
 *     that was received.
 *
 * @return
 *     CHANNEL_RC_OK (zero) if the PDU was handled successfully, an error code
 *     (non-zero) otherwise.
 */
static UINT guac_rdp_rail_handshake_ex(RailClientContext* rail,
        RAIL_CONST RAIL_HANDSHAKE_EX_ORDER* handshake_ex) {
    guac_rdp_rail_data* rail_data = (guac_rdp_rail_data*) rail->custom;
    guac_client* client = rail_data->client;
    guac_client_log(client, GUAC_LOG_TRACE, "RAIL handshake ex callback.");
    return guac_rdp_rail_complete_handshake(rail);
}

static BOOL guac_rdp_rail_window_create(rdpContext* context,
        RAIL_CONST WINDOW_ORDER_INFO* orderInfo,
        RAIL_CONST WINDOW_STATE_ORDER* windowState) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;
    guac_rdp_rail_data* rail_data = rdp_client->rail_interface->custom;

    guac_client_log(client, GUAC_LOG_TRACE, ">>> RAIL window create callback: %d", orderInfo->windowId);
    guac_client_log(client, GUAC_LOG_TRACE, ">>> RAIL client params (x, y, w, h): %d, %d, %d, %d", windowState->clientOffsetX, windowState->clientOffsetY, windowState->clientAreaWidth, windowState->clientAreaHeight);
    guac_client_log(client, GUAC_LOG_TRACE, ">>> RAIL window params (x, y, w, h): %d, %d, %d, %d", windowState->windowOffsetX, windowState->windowOffsetY, windowState->windowWidth, windowState->windowHeight);
    guac_client_log(client, GUAC_LOG_TRACE, ">>> RAIL window/client delta (x, y): %d, %d", windowState->windowClientDeltaX, windowState->windowClientDeltaY);
    guac_client_log(client, GUAC_LOG_TRACE, ">>> RAIL visible offset (x, y): %d, %d", windowState->visibleOffsetX, windowState->visibleOffsetY);


    guac_client_log(client, GUAC_LOG_TRACE, ">>> Allocating a new RAIL window.");
    guac_rdp_rail_window* new_window = guac_mem_alloc(sizeof(guac_rdp_rail_window));
    new_window->window_layer = guac_display_alloc_layer(rdp_client->display, 0);
    new_window->window_id = orderInfo->windowId;
    new_window->x = windowState->windowOffsetX;
    new_window->y = windowState->windowOffsetY;
    new_window->w = windowState->windowWidth;
    new_window->h = windowState->windowHeight;

    guac_client_log(client, GUAC_LOG_TRACE, ">>> Moving and resizing the layer for the new window.");
    guac_display_layer_move(new_window->window_layer, windowState->windowOffsetX, windowState->windowOffsetY);
    guac_display_layer_resize(new_window->window_layer, windowState->windowWidth, windowState->windowHeight);

    guac_client_log(client, GUAC_LOG_TRACE, ">>> Adding window to window list.");
    guac_common_list_lock(rail_data->rail_windows);
    guac_common_list_add(rail_data->rail_windows, new_window);
    rail_data->num_windows++;
    guac_common_list_unlock(rail_data->rail_windows);

    // guac_client_log(client, GUAC_LOG_TRACE, "Notfying render thread of modification.");
    // guac_display_render_thread_notify_modified(rdp_client->render_thread);
    // guac_display_render_thread_notify_frame(rdp_client->render_thread);

    guac_client_log(client, GUAC_LOG_TRACE, ">>> Done with RAIL window creation.");
    return TRUE;

}

/**
 * A callback function that is executed when an update for a RAIL window is
 * received from the RDP server.
 *
 * @param context
 *     A pointer to the rdpContext structure used by FreeRDP to handle the
 *     window update.
 *
 * @param orderInfo
 *     A pointer to the data structure that contains information about what
 *     window was updated what updates were performed.
 *
 * @param windowState
 *     A pointer to the data structure that contains details of the updates
 *     to the window, as indicated by flags in the orderInfo field.
 *
 * @return
 *     TRUE if the client-side processing of the updates as successful; otherwise
 *     FALSE. This implementation always returns TRUE.
 */
static BOOL guac_rdp_rail_window_update(rdpContext* context,
        RAIL_CONST WINDOW_ORDER_INFO* orderInfo,
        RAIL_CONST WINDOW_STATE_ORDER* windowState) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;
    guac_rdp_rail_data* rail_data = rdp_client->rail_interface->custom;

    guac_client_log(client, GUAC_LOG_TRACE, ">>> RAIL window update callback: %d", orderInfo->windowId);

    UINT32 fieldFlags = orderInfo->fieldFlags;

    /* If the flag for window visibilty is set, check visibility. */
    if (fieldFlags & WINDOW_ORDER_FIELD_SHOW) {
        guac_client_log(client, GUAC_LOG_TRACE, "RAIL window visibility change: %d", windowState->showState);

        /* State is either hidden or minimized - send restore command. */
        if (windowState->showState == GUAC_RDP_RAIL_WINDOW_STATE_MINIMIZED) {

            guac_client_log(client, GUAC_LOG_DEBUG, "RAIL window minimized, sending restore command.");

            RAIL_SYSCOMMAND_ORDER syscommand;
            syscommand.windowId = orderInfo->windowId;
            syscommand.command = SC_RESTORE;
            rdp_client->rail_interface->ClientSystemCommand(rdp_client->rail_interface, &syscommand);
        }
    }

    /**
     * If the Window position has changed, we need to force a refresh of the
     * window area.
     */
    if ((fieldFlags & WINDOW_ORDER_FIELD_WND_OFFSET)
            || (fieldFlags & WINDOW_ORDER_FIELD_WND_SIZE)
            || (fieldFlags & WINDOW_ORDER_FIELD_CLIENT_AREA_OFFSET)
            || (fieldFlags & WINDOW_ORDER_FIELD_CLIENT_AREA_SIZE)
            || (fieldFlags & WINDOW_ORDER_FIELD_WND_CLIENT_DELTA)
            || (fieldFlags & WINDOW_ORDER_FIELD_VIS_OFFSET)
            || (fieldFlags & WINDOW_ORDER_FIELD_VISIBILITY)) {

        
        guac_common_list_element* rail_window_element = get_rail_window(rail_data->rail_windows, orderInfo->windowId);
        if (rail_window_element == NULL) {
            guac_client_log(client, GUAC_LOG_ERROR, ">>> UPDATE: Could not retrieve the specified RAIL window: %d", orderInfo->windowId);
            return FALSE;
        }
        
        guac_client_log(client, GUAC_LOG_TRACE, ">>> UPDATE: Window position for window: %d.", orderInfo->windowId);

        guac_rdp_rail_window* rail_window = (guac_rdp_rail_window*) rail_window_element->data;
        rail_window->x = windowState->windowOffsetX;
        rail_window->y = windowState->windowOffsetY;
        rail_window->w = windowState->windowWidth;
        rail_window->h = windowState->windowHeight;

        guac_display_layer_raw_context* current_context = guac_display_layer_open_raw(rail_window->window_layer);

        guac_display_layer_move(rail_window->window_layer, windowState->windowOffsetX, windowState->windowOffsetY);
        guac_display_layer_resize(rail_window->window_layer, windowState->windowWidth, windowState->windowHeight);

        guac_display_layer_close_raw(rail_window->window_layer, current_context);

    }

    return TRUE;

}

/**
 * A callback function that is executed when a RAIL window has been closed and
 * should be removed from tracking by the RAIL plugin.
 *
 * @param context
 *     A pointer to the rdpContext structure used by FreeRDP to handle the
 *     window delete event.
 *
 * @param orderInfo
 *     A pointer to the data structure that contains information about what
 *     window was deleted.
 *
 * @return
 *     A pointer to a list of RAIL windows that are open when RAIL is in use and
 *     applications are being accessed as RemoteApps.
 */
static BOOL guac_rdp_rail_window_delete(rdpContext* context,
        RAIL_CONST WINDOW_ORDER_INFO* orderInfo) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;
    guac_rdp_rail_data* rail_data = rdp_client->rail_interface->custom;

    guac_client_log(client, GUAC_LOG_TRACE, "RAIL window delete callback: %d", orderInfo->windowId);

    /* Remove the layer associated with the window. */
    guac_common_list_element* rail_window = get_rail_window(rail_data->rail_windows, orderInfo->windowId);
    guac_display_free_layer(((guac_rdp_rail_window*) rail_window->data)->window_layer);
    
    /* Update the RAIL window list. */
    guac_common_list_lock(rail_data->rail_windows);
    guac_common_list_remove(rail_data->rail_windows, rail_window);
    guac_common_list_unlock(rail_data->rail_windows);

    return TRUE;

}

/**
 * Callback which associates handlers specific to Guacamole with the
 * RailClientContext instance allocated by FreeRDP to deal with received
 * RAIL (RemoteApp) messages.
 *
 * This function is called whenever a channel connects via the PubSub event
 * system within FreeRDP, but only has any effect if the connected channel is
 * the RAIL channel. This specific callback is registered with the PubSub
 * system of the relevant rdpContext when guac_rdp_rail_load_plugin() is
 * called.
 *
 * @param context
 *     The rdpContext associated with the active RDP session.
 *
 * @param args
 *     Event-specific arguments, mainly the name of the channel, and a
 *     reference to the associated plugin loaded for that channel by FreeRDP.
 */
static void guac_rdp_rail_channel_connected(rdpContext* context,
        ChannelConnectedEventArgs* args) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;

    /* Set up data structure for tracking RAIL-specific data */
    guac_rdp_rail_data* rail_data = guac_mem_alloc(sizeof(guac_rdp_rail_data));
    rail_data->client = client;
    rail_data->rail_windows = guac_common_list_alloc();
    rail_data->num_windows = 0;

    /* Ignore connection event if it's not for the RAIL channel */
    if (strcmp(args->name, RAIL_SVC_CHANNEL_NAME) != 0)
        return;

    /* The structure pointed to by pInterface is guaranteed to be a
     * RailClientContext if the channel is RAIL */
    RailClientContext* rail = (RailClientContext*) args->pInterface;
    rdp_client->rail_interface = rail;

    /* Init FreeRDP RAIL context, ensuring the guac_client can be accessed from
     * within any RAIL-specific callbacks */
    rail->custom = rail_data;
    rail->ServerExecuteResult = guac_rdp_rail_execute_result;
    rail->ServerHandshake = guac_rdp_rail_handshake;
    rail->ServerHandshakeEx = guac_rdp_rail_handshake_ex;
    context->update->window->WindowCreate = guac_rdp_rail_window_create;
    context->update->window->WindowUpdate = guac_rdp_rail_window_update;
    context->update->window->WindowDelete = guac_rdp_rail_window_delete;

    guac_client_log(client, GUAC_LOG_DEBUG, "RAIL (RemoteApp) channel "
            "connected.");

}

void guac_rdp_rail_load_plugin(rdpContext* context) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;

    /* Attempt to load FreeRDP support for the RAIL channel */
    if (guac_freerdp_channels_load_plugin(context, "rail", context->settings)) {
        guac_client_log(client, GUAC_LOG_WARNING,
                "Support for the RAIL channel (RemoteApp) could not be "
                "loaded. This support normally takes the form of a plugin "
                "which is built into FreeRDP. Lacking this support, "
                "RemoteApp will not work.");
        return;
    }

    /* Complete RDP side of initialization when channel is connected */
    PubSub_SubscribeChannelConnected(context->pubSub,
            (pChannelConnectedEventHandler) guac_rdp_rail_channel_connected);

    guac_client_log(client, GUAC_LOG_DEBUG, "Support for RAIL (RemoteApp) "
            "registered. Awaiting channel connection.");

}
