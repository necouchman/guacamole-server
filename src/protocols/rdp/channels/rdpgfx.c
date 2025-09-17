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
#include "channels/rdpgfx.h"
#include "common/list.h"
#include "plugins/channels.h"
#include "rdp.h"
#include "settings.h"

#include <freerdp/client/rdpgfx.h>
#include <freerdp/codec/region.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/event.h>
#include <freerdp/types.h>
#include <guacamole/assert.h>
#include <guacamole/client.h>

#include <stdlib.h>
#include <string.h>

static UINT guac_rdp_rdpgfx_window_update(RdpgfxClientContext* context, gdiGfxSurface* surface) {

    /* This. Is. Awesome. */
    rdpGdi* gdi = (rdpGdi*) context->custom;
    rdpContext* rdpContext = gdi->context;
    guac_client* client = ((rdp_freerdp_context*) rdpContext)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;
    guac_rdp_rail_data* rail_data = (guac_rdp_rail_data*) rdp_client->rail_interface->custom;

    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Update the window: %d", surface->windowId);
    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Window dimensions (w, h): %d, %d", surface->width, surface->height);
    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Window mapped dimensions (w, h): %d, %d", surface->mappedWidth, surface->mappedHeight);
    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Target dimensions (w, h): %d, %d", surface->outputTargetWidth, surface->outputTargetHeight);
    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Window parameters (x, y, w, h): %d, %d, %d, %d",
                    surface->outputOriginX, surface->outputOriginY, surface->width, surface->height);


    guac_common_list_element* rail_window_element = get_rail_window(rail_data->rail_windows, surface->windowId);
    if (rail_window_element == NULL) {
        guac_client_log(client, GUAC_LOG_ERROR, ">>> RDPGFX: Could not retrieve the specified RAIL window.");
        return FALSE;
    }

    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Opening window layer context.");
    guac_rdp_rail_window* rail_window = (guac_rdp_rail_window*) rail_window_element->data;
    // guac_display_layer* default_layer = guac_display_default_layer(rdp_client->display);
    guac_display_layer_raw_context* current_context = guac_display_layer_open_raw(rail_window->window_layer);
    
    if (current_context == NULL) {
        guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Unable to get current context.");
        return CHANNEL_RC_OK;
    }

    /* Ignore paint if GDI output is suppressed */
    if (gdi->suppressOutput) {
        guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: GDI is requesting suppression of output.");
        goto paint_complete;
    }

    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Initializing output area.");
    /* Resynchronize default layer buffer details with FreeRDP's GDI */
    current_context->buffer = gdi->primary_buffer;
    current_context->stride = gdi->stride;
    guac_rect_init(&current_context->bounds, rail_window->x, rail_window->y, rail_window->w, rail_window->h);

    /* guac_rect uses signed arithmetic for all values. While FreeRDP
     * definitely performs its own checks and ensures these values cannot get
     * so large as to cause problems with signed arithmetic, it's worth
     * checking and bailing out here if an external bug breaks that. */
    GUAC_ASSERT(surface->outputTargetWidth <= INT_MAX && surface->outputTargetHeight <= INT_MAX);

    UINT32 nrects = 0;
    const RECTANGLE_16* rects = region16_rects(&surface->invalidRegion, &nrects);

    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: %d rects to update.", nrects);

    for (UINT32 x = 0; x < nrects; x++) {

        const RECTANGLE_16* current_rect = &rects[x];
        UINT32 current_width = current_rect->right - current_rect->left;
        UINT32 current_height = current_rect->bottom - current_rect->top;

        guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Processing update to rect %d (left, top, right, bottom, width, height): %d, %d, %d, %d, %d, %d",
                x, current_rect->left, current_rect->top, current_rect->right, current_rect->bottom, current_width, current_height);

        guac_rect dst_rect;
        guac_rect_init(&dst_rect, current_rect->left, current_rect->top, current_width, current_height);
        guac_rect_constrain(&dst_rect, &current_context->bounds);
        guac_rect_extend(&current_context->dirty, &dst_rect);

    }

paint_complete:

    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Painting is complete, cleaning up.");

    guac_client_log(client, GUAC_LOG_TRACE, ">>> RDPGFX: Closing window layer context.");

    guac_display_layer_close_raw(rail_window->window_layer, current_context);

    return CHANNEL_RC_OK;

}

/**
 * Callback which associates handlers specific to Guacamole with the
 * RdpgfxClientContext instance allocated by FreeRDP to deal with received
 * RDPGFX (Graphics Pipeline) messages.
 *
 * This function is called whenever a channel connects via the PubSub event
 * system within FreeRDP, but only has any effect if the connected channel is
 * the RDPGFX channel. This specific callback is registered with the
 * PubSub system of the relevant rdpContext when guac_rdp_rdpgfx_load_plugin() is
 * called.
 *
 * @param context
 *     The rdpContext associated with the active RDP session.
 *
 * @param args
 *     Event-specific arguments, mainly the name of the channel, and a
 *     reference to the associated plugin loaded for that channel by FreeRDP.
 */
static void guac_rdp_rdpgfx_channel_connected(rdpContext* context,
        ChannelConnectedEventArgs* args) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;

    /* Ignore connection event if it's not for the RDPGFX channel */
    if (strcmp(args->name, RDPGFX_DVC_CHANNEL_NAME) != 0)
        return;

    /* Init GDI-backed support for the Graphics Pipeline */
    RdpgfxClientContext* rdpgfx = (RdpgfxClientContext*) args->pInterface;
    rdpGdi* gdi = context->gdi;

    if (!gdi_graphics_pipeline_init(gdi, rdpgfx))
        guac_client_log(client, GUAC_LOG_WARNING, "Rendering backend for RDPGFX "
                "channel could not be loaded. Graphics may not render at all!");
    else
        guac_client_log(client, GUAC_LOG_DEBUG, "RDPGFX channel will be used for "
                "the RDP Graphics Pipeline Extension.");

    rdpgfx->UpdateWindowFromSurface = guac_rdp_rdpgfx_window_update;

}

/**
 * Callback which handles any RDPGFX cleanup specific to Guacamole.
 *
 * This function is called whenever a channel disconnects via the PubSub event
 * system within FreeRDP, but only has any effect if the disconnected channel
 * is the RDPGFX channel. This specific callback is registered with the PubSub
 * system of the relevant rdpContext when guac_rdp_rdpgfx_load_plugin() is
 * called.
 *
 * @param context
 *     The rdpContext associated with the active RDP session.
 *
 * @param args
 *     Event-specific arguments, mainly the name of the channel, and a
 *     reference to the associated plugin loaded for that channel by FreeRDP.
 */
static void guac_rdp_rdpgfx_channel_disconnected(rdpContext* context,
        ChannelDisconnectedEventArgs* args) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;

    /* Ignore disconnection event if it's not for the RDPGFX channel */
    if (strcmp(args->name, RDPGFX_DVC_CHANNEL_NAME) != 0)
        return;

    /* Un-init GDI-backed support for the Graphics Pipeline */
    RdpgfxClientContext* rdpgfx = (RdpgfxClientContext*) args->pInterface;
    rdpGdi* gdi = context->gdi;
    gdi_graphics_pipeline_uninit(gdi, rdpgfx);

    guac_client_log(client, GUAC_LOG_DEBUG, "RDPGFX channel support unloaded.");

}

void guac_rdp_rdpgfx_load_plugin(rdpContext* context) {

    /* Subscribe to and handle channel connected events */
    PubSub_SubscribeChannelConnected(context->pubSub,
        (pChannelConnectedEventHandler) guac_rdp_rdpgfx_channel_connected);

    /* Subscribe to and handle channel disconnected events */
    PubSub_SubscribeChannelDisconnected(context->pubSub,
        (pChannelDisconnectedEventHandler) guac_rdp_rdpgfx_channel_disconnected);

    /* Add "rdpgfx" channel */
    guac_freerdp_dynamic_channel_collection_add(context->settings, "rdpgfx", NULL);

}

