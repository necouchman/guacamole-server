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

#include <stdlib.h>
#include <string.h>

/**
 * All tn5250 connection settings which may be updated by unprivileged users
 * through "argv" streams.
 */
typedef enum guac_tn5250_argv_setting {

    /**
     * The color scheme of the terminal.
     */
    GUAC_TN5250_ARGV_SETTING_COLOR_SCHEME,

    /**
     * The name of the font family used by the terminal.
     */
    GUAC_TN5250_ARGV_SETTING_FONT_NAME,

    /**
     * The size of the font used by the terminal, in points.
     */
    GUAC_TN5250_ARGV_SETTING_FONT_SIZE

} guac_tn5250_argv_setting;

/**
 * The value or current status of a connection parameter received over an
 * "argv" stream.
 */
typedef struct guac_tn5250_argv {

    /**
     * The specific setting being updated.
     */
    guac_tn5250_argv_setting setting;

    /**
     * Buffer space for containing the received argument value.
     */
    char buffer[GUAC_TN5250_ARGV_MAX_LENGTH];

    /**
     * The number of bytes received so far.
     */
    int length;

} guac_tn5250_argv;

/**
 * Handler for "blob" instructions which appends the data from received blobs
 * to the end of the in-progress argument value buffer.
 *
 * @see guac_user_blob_handler
 */
static int guac_tn5250_argv_blob_handler(guac_user* user,
        guac_stream* stream, void* data, int length) {

    guac_tn5250_argv* argv = (guac_tn5250_argv*) stream->data;

    /* Calculate buffer size remaining, including space for null terminator,
     * adjusting received length accordingly */
    int remaining = sizeof(argv->buffer) - argv->length - 1;
    if (length > remaining)
        length = remaining;

    /* Append received data to end of buffer */
    memcpy(argv->buffer + argv->length, data, length);
    argv->length += length;

    return 0;

}

/**
 * Handler for "end" instructions which applies the changes specified by the
 * argument value buffer associated with the stream.
 *
 * @see guac_user_end_handler
 */
static int guac_tn5250_argv_end_handler(guac_user* user,
        guac_stream* stream) {

    int size;

    guac_client* client = user->client;
    guac_tn5250_client* tn5250_client = (guac_tn5250_client*) client->data;
    guac_terminal* terminal = tn5250_client->term;

    /* Append null terminator to value */
    guac_tn5250_argv* argv = (guac_tn5250_argv*) stream->data;
    argv->buffer[argv->length] = '\0';

    /* Apply changes to chosen setting */
    switch (argv->setting) {

        /* Update color scheme */
        case GUAC_TN5250_ARGV_SETTING_COLOR_SCHEME:
            guac_terminal_apply_color_scheme(terminal, argv->buffer);
            break;

        /* Update font name */
        case GUAC_TN5250_ARGV_SETTING_FONT_NAME:
            guac_terminal_apply_font(terminal, argv->buffer, -1, 0);
            break;

        /* Update font size */
        case GUAC_TN5250_ARGV_SETTING_FONT_SIZE:

            /* Update only if font size is sane */
            size = atoi(argv->buffer);
            if (size > 0) {
                guac_terminal_apply_font(terminal, NULL, size,
                        tn5250_client->settings->resolution);
            }

            break;

    }

    /* Update terminal window size if connected */
    if (tn5250_client->tn5250 != NULL && tn5250_client->naws_enabled)
        guac_tn5250_send_naws(tn5250_client->tn5250, terminal->term_width,
                terminal->term_height);

    free(argv);
    return 0;

}

int guac_tn5250_argv_handler(guac_user* user, guac_stream* stream,
        char* mimetype, char* name) {

    guac_tn5250_argv_setting setting;

    /* Allow users to update the color scheme and font details */
    if (strcmp(name, "color-scheme") == 0)
        setting = GUAC_TN5250_ARGV_SETTING_COLOR_SCHEME;
    else if (strcmp(name, "font-name") == 0)
        setting = GUAC_TN5250_ARGV_SETTING_FONT_NAME;
    else if (strcmp(name, "font-size") == 0)
        setting = GUAC_TN5250_ARGV_SETTING_FONT_SIZE;

    /* No other connection parameters may be updated */
    else {
        guac_protocol_send_ack(user->socket, stream, "Not allowed.",
                GUAC_PROTOCOL_STATUS_CLIENT_FORBIDDEN);
        guac_socket_flush(user->socket);
        return 0;
    }

    guac_tn5250_argv* argv = malloc(sizeof(guac_tn5250_argv));
    argv->setting = setting;
    argv->length = 0;

    /* Prepare stream to receive argument value */
    stream->blob_handler = guac_tn5250_argv_blob_handler;
    stream->end_handler = guac_tn5250_argv_end_handler;
    stream->data = argv;

    /* Signal stream is ready */
    guac_protocol_send_ack(user->socket, stream, "Ready for updated "
            "parameter.", GUAC_PROTOCOL_STATUS_SUCCESS);
    guac_socket_flush(user->socket);
    return 0;

}

