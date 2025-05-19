#include "unity.h"
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h> 

#include "../plugin.h"
#include "../http.h"

#include "../data.h"
#include "../data_geo.h"
#include "../data_sql.h"

#define WS_EXPOSE_INTERNALS
#include "ws.h"

#include "mock_ws.h"
#include "mock_data.h"
#include "plugin_ws.c"

void errormsg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    //vprintf(fmt, args);
    va_end(args);
    //printf("\n");
}
void debugmsg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    //vprintf(fmt, args);
    va_end(args);
    //printf("\n");
}
void logmsg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    //vprintf(fmt, args);
    va_end(args);
    //printf("\n");
}
PluginHostInterface g_host_fns = {
    .errormsg=errormsg,
    .debugmsg=debugmsg,
    .logmsg=logmsg
};
const PluginHostInterface *g_host = &g_host_fns;
char g_last_sent_json[1024];
CommandResult_t g_returnOnWsTextFrame = CR_PROCESSED;
int g_returnSendMessage = 0;
int g_returnExecute = 0;
DbQuery g_stubQuerySet;

CommandResult_t stubOnWsTextFrame(struct ws_session_t * s, const char *txt, size_t len, void *user_data){
    return g_returnOnWsTextFrame;
}
int stub_ws_send_text_message(struct ws_session_t *s, const char *txt,  int cmock_num_calls) {
    snprintf(g_last_sent_json, sizeof(g_last_sent_json) - 1, "%d:'%s'", cmock_num_calls, txt);
    // printf("%s\n", g_last_sent_json);
    return g_returnSendMessage;
}

void test_ws_send_user_data(){
    ClientContext ctx;
    user_data_t u;
    u.id =1;
    u.lat = 2;
    u.lon = 3;
    u.alt = 1.2;
    u.version = 4;
    ws_session_t ws;
    ws.ctx = &ctx;
    WsProcessorApi_t api={
        .onWsTextFrame = stubOnWsTextFrame
    };
    AppContext_t app;
    app.ctx = &ctx;
    app.s = &ws;
    ws.user_data = (void*)&app;
    g_returnSendMessage = 0;
    ws_send_text_message_StubWithCallback(stub_ws_send_text_message);
    ws_get_user_data_IgnoreAndReturn(0);
    ws_send_json_user_data(&app, &u);
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "0:'{"));
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "\"lat\": 2.0"));
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "\"version\": 4"));

    ws_send_text_message_StubWithCallback(stub_ws_send_text_message);
    ws_send_json_pong(&app);
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "1:'{"));
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "\"type\": \"pong\""));

    ws_json_command(&app, WST_PING, NULL);
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "2:'{"));
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "\"type\": \"pong\""));

    const char *msg= "{ \"type\": \"ping\" }";
    ws_getClientContext_IgnoreAndReturn(&ctx);
    plugin_ws_OnTextFrame(app.s, msg, strlen(msg), NULL);
    // printf("%s\n",g_last_sent_json);
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, ":'{"));
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "\"type\": \"pong\""));
}

int stubExecute(data_handle_t *dh, DbQuery *db_query){
    // db_query->result_count = g_stubQuerySet.result_count;
    memcpy(db_query, &g_stubQuerySet, sizeof(DbQuery));
    return g_returnExecute;
}
/**
 * GEO DATA API stubs
 */
int stubGeoGetMaxUser(data_handle_t *dh, int *out_count){
    *out_count = 1;
    return 0;
}

int stubGeoGetUser(data_handle_t *dh, int idx, user_data_t **out_user){
    return 0;
}
user_data_t *g_returnFindUserBySession=NULL;
user_data_t* stubFindUserBySession(data_handle_t *dh, const char* session_key){
    return g_returnFindUserBySession;
}
/**
 * WST_HELLO  using GEO data api
 */
void test_ws_wst_hello(){
    const char *session_id= "ABCDEFG";
    ClientContext ctx;
    snprintf(ctx.request.session_id, sizeof( ctx.request.session_id ), "%s", session_id);
    user_data_t u;
    u.id =1;
    u.lat = 2;
    u.lon = 3;
    u.alt = 1.2;
    u.version = 4;
    WsProcessorApi_t api={
        .onWsTextFrame = stubOnWsTextFrame
    };
    ws_session_t ws;
    ws.ctx = &ctx;
    AppContext_t app;
    app.ctx = &ctx;
    app.s = &ws;
    ws.user_data = (void*)&app;
    data_handle_t geoh;
    data_api_geo_t geoapi={
        .get_max_user = stubGeoGetMaxUser,
        .get_user = stubGeoGetUser,
        .find_user_by_session = stubFindUserBySession
    };
    geoh.specific_api = &geoapi;
    user_data_t ud={
        .id = 10, .nick="nick10",
        .index=0, 
        .lat = 11.11, .lon=22.22, .alt=1.2,
        .version=1
    };
    strcpy(ud.session_key, session_id);
    g_returnFindUserBySession = &ud;

    g_returnSendMessage = 0;
    ws_send_text_message_StubWithCallback(stub_ws_send_text_message);
    const char *msg= "{ \"type\": \"hello\", \"session_id\": \"ABCDEFG\", \"user_id\": 10 }";
    ws_getClientContext_IgnoreAndReturn(&ctx);
    data_get_handle_by_name_ExpectAndReturn("geo", &geoh);
    // ws_get_user_data_IgnoreAndReturn(0);
    // ws_set_user_data_Ignore();
    CommandResult_t res = plugin_ws_OnTextFrame(app.s, msg, strlen(msg), (void*)&app);
    TEST_ASSERT_EQUAL(CR_PROCESSED, res);
    // printf("%s\n", g_last_sent_json);
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "0:'{"));
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "\"type\": \"user_data\""));
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "\"user_id\": 10"));
}

// Stub for ws_gen_acception_key for C99 compatibility
static int stub_ws_gen_acception_key(const char *guid, const char *key, char *keyaccept,
     size_t keyacceptlen, int cmock_num_calls)
{
    strncpy(keyaccept, "fakekey", keyacceptlen);
    return 0;
}

void test_ws_ws_handler(){
    PluginContext pc;
    ClientContext ctx;
    ws_session_t ws;
    ws.ctx = &ctx;
    WsRequestParams wsparams= {0};
    ctx.request.header_count = 1;
    strcpy(ctx.request.headers[0].key, "Sec-WebSocket-Key");
    strcpy(ctx.request.headers[0].value, "dGhlIHNhbXBsZSBub25jZQ==");
    int pipefd[2];
    pipe(pipefd);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    ctx.socket_fd = pipefd[1];

    // this part of the communication will just wait in the socket, which is ok in this test.
    const char *msg = "{ \"type\": \"hello\", \"session_id\": \"ABCDEFG\", \"user_id\": 10 }";
    write(pipefd[0], "\x81\x2b", 2); // WebSocket text frame header, FIN=1, opcode=1, length=43
    write(pipefd[0], msg, strlen(msg));

    g_returnSendMessage = 0;
    ws_send_text_message_StubWithCallback(stub_ws_send_text_message);
    ws_gen_acception_key_StubWithCallback(stub_ws_gen_acception_key);
    ws_session_create_IgnoreAndReturn(&ws);
    ws_getClientContext_IgnoreAndReturn(&ctx);
    // this test can only validate the first phase of the communication, mock...
    ws_set_user_data_Ignore();
    ws_handle_ws_loop_IgnoreAndReturn(0);
    ws_session_destroy_Ignore();
    
    ws_ws_handler(&pc, &ctx, &wsparams); // function under test...
    sleep(1); // let the pipe handler / kernel run

    //read the http response, with the accept.
    char upgrade_reply[1024] = {0};
    read(pipefd[0], upgrade_reply, sizeof(upgrade_reply)-1);

    TEST_ASSERT_NOT_NULL(strstr(upgrade_reply, "HTTP/1.1 101 Switching Protocols"));
    TEST_ASSERT_NOT_NULL(strstr(upgrade_reply, "Upgrade: websocket"));
    TEST_ASSERT_NOT_NULL(strstr(upgrade_reply, "Sec-WebSocket-Accept"));

    close(pipefd[0]);
    close(pipefd[1]);
}

/*
// TODO: later if we have sql backand used.
void test_ws_some_sql_backend(){
    ClientContext ctx;
    user_data_t u;
    u.id =1;
    u.lat = 2;
    u.lon = 3;
    u.alt = 1.2;
    u.version = 4;
    WsProcessorApi_t api={
        .onWsTextFrame = stubOnWsTextFrame
    };
    AppContext_t app;
    app.ctx = &ctx;
    data_handle_t sqlh;
    data_api_sql_t sqlapi={
        .execute = stubExecute
    };
    sqlh.specific_api = &sqlapi;
    g_stubQuerySet.result_count = 1;
    snprintf(g_stubQuerySet.rows[0], sizeof(g_stubQuerySet.rows[0]), "%d|%s|%0.2f|%0.2f|%0.2f",
        10, "nick10", 11.11, 22.22, 1.2
    );

    g_returnSendMessage = 0;
    ws_send_text_message_StubWithCallback(stub_ws_send_text_message);
    const char *msg= "{ \"type\": \"hello\", \"session_id\": \"ABCDEF\", \"user_id\": 10 }";
    ws_getClientContext_IgnoreAndReturn(&ctx);
    // data_get_handle_by_name_ExpectAndReturn("sql", &sqlh);
    data_get_handle_by_name_ExpectAndReturn("geo", &sqlh);

    plugin_ws_OnTextFrame(app.s, msg, strlen(msg), NULL);

    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "4:'{"));
    TEST_ASSERT_NOT_NULL(strstr(g_last_sent_json, "\"type\": \"pong\""));
}
*/