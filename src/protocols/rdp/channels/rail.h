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

#ifndef GUAC_RDP_CHANNELS_RAIL_H
#define GUAC_RDP_CHANNELS_RAIL_H

#include "config.h"

#include "common/list.h"

#include <freerdp/freerdp.h>
#include <freerdp/window.h>

#include <guacamole/display.h>

#ifdef FREERDP_RAIL_CALLBACKS_REQUIRE_CONST
/**
 * FreeRDP 2.0.0-rc4 and newer requires the final arguments for RAIL
 * callbacks to be const.
 */
#define RAIL_CONST const
#else
/**
 * FreeRDP 2.0.0-rc3 and older requires the final arguments for RAIL
 * callbacks to NOT be const.
 */
#define RAIL_CONST
#endif

/**
 * The RAIL window state that indicates a hidden window.
 */
#define GUAC_RDP_RAIL_WINDOW_STATE_HIDDEN 0x00

/**
 * The RAIL window state that indicates a visible but minimized window.
 */
#define GUAC_RDP_RAIL_WINDOW_STATE_MINIMIZED 0x02

/**
 * A structure for storing data related to individual RAIL windows, keeping track
 * of the display layer on which the window is drawn and the parent window.
 */
typedef struct guac_rdp_rail_window guac_rdp_rail_window;

struct guac_rdp_rail_window {

    /**
     * A reference to the display layer that this window will be rendered on.
     */
    guac_display_layer* window_layer;

    /**
     * The FreeRDP window identifier
     */
    UINT64 window_id;

    /**
     * The x offset of the window in the available display area.
     */
    UINT32 x;

    /**
     * The y offset of the window in the available display area.
     */
    UINT32 y;

    /**
     * The last known width of the window as displayed in the available display area.
     */
    UINT32 w;

    /**
     * The last known height of the window as displayed in the available display area.
     */
    UINT32 h;

};

/**
 * A structure for storing generic RAIL data that can be referenced by the
 * FreeRDP RAIL custom data pointer. This containers pointers to the Guacamole
 * client to which this RAIL data belongs, along with the windows that are open
 * by this RAIL session.
 */
typedef struct guac_rdp_rail_data {

    /**
     * A reference to the guac_client object with which this RAIL data is
     * associated.
     */
    guac_client* client;

    /**
     * A linked list of RAIL windows.
     */
    guac_common_list* rail_windows;

    /**
     * The total number of windows open on this session.
     */
    int num_windows;

} guac_rdp_rail_data;

/**
 * Initializes RemoteApp support for RDP and handling of the RAIL channel. If
 * failures occur, messages noting the specifics of those failures will be
 * logged, and RemoteApp support will not be functional.
 *
 * This MUST be called within the PreConnect callback of the freerdp instance
 * for RAIL support to be loaded.
 *
 * @param context
 *     The rdpContext associated with the FreeRDP side of the RDP connection.
 */
void guac_rdp_rail_load_plugin(rdpContext* context);

/**
 * Traverse the window_list, looking for a window whose window_id matches the
 * provided identifier, returning a pointer to that list item or NULL if no such
 * window is in the list.
 *
 * @param window_list
 *     The list of windows to search through for the identifier.
 * 
 * @param window_id
 *     The identifier of the window to search for.
 *
 * @return
 *     A pointer to the window whose window_id matched the provided window_id,
 *     or NULL if no such window exists.
 */
guac_common_list_element* get_rail_window(guac_common_list* window_list, UINT64 window_id);

#endif

