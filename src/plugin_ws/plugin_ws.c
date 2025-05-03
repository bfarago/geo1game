/**
 * WebSocket plugin implementation
 * available capabilities:
 *  sha1, base64, key-accept, json parser
 * todo / missing requirements:
 *  - session and logged in user handling
 *  - some data representation with timestamp and version nr
 *  - more statistics
 *  - database connection , somehow.
 *  - and/or other plugins can add or trigger this one, to send something (queue)
 */

#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
 
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#include <json-c/json.h>

#include "../plugin.h"
#include "../data.h"
#include "../data_geo.h"

typedef enum {
    CR_UNKNOWN,
    CR_QUIT,
    CR_PROCESSED,
    CR_ERROR
} CommandResult_t;

typedef enum {
    CMD_NOP,            // Do nothing, unknown, not implemented, etc.
    CMD_DISCONNECT,     // Client requests to disconnect (i.e. leave the browser)
    CMD_PING,           // App layer keep-alive and time sync.
    CMD_PONG,           // The response for the PING command.
    CMD_REFRESH,        // Will forget what was the last sent, so send everything again.
    CMD_HELLO,          // SESSION management, login
    CMD_CHAT_MESSAGE,   // Chat subsystem received. transmit message.
    CMD_UPDATE_USER_POS,// User position update (crosshair, working area)
    CMD_GET_USER_POS,   // Get user position
    CMD_USERS_POS,      // Get all user positions
    CMD_RESOURCES,      // Get resources (list)
    CMD_REGIONS,        // Get regions list
    CMD_TRADE_ORDERS,   // Get trade orders
    CMD_DELETE_ORDER,   // Delete order
    CMD_ADD_ORDER,      // Add order
    CMD_MAX_ID          // number of the predefined commands.
} CommandId_t;

const char* g_command_names[CMD_MAX_ID] = {
    [CMD_NOP]=              "nop",
    [CMD_DISCONNECT]=       "disconnect",
    [CMD_PING]=             "ping",
    [CMD_PONG]=             "pong",
    [CMD_REFRESH]=          "refresh",
    [CMD_HELLO]=            "hello",
    [CMD_CHAT_MESSAGE]=     "chat_message",
    [CMD_UPDATE_USER_POS]=  "update_user_pos",
    [CMD_GET_USER_POS]=     "get_user_pos",
    [CMD_USERS_POS]=        "users_pos",
    [CMD_RESOURCES]=        "resources",
    [CMD_REGIONS]=          "regions",
    [CMD_TRADE_ORDERS]=     "trade_orders",
    [CMD_DELETE_ORDER]=     "delete_order",
    [CMD_ADD_ORDER]=        "add_order"
};

// globals
const PluginHostInterface *g_host;

static time_t last_ping_sent = 0; // todo: very preliminary.

int g_is_running = 0;
int g_keep_running = 1;
int g_sleep_is_needed = 0;

const char* g_http_routes[1]={"status.json"};
int g_http_routess_count = 1;
const char* g_control_routes[1]={"ws"}; // like WS HELP enter.
int g_control_routess_count = 1;
const char* g_ws_routes[1]={"id_dont_know_yet"};
int g_ws_routess_count = 1;

void gen_acception_key(const char* guid, const char* input_key, char* out_buf, size_t out_buf_len)
{
    g_host->debugmsg("Sec-WebSocket-Key: %s", input_key);
    // RFC 6455: Sec-WebSocket-Accept = base64( SHA1( key + GUID ) )
    char concat[256];
    snprintf(concat, sizeof(concat), "%s%s", input_key, guid);

    unsigned char hash[SHA_DIGEST_LENGTH]; // 20 bytes
    SHA1((unsigned char*)concat, strlen(concat), hash);

    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, hash, SHA_DIGEST_LENGTH);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    snprintf(out_buf, out_buf_len, "%.*s", (int)bufferPtr->length, bufferPtr->data);
    BIO_free_all(bio);

    g_host->debugmsg("Sec-WebSocket-Accept: %s", out_buf);
}

void ws_control_test(ClientContext *ctx) {
    (void)ctx;
    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const char *key = "dGhlIHNhbXBsZSBub25jZQ=="; // this is the key from the client request
    char keyaccept[128] = {0};
    int keyacceptlen = sizeof(keyaccept);
    gen_acception_key(guid, key, keyaccept, keyacceptlen);
    g_host->debugmsg("Sec-WebSocket-Accept: %s", keyaccept);
    return;
}

/* When a control CLI command typed by user match with the provided list,
// the client request will be landed here, to provides some meaingful
// response on WebSocket protocol.
*/
void ws_control_handler(PluginContext *pc, ClientContext *ctx, char* cmd, int argc, char **argv){
    (void)pc;
    if (cmd){
        if (0 == strcasecmp(cmd, "ws")){
            if (argc >= 1){
                if (0 == strcmp(argv[0], "help")){
                    dprintf(ctx->socket_fd, 
                        "WS HELP:\nThis is the command line interface of the plugin ws.\n"
                        " available sub-commands are:\n"
                        " help: prints this text.\n"
                        " test: execute a developement testcase.\n"
                    );
                }else if (0 == strcmp(argv[0], "test")){
                    ws_control_test(ctx);
                }
            }
        }
    }
    
    return;
}

/* When a http route match with the provided list, a client request
// will land here, to provides some meaingful response on http protocol.
*/
void ws_http_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    (void)params;
    g_host->http.send_response(ctx->socket_fd, 404, "text/plain", "This path is not yet impkemented.\n");
}

void ws_send_text_message(ClientContext *ctx, const char *msg) {
    char frame[1024];
    size_t len = strlen(msg);
    frame[0] = 0x81; // FIN=1, opcode=1 (text)
    if (len <= 125) {
        frame[1] = (unsigned char)len;
        memcpy(&frame[2], msg, len);
        write(ctx->socket_fd, frame, 2 + len);
    } else if (len <= 65535) {
        frame[1] = 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        memcpy(&frame[4], msg, len);
        write(ctx->socket_fd, frame, 4 + len);
    } else {
        frame[1] = 127;
        frame[2] = 0; frame[3] = 0; frame[4] = 0; frame[5] = 0;
        frame[6] = (len >> 24) & 0xFF;
        frame[7] = (len >> 16) & 0xFF;
        frame[8] = (len >> 8) & 0xFF;
        frame[9] = len & 0xFF;
        memcpy(&frame[10], msg, len);
        write(ctx->socket_fd, frame, 10 + len);
    }
}
void ws_send_json(ClientContext *ctx, json_object *obj) {
    const char *json_str = json_object_to_json_string(&obj, &json_str);
    ws_send_text_message(ctx, json_str);
    free(json_str);
}

void ws_send_user_data(ClientContext *ctx, user_data_t* user_data){
    if (user_data){
        struct json_object *obj = json_object_new_object();
        json_object_object_add(obj, "type", json_object_new_string("user_data"));
        json_object_object_add(obj, "user_id", json_object_new_int(user_data->id));
        json_object_object_add(obj, "lat", json_object_new_double(user_data.lat));
        json_object_object_add(obj, "lon", json_object_new_double(user_data.lon));
        json_object_object_add(obj, "alt", json_object_new_double(user_data->alt));
        json_object_object_add(obj, "version", json_object_new_int(user_data->version));
        ws_send_json(ctx, obj);
        json_object_put(obj);
    }
}
void ws_send_pong(ClientContext *ctx ) {
    struct json_object *obj = json_object_new_object();
    json_object_object_add(obj, "type", json_object_new_string("pong"));
    ws_send_json(ctx, obj);
    json_object_put(obj);
}
void ws_parse_sql_user(char* row, user_data_t* nuser){
    char *f = strtok(row, "|");
    nuser.user_id = atoi(f);
    f = strtok(NULL, "|");
    strcpy(nuser.nick, f);
    f = strtok(NULL, "|");
    nuser.lat = atof(f);
    f = strtok(NULL, "|");
    nuser.lon = atof(f);
    f = strtok(NULL, "|");
    nuser.alt = atof(f);
}
CtxResultStatus get_user_from_sql_by_session_key(const char *session_key, user_data_t **puser)
{
    data_handle_t *sqlh= data_get_handle_by_name("sql");
    if (sqlh == NULL) {
        errormsg("No sql data handle found");
        return CR_ERROR;
    }
    data_api_sql_t *geoapi = (data_api_sql_t *)sqlh->specific_api);
    if (sqlapi == NULL) {
        errormsg("No sql data handle found");
    }
    DbQuery q;
    snprintf(q.query, sizeof(q.query), "select id, nick, lat, lon, alt from users where session_id = '%s' LIMIT 2", session_key);
    int rrc = sqlapi->execute(sqlh, &q);
    if (rrc < 0) {
        errormsg("Query internal error");
        return CR_ERROR;
    }else if (rrc == 0) {
        errormsg("No user found for session key %s", session_key);
        return CR_ERROR;
    }else if (rr > 1) {
        debugmsg("More than one user found for session key %s", session_key);
    }
    char * row= q.rows[0];
    user_data_t nuser = {0};
    ws_parse_sql_user(row, &nuser);
    logmsg("User %s logged in with session key %s. User id %d, lat %f, lon %f, alt %f",
            nuser.nick, session_key,
            nuser.user_id,
            nuser.lat, nuser.lon, nuser.alt);

    geoapi->add_user(dh, &nuser);
    *puser = geapi->find_user_by_user_id(dh, nuser.user_id);
    if (*puser) {
        user_id = nuser.id;
    }else{
        errormsg("No user found for user_id %d", nuser.user_id);
    }
}
CtxResultStatus get_user_from_sql_by_user_id(int user_id, user_data_t **puser)
{
    data_handle_t *sqlh= data_get_handle_by_name("sql");
    if (sqlh == NULL) {
        errormsg("No sql data handle found");
        return CR_ERROR;
    }
    data_api_sql_t *geoapi = (data_api_sql_t *)sqlh->specific_api);
    if (sqlapi == NULL) {
        errormsg("No sql data handle found");
    }
    DbQuery q;
    snprintf(q.query, sizeof(q.query), "select id, nick, lat, lon, alt from users where id = %d LIMIT 1", user_id);
    int rrc = sqlapi->execute(sqlh, &q);
    if (rrc < 0) {
        errormsg("Query internal error");
        return CR_ERROR;
    }else if (rrc == 0) {
        errormsg("No user found for session key %s", user_id);
        return CR_ERROR;
    }
    char * row= q.rows[0];
    user_data_t nuser = {0};
    ws_parse_sql_user(row, &nuser);
    logmsg("User %s logged in with session key %s. User id %d, lat %f, lon %f, alt %f",
            nuser.nick, session_key,
            nuser.user_id,
            nuser.lat, nuser.lon, nuser.alt);

    geoapi->add_user(dh, &nuser);
    user = geapi->find_user_by_user_id(dh, nuser.user_id);
    if (user) {
        user_id = nuser.id;
    }else{
        errormsg("No user found for user_id %d", nuser.user_id);
        return CR_ERROR;
    }
    return CR_PROCESSED;
}
CommandResult_t ws_json_command(ClientContext *ctx, CommandId_t cmd, struct json_object *parsed) {
    CommandResult_t result = CR_UNKNOWN;
    switch (cmd) {
        case CMD_REFRESH:
            // will set the known last version to 0, so all update will be sent.
        break;
        case CMD_CHAT_MESSAGE:
            // todo: chat queue, version handling, session and user management needed.
        break;
        case CMD_UPDATE_USER_POS:
            // todo:session and user management needed.
        break;
        case CMD_HELLO:{
            data_handle_t *dh= data_get_handle_by_name("geo");
            if (dh == NULL) {
                errormsg("No geo data handle found");
                return CR_ERROR;
            }
            data_api_geo_t *geoapi = (data_api_geo_t *)dh->specific_api);
            const char *session_key = &ctx->request->session_id;
            if (strlen(session_key) == 0) {
                debugmsg("There was no session in the header. Get from ws.");
                struct json_object *session_id_obj;
                if (json_object_object_get_ex(parsed, "session_id", &session_id_obj)) {
                    session_key= json_object_get_string(session_id_obj);
                }
            }
            if (!session_key) {
                errormsg("There was no session_key.");
                return CR_ERROR;
            }
            if ((strlen(session_key) < 5) || (strlen(session_key) > 50  ) {
                errormsg("The session_key was wrong.");
                return CR_ERROR;
            }
            int user_id = -1;
            struct json_object *user_id_obj;
            if (json_object_object_get_ex(parsed, "user_id", &user_id_obj)) {
                int ws_user_id= json_object_get_int(user_id_obj);
                if ((ws_user_id >= 0) && {ws_user_id <= 9999}) {
                    user_id= ws_user_id; // plausible
                } else {
                    errormsg("The user_id was wrong.");
                    return CR_ERROR;
                }
            }
            if (user_id < 0) {
                // todo: is it part of the helo protocol ?
                errormsg("The user_id was wrong in the hello protocol.");
                // return CR_ERROR; // if it is not part, the check otherwise
            }
            user_data_t* user = geoapi->find_user_by_session_key(dh, session_key);
            if (user == NULL) {
                // there was no session yet seen here, but user_id is needed.
                if (user_id >= 0){
                    user= geoapi->find_user_by_user_id(dh, user_id);
                    if (user){
                        //the user was already here, but different session key (?)
                        strcpy(user->session_key, session_key);
                        geoapi->set_user(dh, user);
                    } else {
                        ws_get_user_from_db_by_user_id(user_id, &user);
                    }
                } else {
                    ws_get_user_from_db_by_session_key(session_key, &user);
                }
            }
            if (user) {
                user_id = user->id;
                ws_send_user_data(ctx, user);
                result = CR_PROCESSED;
            }else{
                errormsg("There was no user for the session_key.");
                return CR_ERROR;
            }
        }
        break;
        case CMD_DISCONNECT:
            g_host->debugmsg("Client requested quit");
            result = CR_QUIT;
            break;
        case CMD_PING:
        {
            g_host->debugmsg("Application-level ping received, sending pong");
            ws_send_pong(ctx);
            result = CR_PROCESSED;
        }
        break;
        case CMD_PONG:
            g_host->debugmsg("Application-level pong received");
            result = CR_PROCESSED;
            break;
        
        case CMD_NOP:
        default:
            break;
    }
    return result;
}

CommandResult_t ws_json(ClientContext *ctx, const unsigned char *payload){
    CommandResult_t res = CR_UNKNOWN;
    // PluginContext *pc, ClientContext *ctx, WsRequestParams *wsparams
    struct json_object *parsed = json_tokener_parse((const char *)payload);
    if (!parsed) {
        g_host->debugmsg("Invalid JSON received");
        res = CR_ERROR;
        return res;
    }

    struct json_object *type_obj;
    if (json_object_object_get_ex(parsed, "type", &type_obj)) {
        const char *type = json_object_get_string(type_obj);
        g_host->debugmsg("Received message type: %s", type);
        for(int i = 0; i < CMD_MAX_ID; i++) {
            CommandId_t cmd = (CommandId_t)i;
            const char *name=g_command_names[i];
            if (strcasecmp(type, name) == 0) {
                res = ws_json_command(ctx, cmd, parsed);
                if (res != CR_UNKNOWN){
                    break;
                }
            }
        }
    }
    json_object_put(parsed); // cleanup
    return res; // or maybe handled, bot nothing more to knonw here...
}

void handle_ws_loop(PluginContext *pc, ClientContext *ctx, WsRequestParams *wsparams) {
    (void)pc;
    (void)wsparams;
    char frame[BUF_SIZE];
    g_is_running++; // todo: atomically
    while (g_keep_running) {
        ssize_t len = read(ctx->socket_fd, frame, sizeof(frame));
        if (len <= 0) {
            g_host->debugmsg("WebSocket connection closed or error");
            break;
        }

        unsigned char opcode = frame[0] & 0x0F;
        if (opcode == 0x8) {
            g_host->debugmsg("WebSocket close frame received");
            break;
        }
        if (opcode == 0xA) {
            g_host->debugmsg("Pong received from client");
            continue;
        }
        if (opcode != 0x1) {
            g_host->debugmsg("Unsupported WebSocket frame received");
            continue;
        }

        size_t payload_len = frame[1] & 0x7F;
        int mask_offset = 2;
        if (payload_len == 126) {
            payload_len = (frame[2] << 8) | frame[3];
            mask_offset = 4;
        } else if (payload_len == 127) {
            g_host->debugmsg("Payload too large to handle");
            break;
        }

        unsigned char *mask = (unsigned char*)&frame[mask_offset];
        unsigned char *payload = (unsigned char*)&frame[mask_offset + 4];
        for (size_t i = 0; i < payload_len; i++) {
            payload[i] ^= mask[i % 4];
        }

        g_host->debugmsg("WS text message: %.*s", (int)payload_len, payload);
        CommandResult_t jsres = ws_json(ctx, payload);
        if (jsres == CR_ERROR) {
            continue; // wrong json format, may be this is not even json ?
        } else if  (jsres == CR_UNKNOWN) {
            continue; // unknown
        } else if  (jsres == CR_QUIT) {
            break; // quit
        }

        char response[BUF_SIZE];
        size_t response_len = 0;
        response[0] = 0x81;
        if (payload_len <= 125) {
            response[1] = (unsigned char)payload_len;
            memcpy(&response[2], payload, payload_len);
            response_len = 2 + payload_len;
            write(ctx->socket_fd, response, response_len);
        }
        // Time measurement and periodic ping sending
        time_t now = time(NULL);
        if (now - last_ping_sent >= 10) { // send ping every 10 seconds
            char ping_msg[] = { 0x89, 0x00 }; // opcode 0x9 for ping, no payload
            write(ctx->socket_fd, ping_msg, sizeof(ping_msg));
            g_host->debugmsg("Ping sent to client");
            last_ping_sent = now;
        }
    }
    g_is_running--; // todo: atomically
}
void ws_ws_handler(PluginContext *pc, ClientContext *ctx, WsRequestParams *wsparams) {
    (void)pc;
    (void)wsparams;    // collects all inputs

    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const char *key = NULL;
    for (int i = 0; i < ctx->request.header_count; i++) {
        if (strcasecmp(ctx->request.headers[i].key, "Sec-WebSocket-Key") == 0) {
            key = ctx->request.headers[i].value;
            break;
        }
    }
    if (!key) {
        g_host->debugmsg("Sec-WebSocket-Key not found");
        g_host->http.send_response(ctx->socket_fd, 400, "text/plain", "Missing Sec-WebSocket-Key");
        return;
    }

    g_host->debugmsg("Sec-WebSocket-Key: %s", key);
    /* step1, process http headers and get "Sec-WebSocket-Key" keyed value.
     *  so, we need to add http proto headers to the WS as well, I guess..
     */
    // const char request_header_key = "Sec-WebSocket-Key";
    // const char response_header_key = "Sec-WebSocket-Accept";

    // Optionally test RFC 6455 key generation:
    
    char keyaccept[128] = {0};
    int keyacceptlen = sizeof(keyaccept);
    gen_acception_key(guid, key, keyaccept, keyacceptlen);
    char resp[BUF_SIZE];
    int rml=sizeof(resp);
    int o= 0;
    const char* fmt=
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n";
    o += snprintf(resp+o,rml-o, fmt, keyaccept);

    // send response header
    dprintf(ctx->socket_fd, "%s", resp);

    handle_ws_loop(pc, ctx, wsparams);
}
int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    (void)pc;
    g_host = host;
    g_host->server.register_control_route(pc, g_control_routess_count, g_control_routes);
    return PLUGIN_SUCCESS;
}

int plugin_thread_init(PluginContext *ctx) {
    (void)ctx;
    // this will be run for each new connection
    return 0;
}

int plugin_thread_finish(PluginContext *ctx) {
    (void)ctx;
    // this will be run for each connection, when finished.
    return 0;
}
int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    (void)pc;
    g_host = host;
    pc->control.request_handler = ws_control_handler;
    pc->http.request_handler = ws_http_handler;
    pc->ws.request_handler = ws_ws_handler;
    g_sleep_is_needed = 0;
    g_keep_running = 1;
    g_is_running = 0; // incremented by the handler, if needed.
    pc->tried_to_shutdown = 0;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    (void)pc;
    // Will runs once, when plugin unloaded.
    pc->http.request_handler = NULL;
}
// Plugin event handler implementation
int plugin_event(PluginContext *pc, PluginEventType event, const PluginEventContext* ctx) {
    (void)pc;
    (void)ctx;
    switch (event) {
        case PLUGIN_EVENT_SLEEP:
        case PLUGIN_EVENT_STANDBY:
            g_sleep_is_needed = 1;
            return (g_is_running>0) ? 1 : 0; // tries to keep running
        break;
        case PLUGIN_EVENT_TERMINATE:
            g_keep_running = 0;
            return (g_is_running>0) ? 1 : 0; // tries to keep running
        break;
    }
    return 0;
}