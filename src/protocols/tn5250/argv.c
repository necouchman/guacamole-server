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

#include "config.h"
#include "argv.h"
#include "tn5250.h"
#include "terminal/terminal.h"

#include <guacamole/protocol.h>
#include <guacamole/socket.h>
#include <guacamole/user.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int guac_tn5250_argv_callback(guac_user* user, const char* mimetype,
        const char* name, const char* value, void* data) {

    guac_client* client = user->client;
    guac_tn5250_client* tn5250_client = (guac_tn5250_client*) client->data;
    guac_terminal* terminal = tn5250_client->term;

    /* Update color scheme */
    if (strcmp(name, GUAC_TN5250_ARGV_COLOR_SCHEME) == 0)
        guac_terminal_apply_color_scheme(terminal, value);

    /* Update font name */
    else if (strcmp(name, GUAC_TN5250_ARGV_FONT_NAME) == 0)
        guac_terminal_apply_font(terminal, value, -1, 0);

    /* Update only if font size is sane */
    else if (strcmp(name, GUAC_TN5250_ARGV_FONT_SIZE) == 0) {
        int size = atoi(value);
        if (size > 0)
            guac_terminal_apply_font(terminal, NULL, size,
                    tn5250_client->settings->resolution);
    }

    return 0;

}

void* guac_tn5250_send_current_argv(guac_user* user, void* data) {

    guac_tn5250_client* tn5250_client = (guac_tn5250_client*) data;
    guac_terminal* terminal = tn5250_client->term;

    /* Send current color scheme */
    guac_user_stream_argv(user, user->socket, "text/plain",
            GUAC_TN5250_ARGV_COLOR_SCHEME,
            guac_terminal_get_color_scheme(terminal));

    /* Send current font name */
    guac_user_stream_argv(user, user->socket, "text/plain",
            GUAC_TN5250_ARGV_FONT_NAME,
            guac_terminal_get_font_name(terminal));

    /* Send current font size */
    char font_size[64];
    sprintf(font_size, "%i", guac_terminal_get_font_size(terminal));
    guac_user_stream_argv(user, user->socket, "text/plain",
            GUAC_TN5250_ARGV_FONT_SIZE, font_size);

    return NULL;

}

