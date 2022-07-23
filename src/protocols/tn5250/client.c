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
#include "client.h"
#include "settings.h"
#include "tn5250.h"
#include "user.h"

#include <langinfo.h>
#include <locale.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <guacamole/argv.h>
#include <guacamole/client.h>
#include <guacamole/recording.h>

int guac_client_init(guac_client* client) {

    guac_client_log(client, GUAC_LOG_INFO, "%s: Starting initialization of TN5250 client.", __func__);

    /* Set client args */
    client->args = GUAC_TN5250_CLIENT_ARGS;

    /* Allocate client instance data */
    guac_tn5250_client* tn5250_client = calloc(1, sizeof(guac_tn5250_client));
    client->data = tn5250_client;

    /* Init tn5250 client */
    tn5250_client->socket_fd = -1;
    tn5250_client->echo_enabled = 1;

    /* Set handlers */
    client->join_handler = guac_tn5250_user_join_handler;
    client->free_handler = guac_tn5250_client_free_handler;
    client->leave_handler = guac_tn5250_user_leave_handler;

    /* Register handlers for argument values that may be sent after the handshake */
    guac_argv_register(GUAC_TN5250_ARGV_COLOR_SCHEME, guac_tn5250_argv_callback, NULL, GUAC_ARGV_OPTION_ECHO);
    guac_argv_register(GUAC_TN5250_ARGV_FONT_NAME, guac_tn5250_argv_callback, NULL, GUAC_ARGV_OPTION_ECHO);
    guac_argv_register(GUAC_TN5250_ARGV_FONT_SIZE, guac_tn5250_argv_callback, NULL, GUAC_ARGV_OPTION_ECHO);

    /* Set locale and warn if not UTF-8 */
    setlocale(LC_CTYPE, "");
    if (strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
        guac_client_log(client, GUAC_LOG_INFO,
                "Current locale does not use UTF-8. Some characters may "
                "not render correctly.");
    }

    /* Success */
    return 0;

}

int guac_tn5250_client_free_handler(guac_client* client) {

    guac_tn5250_client* tn5250_client = (guac_tn5250_client*) client->data;

    /* Close tn5250 connection */
    if (tn5250_client->socket_fd != -1)
        close(tn5250_client->socket_fd);

    /* Clean up recording, if in progress */
    if (tn5250_client->recording != NULL)
        guac_recording_free(tn5250_client->recording);

    /* Kill terminal */
    guac_terminal_free(tn5250_client->term);

    /* Wait for and free telnet session, if connected */
    if (tn5250_client->tn5250 != NULL) {
        pthread_join(tn5250_client->client_thread, NULL);
        telnet_free(tn5250_client->tn5250);
    }

    /* Free settings */
    if (tn5250_client->settings != NULL)
        guac_tn5250_settings_free(tn5250_client->settings);

    free(tn5250_client);
    return 0;

}

