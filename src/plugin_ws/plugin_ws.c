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
#include "ws.h"

#include "../plugin.h"
#include "../data.h"
#include "../data_geo.h"
#include "../data_sql.h"
#include "cmd.h"

#define MAX_APPSESSION (100)

typedef enum WsTypeId_t
{
    WST_NOP,             // Do nothing, unknown, not implemented, etc.
    WST_DISCONNECT,      // Client requests to disconnect (i.e. leave the browser)
    WST_PING,            // App layer keep-alive and time sync.
    WST_PONG,            // The response for the PING command.
    WST_REFRESH,         // Will forget what was the last sent, so send everything again.
    WST_HELLO,           // SESSION management, login
    WST_CHAT_MESSAGE,    // Chat subsystem received. transmit message.
    WST_UPDATE_USER_POS, // User position update (crosshair, working area)
    WST_GET_USER_POS,    // Get user position
    WST_USERS_POS,       // Get all user positions
    WST_RESOURCES,       // Get resources (list)
    WST_REGIONS,         // Get regions list
    WST_TRADE_ORDERS,    // Get trade orders
    WST_DELETE_ORDER,    // Delete order
    WST_ADD_ORDER,       // Add order
    WST_MAX_ID           // number of the predefined commands.
} WsTypeId_t;

const char *g_wstype_names[WST_MAX_ID] = {
    [WST_NOP] = "nop",
    [WST_DISCONNECT] = "disconnect",
    [WST_PING] = "ping",
    [WST_PONG] = "pong",
    [WST_REFRESH] = "refresh",
    [WST_HELLO] = "hello",
    [WST_CHAT_MESSAGE] = "chat_message",
    [WST_UPDATE_USER_POS] = "update_user_pos",
    [WST_GET_USER_POS] = "get_user_pos",
    [WST_USERS_POS] = "users_pos",
    [WST_RESOURCES] = "resources",
    [WST_REGIONS] = "regions",
    [WST_TRADE_ORDERS] = "trade_orders",
    [WST_DELETE_ORDER] = "delete_order",
    [WST_ADD_ORDER] = "add_order"
};

typedef struct
{
    ClientContext *ctx;
    struct ws_session_t *s;
    user_data_t *user;
    int alive;
} AppContext_t;

typedef struct{
    size_t session_count;
    AppContext_t sessions[MAX_APPSESSION];
    //todo : lock
} wsapp_singleton_t;

wsapp_singleton_t g_wsapp;
size_t wsapp_session_count();
AppContext_t *wsapp_session_get(size_t index);
AppContext_t *wsapp_session_create(ws_session_t *s);
void wsapp_session_destroy(ws_session_t *s);
void broadcast_chat_message(user_data_t *sender, const char *msg);

// globals
const PluginHostInterface *g_host;

time_t last_ping_sent = 0; // todo: very preliminary.

int g_is_running = 0;
int g_keep_running = 1;
int g_sleep_is_needed = 0;

const char *g_http_routes[1] = {"/ws"};
int g_http_routess_count = 1;

typedef enum{
    CMD_WS_STAT,
    CMD_WS_SOMETHING,
    CMD_WS_MAX_ID
} WsControlCmdId;

static const CommandEntry g_plugin_ws_control_cmds[] = {
 {.path="ws stat", .help="WebSocket statistics", .arg_hint=""},
 {.path="ws something", . help="No op", .arg_hint=""}
};

static const int g_plugin_ws_control_count = 2;

const char *g_ws_routes[1] = {"id_dont_know_yet"};
int g_ws_routess_count = 1;

void ws_control_test(ClientContext *ctx)
{
    (void)ctx;
    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const char *key = "dGhlIHNhbXBsZSBub25jZQ=="; // this is the key from the client request
    char keyaccept[128] = {0};
    int keyacceptlen = sizeof(keyaccept);
    ws_gen_acception_key(guid, key, keyaccept, keyacceptlen);
    g_host->debugmsg("Sec-WebSocket-Accept: %s", keyaccept);
    return;
}
void ws_conrol_list(ClientContext *ctx){
    size_t sc = wsapp_session_count();
    if (sc < 1){
        dprintf(ctx->socket_fd,
            "There were no session seen.\n");
    }else{
        for (size_t i=0; i < sc; i++){
            AppContext_t *a = wsapp_session_get(i);
            if (!a) continue; // not possible, but...
            if (a->alive){
                ws_session_t *s= a->s;
                int flen=0;
                ws_get_info(s, &flen);
                dprintf(ctx->socket_fd,
                    "WS Session ip:%s ", //frame_len:%zuk",
                    a->ctx->client_ip // , s->frame_capacity/1024
                );
                char buf[BUF_SIZE];
                ws_measure_dump_str(a->s, buf, sizeof(buf));
                dprintf(ctx->socket_fd, "%s", buf );
                if (a->user){
                    user_data_t *u= a->user;
                    dprintf(ctx->socket_fd, "user: %d/%zu (%s), pos: (%0.2f, %0.2f, %0.2f) session:%s v:%d",
                        u->id, u->index, u->nick,
                        u->lat, u->lon, u->alt,
                        u->session_key, u->version
                    );
                }
            }
        }
    }
}

/* When a control CLI command typed by user match with the provided list,
// the client request will be landed here, to provides some meaingful
// response on WebSocket protocol.
*/
void ws_control_handler(PluginContext *pc, ClientContext *ctx, char *cmd, int argc, char **argv)
{
    (void)pc;
    if (cmd)
    {
        if (0 == strcasecmp(cmd, "ws"))
        {
            if (argc >= 1)
            {
                if (0 == strcmp(argv[0], "help"))
                {
                    dprintf(ctx->socket_fd,
                            "WS HELP:\nThis is the command line interface of the plugin ws.\n"
                            " available sub-commands are:\n"
                            " help: prints this text.\n"
                            " test: execute a developement testcase.\n"
                            " list: list all alive sessions.\n"
                        );
                }
                if (0 == strcmp(argv[0], "list"))
                {
                    ws_conrol_list(ctx);
                }
                else if (0 == strcmp(argv[0], "test"))
                {
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
void ws_http_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params)
{
    (void)pc;
    (void)params;

    char buf[32*1024];
    int o = 0;

    o += snprintf(buf + o, sizeof(buf) - o,
        "<html><head><title>WebSocket Statistics</title></head><body>"
        "<h1>WebSocket Statistics</h1>"
        "<table border='1'><tr><th>IP</th><th>Frame Capacity (kB)</th><th>User Info</th><th>Stats</th></tr>");

    size_t sc = wsapp_session_count();
    if (sc == 0) {
        o += snprintf(buf + o, sizeof(buf) - o, "<tr><td colspan='4'>No active WebSocket sessions.</td></tr>");
    }else{
        for (size_t i = 0; i < sc; i++) {
            AppContext_t *a = wsapp_session_get(i);
            if (!a || !a->alive) continue;

            char stats[BUF_SIZE] = {0};
            ws_measure_dump_str(a->s, stats, sizeof(stats));
            int flen =0;
            ws_get_info(a->s, &flen);
            o += snprintf(buf + o, sizeof(buf) - o,
                "<tr><td>%s</td><td>%d</td><td>",
                a->ctx->client_ip, flen);

            if (a->user) {
                user_data_t *u = a->user;
                o += snprintf(buf + o, sizeof(buf) - o,
                    "ID: %d/%zu (%s), Pos: (%.2f, %.2f, %.2f), Session: %s, V: %d",
                    u->id, u->index, u->nick,
                    u->lat, u->lon, u->alt,
                    u->session_key, u->version);
            } else {
                o += snprintf(buf + o, sizeof(buf) - o, "N/A");
            }

            o += snprintf(buf + o, sizeof(buf) - o,
                "</td><td><pre>%s</pre></td></tr>", stats);
        }
    }
    o += snprintf(buf + o, sizeof(buf) - o, "</table></body></html>");

    g_host->http.send_response(ctx->socket_fd, 200, "text/html", buf);
}

void ws_send_json(AppContext_t *actx, json_object *obj)
{
    const char *json_str = json_object_to_json_string(obj);
    ws_send_text_message(actx->s, json_str);
}

void ws_send_json_user_data(AppContext_t *actx, user_data_t *user_data)
{
    if (user_data)
    {
        struct json_object *obj = json_object_new_object();
        json_object_object_add(obj, "type", json_object_new_string("user_data"));
        json_object_object_add(obj, "user_id", json_object_new_int(user_data->id));
        json_object_object_add(obj, "lat", json_object_new_double(user_data->lat));
        json_object_object_add(obj, "lon", json_object_new_double(user_data->lon));
        json_object_object_add(obj, "alt", json_object_new_double(user_data->alt));
        json_object_object_add(obj, "version", json_object_new_int(user_data->version));
        ws_send_json(actx, obj);
        json_object_put(obj);
    }
}
void ws_send_json_pong(AppContext_t *actx)
{
    struct json_object *obj = json_object_new_object();
    json_object_object_add(obj, "type", json_object_new_string("pong"));
    ws_send_json(actx, obj);
    json_object_put(obj);
}
void wsapp_send_json_error(AppContext_t* actx, const char* message){
    struct json_object *obj = json_object_new_object();
    json_object_object_add(obj, "error", json_object_new_string(message));
    ws_send_json(actx, obj);
    json_object_put(obj);
}

void ws_parse_sql_user(char *row, user_data_t *nuser)
{
    char *f = strtok(row, "|");
    nuser->id = atoi(f);
    f = strtok(NULL, "|");
    strcpy(nuser->nick, f);
    f = strtok(NULL, "|");
    nuser->lat = atof(f);
    f = strtok(NULL, "|");
    nuser->lon = atof(f);
    f = strtok(NULL, "|");
    nuser->alt = atof(f);
}

CommandResult_t ws_get_user_from_sql_by_session_key(const char *session_key, user_data_t *puser)
{
    data_handle_t *sqlh = data_get_handle_by_name("sql");
    if (sqlh == NULL)
    {
        errormsg("No sql data handle found");
        return CR_ERROR;
    }
    data_api_sql_t *sqlapi = (data_api_sql_t *)sqlh->specific_api;
    if (sqlapi == NULL)
    {
        errormsg("No sql data handle found");
    }
    DbQuery q;
    snprintf(q.query, sizeof(q.query), "select id, nick, lat, lon, alt from users where session_id = '%s' LIMIT 2", session_key);
    int rrc = sqlapi->execute(sqlh, &q);
    if (rrc < 0)
    {
        errormsg("Query internal error");
        return CR_ERROR;
    }
    else if (rrc == 0)
    {
        errormsg("No user found for session key %s", session_key);
        return CR_ERROR;
    }
    else if (rrc > 1)
    {
        debugmsg("More than one user found for session key %s", session_key);
    }
    char *row = q.rows[0];
    ws_parse_sql_user(row, puser);
    logmsg("User %s logged in with session key %s. User id %d, lat %f, lon %f, alt %f",
           puser->nick, session_key,
           puser->id,
           puser->lat, puser->lon, puser->alt);
    return CR_PROCESSED;
}
CommandResult_t ws_get_user_from_sql_by_user_id(int user_id, user_data_t *puser)
{
    data_handle_t *sqlh = data_get_handle_by_name("sql");
    if (sqlh == NULL)
    {
        errormsg("No sql data handle found");
        return CR_ERROR;
    }
    data_api_sql_t *sqlapi = (data_api_sql_t *)sqlh->specific_api;
    if (sqlapi == NULL)
    {
        errormsg("No sql data handle found");
    }
    DbQuery q;
    snprintf(q.query, sizeof(q.query), "select id, nick, lat, lon, alt from users where id = %d LIMIT 1", user_id);
    int rrc = sqlapi->execute(sqlh, &q);
    if (rrc < 0)
    {
        errormsg("Query internal error");
        return CR_ERROR;
    }
    else if (rrc == 0)
    {
        errormsg("No user found for session key %s", user_id);
        return CR_ERROR;
    }
    char *row = q.rows[0];

    ws_parse_sql_user(row, puser);
    logmsg("User %s logged in with session key %s. User id %d, lat %f, lon %f, alt %f",
           puser->nick, puser->session_key,
           puser->id,
           puser->lat, puser->lon, puser->alt);
    return CR_PROCESSED;
}

size_t wsapp_session_count(){
    return g_wsapp.session_count;
}
AppContext_t *wsapp_session_get(size_t index){
    return &g_wsapp.sessions[index];
}
AppContext_t *wsapp_session_find(ws_session_t *s){
    int sc= wsapp_session_count();
    if (sc < 1) return NULL;
    if (sc >= MAX_APPSESSION) return NULL;

    for (int i=0; i < MAX_APPSESSION; i++){
        AppContext_t *p = wsapp_session_get(i);
        if (p->s == s){
            return p;
        }
    }
    return NULL;
}
AppContext_t *wsapp_session_create(ws_session_t *s){
    //todo: lock
    int sc= wsapp_session_count();
    if (sc < MAX_APPSESSION){
        AppContext_t *p = &g_wsapp.sessions[sc];
        g_wsapp.session_count++;
        p->s = s;
        return  p;
    }else{
        for (int i=0; i<MAX_APPSESSION; i++){
            AppContext_t *x= &g_wsapp.sessions[i];
            if ((x->ctx->socket_fd < 0) || (x->alive == 0)) // todo: where is the connection status?
            { 
                //reusable...
                x->s = s;
                return x;
            }
        }
    }
    return NULL;
}
void wsapp_session_destroy(ws_session_t *s) {
    AppContext_t *a= wsapp_session_find(s);
    if (a){
        a->alive = 0;
    }
}
void broadcast_chat_message(user_data_t *sender, const char *msg) {
    json_object *chat_packet = json_object_new_object();
    json_object_object_add(chat_packet, "type", json_object_new_string("chat_message"));
    json_object_object_add(chat_packet, "user_id", json_object_new_int(sender->id));
    json_object_object_add(chat_packet, "nick", json_object_new_string(sender->nick));
    json_object_object_add(chat_packet, "message", json_object_new_string(msg));
    json_object_object_add(chat_packet, "timestamp", json_object_new_int(time(NULL)));

    const char *json_str = json_object_to_json_string(chat_packet);

    for (size_t i = 0; i < wsapp_session_count(); ++i) {
        AppContext_t *a = wsapp_session_get(i);
        if (a->alive){
            ws_session_t * s= a->s;
            if (s && a->user && a->user->id != sender->id) {
                // todo: some output queue would be better
                ws_send_text_message(s, json_str);
            }
        }
    }

    json_object_put(chat_packet);
}

CommandResult_t ws_json_command(AppContext_t *actx, WsTypeId_t wst, struct json_object *parsed)
{
    if (!actx) return CR_ERROR;
    ClientContext *ctx = actx->ctx;
    CommandResult_t result = CR_UNKNOWN;
    switch (wst)
    {
    case WST_REFRESH:
        // will set the known last version to 0, so all update will be sent.
        break;
    case WST_CHAT_MESSAGE:{
            struct json_object *msg_obj;
            if (!json_object_object_get_ex(parsed, "message", &msg_obj)) {
                wsapp_send_json_error(actx, "Missing chat message text");
                return CR_ERROR;
            }
        
            const char *msg = json_object_get_string(msg_obj);
            if (!actx->user) {
                wsapp_send_json_error(actx, "User not identified");
                return CR_ERROR;
            }
        
            // Opció: ide kerülhet audit log is a DB-be
            broadcast_chat_message(actx->user, msg);
            return CR_PROCESSED;
        }
        break;
    case WST_UPDATE_USER_POS:
        // todo:session and user management needed.
        break;
    case WST_HELLO:
    {
        data_handle_t *dh = data_get_handle_by_name("geo");
        if (dh == NULL)
        {
            errormsg("No geo data handle found");
            return CR_ERROR;
        }
        data_api_geo_t *geoapi = (data_api_geo_t *)dh->specific_api;
        const char *session_key = ctx->request.session_id;
        if (strlen(session_key) == 0)
        {
            debugmsg("There was no session in the header. Get from ws.");
            struct json_object *session_id_obj;
            if (json_object_object_get_ex(parsed, "session_id", &session_id_obj))
            {
                session_key = json_object_get_string(session_id_obj);
            }
        }
        if (!session_key)
        {
            errormsg("There was no session_key.");
            return CR_ERROR;
        }
        if ((strlen(session_key) < 5) || (strlen(session_key) > 50))
        {
            errormsg("The session_key was wrong.");
            return CR_ERROR;
        }
        int user_id = -1;
        struct json_object *user_id_obj;
        if (json_object_object_get_ex(parsed, "user_id", &user_id_obj))
        {
            int ws_user_id = json_object_get_int(user_id_obj);
            if ((ws_user_id >= 0) && (ws_user_id <= 9999))
            {
                user_id = ws_user_id; // plausible
            }
            else
            {
                errormsg("The user_id was wrong.");
                return CR_ERROR;
            }
        }
        if (user_id < 0)
        {
            // todo: is it part of the helo protocol ?
            errormsg("The user_id was wrong in the hello protocol.");
            // return CR_ERROR; // if it is not part, the check otherwise
        }
        user_data_t *user = geoapi->find_user_by_session(dh, session_key);
        if (user == NULL)
        {
            user_data_t nuser;
            strncpy(nuser.session_key, session_key, sizeof(nuser.session_key));
            // there was no session yet seen here, but user_id is needed.
            if (user_id >= 0)
            {
                user = geoapi->find_user_by_user_id(dh, user_id);
                if (user)
                {
                    // the user was already here, but different session key (?)
                    geoapi->set_user(dh, user);
                }
                else
                {
                    ws_get_user_from_sql_by_user_id(user_id, &nuser);
                    geoapi->add_user(dh, &nuser);
                }
            }
            else
            {
                ws_get_user_from_sql_by_session_key(session_key, &nuser);
                geoapi->add_user(dh, &nuser);
            }
        }
        if (user)
        {
            user_id = user->id;
            ws_send_json_user_data(actx, user);
            // this user pointer's lifetime depends on data_geo implementation,
            // actually it is longer than the session. Later id shall be stored.
            actx->user = user; 
            result = CR_PROCESSED;
        }
        else
        {
            errormsg("There was no user for the session_key.");
            return CR_ERROR;
        }
    }
    break;
    case WST_DISCONNECT:
        g_host->debugmsg("Client requested quit");
        result = CR_QUIT;
        break;
    case WST_PING:
    {
        g_host->debugmsg("Application-level ping received, sending pong");
        ws_send_json_pong(actx);
        result = CR_PROCESSED;
    }
    break;
    case WST_PONG:
        g_host->debugmsg("Application-level pong received");
        result = CR_PROCESSED;
        break;

    case WST_NOP:
    default:
        break;
    }
    return result;
}

/** plugin_ws_OnTextFrame
 * Process the recieved text packets as json
 */
CommandResult_t plugin_ws_OnTextFrame(struct ws_session_t *s, const char *txt, size_t len, void *user_data)
{
    (void)len;
    (void)user_data;
    CommandResult_t res = CR_UNKNOWN;
    // TODO: later, s shall contains the actx as well, but now lets find it!
    AppContext_t *actx = wsapp_session_find(s);
    if (!actx){
        actx= (AppContext_t*)user_data;
    }
    struct json_object *parsed = json_tokener_parse((const char *)txt);
    if (!parsed)
    {
        g_host->debugmsg("Invalid JSON received");
        res = CR_ERROR;
        return res;
    }

    struct json_object *type_obj;
    if (json_object_object_get_ex(parsed, "type", &type_obj))
    {
        const char *type = json_object_get_string(type_obj);
        g_host->debugmsg("Received message type: %s", type);

        // todo: replace this with hashmap implementation.
        for (int i = 0; i < WST_MAX_ID; i++)
        {
            WsTypeId_t wst = (WsTypeId_t)i;
            const char *name = g_wstype_names[i];
            if (strcasecmp(type, name) == 0)
            {

                res = ws_json_command(actx, wst, parsed);
                if (res != CR_UNKNOWN)
                {
                    break;
                }
            }
        }
    }
    json_object_put(parsed); // cleanup
    return res;              // or maybe handled, bot nothing more to knonw here...
}
CommandResult_t plugin_ws_OnBinaryFrame(ClientContext *ctx, const unsigned char *buf, size_t len, void *user_data)
{
    (void)ctx;
    (void)buf;
    (void)len;
    (void)user_data;
    return CR_PROCESSED;
}

void ws_ws_handler(PluginContext *pc, ClientContext *ctx, WsRequestParams *wsparams)
{
    (void)pc;
    (void)wsparams; // collects all inputs

    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const char *key = NULL;
    for (int i = 0; i < ctx->request.header_count; i++)
    {
        if (strcasecmp(ctx->request.headers[i].key, "Sec-WebSocket-Key") == 0)
        {
            key = ctx->request.headers[i].value;
            break;
        }
    }
    if (!key)
    {
        g_host->debugmsg("Sec-WebSocket-Key not found");
        g_host->http.send_response(ctx->socket_fd, 400, "text/plain", "Missing Sec-WebSocket-Key");
        return;
    }

    g_host->debugmsg("Sec-WebSocket-Key: %s", key);
    /* step1, process http headers and get "Sec-WebSocket-Key" keyed value.
     *  so, we need to add http proto headers to the WS as well, I guess..
     */
    // Optionally test RFC 6455 key generation:
    char keyaccept[128] = {0};
    int keyacceptlen = sizeof(keyaccept);
    ws_gen_acception_key(guid, key, keyaccept, keyacceptlen);
    char resp[BUF_SIZE];
    int rml = sizeof(resp);
    int o = 0;
    const char *fmt =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n";
    o += snprintf(resp + o, rml - o, fmt, keyaccept);

    // send response header
    dprintf(ctx->socket_fd, "%s", resp);
    
    /** step2, change from HTTP to WS protocol.
     * 
     */

    //session and application context management
    // the Application layer's callback to WS layer
    static const WsProcessorApi_t g_WsCallbacks = {
        .onWsBinaryFrame = plugin_ws_OnBinaryFrame,
        .onWsTextFrame = plugin_ws_OnTextFrame};

    ws_session_t *s= ws_session_create(ctx, &g_WsCallbacks, NULL );
    AppContext_t *actx= wsapp_session_create(s);
    ws_set_user_data(s, (void*)actx);
    ws_handle_ws_loop(s);
    wsapp_session_destroy(s);
    ws_session_destroy(s);
}

int plugin_register(PluginContext *pc, const PluginHostInterface *host)
{
    (void)pc;
    g_host = host;
    // g_host->server.register_control_route(pc, g_control_routess_count, g_control_routes); //old
    g_host->server.register_commands(pc, g_plugin_ws_control_cmds, g_plugin_ws_control_count);
    g_host->server.register_http_route(pc, g_http_routess_count, g_http_routes);
    return PLUGIN_SUCCESS;
}

int plugin_thread_init(PluginContext *ctx)
{
    (void)ctx;
    // this will be run for each new connection
    return 0;
}

int plugin_thread_finish(PluginContext *ctx)
{
    (void)ctx;
    // this will be run for each connection, when finished.
    return 0;
}
static int plugin_ws_execute_command(PluginContext *pc, ClientContext *ctx, CommandEntry *pe, char* cmd){
    (void)pc;
    (void)ctx;
    (void)cmd;
    int ret=-1;
    if (pe){
        switch (pe->handlerid){
            case CMD_WS_SOMETHING: ret = 0; break;
            case CMD_WS_STAT: ret=0; break;
        }
    }
    return ret;
}
int plugin_init(PluginContext *pc, const PluginHostInterface *host)
{
    (void)pc;
    g_host = host;
    pc->control.request_handler = ws_control_handler;
    pc->control.execute_command = plugin_ws_execute_command;

    pc->http.request_handler = ws_http_handler;
    pc->ws.request_handler = ws_ws_handler;
    g_sleep_is_needed = 0;
    g_keep_running = 1;
    g_is_running = 0; // incremented by the handler, if needed.
    pc->tried_to_shutdown = 0;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext *pc)
{
    (void)pc;
    // Will runs once, when plugin unloaded.
    pc->http.request_handler = NULL;
}
// Plugin event handler implementation
int plugin_event(PluginContext *pc, PluginEventType event, const PluginEventContext *ctx)
{
    (void)pc;
    (void)ctx;
    switch (event)
    {
    case PLUGIN_EVENT_SLEEP:
    case PLUGIN_EVENT_STANDBY:
        g_sleep_is_needed = 1;
        return (g_is_running > 0) ? 1 : 0; // tries to keep running
        break;
    case PLUGIN_EVENT_TERMINATE:
        g_keep_running = 0;
        return (g_is_running > 0) ? 1 : 0; // tries to keep running
        break;
    }
    return 0;
}
