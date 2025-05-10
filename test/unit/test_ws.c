/**
 * Test Ws
 * According to RFC 6455. First two bytes are mandatory, further fields
 * are depending on MA, PLEN fields.
 * If payload len <= 125: no Extended payload length encoded.
 * If payload len == 126: 2 byte extended (see diagram)
 * If payload len == 127: 8 byte extended (lower 32 bit is used)
 * If the MASK bit is set to 1, a masking key is present in the
 * following 4 bytes…” (RFC 6455, Section 5.2)
 * Then the payload bytes starts.
 * The baseline of the telegram is:
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * |F|R|R|R| opcode|M| Payload len | Extended payload length       |
 * |I|S|S|S|  (4)  |A|     (7)     | now 16, but can be 0..64 bits |
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * | Masking key (if MA MASK set)                                  |
 * +-------------------------------+-------------------------------+
 * | Payload Data[0] |
 * Known opcodes:
 * 0: continous, 1: text, 2; binary, 8: close, 9: ping, a: pong.
 */
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

#include "ws.c"

void errormsg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

PluginHostInterface g_host_fns = {
    .errormsg=errormsg,
    .debugmsg=errormsg,
    .logmsg=errormsg
};
const PluginHostInterface *g_host = &g_host_fns;

int g_is_running = 0;
int g_keep_running = 1;
int g_sleep_is_needed = 0;

void setUp(void) {}
void tearDown(void) {}


CommandResult_t ws_json(ClientContext *ctx, const unsigned char *payload){
    return CR_ERROR;
}

/**
 *  Requirement: Integrator shall verify if the used external API works...
 */
void test_ws_gen_acception_key(void) {
    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const char *key = "dGhlIHNhbXBsZSBub25jZQ=="; // this is the key from the client request
    char keyaccept[128] = {0};
    int keyacceptlen = sizeof(keyaccept);
    int ret= ws_gen_acception_key(guid, key, keyaccept, keyacceptlen);
    TEST_ASSERT_EQUAL(0, ret);
}

/**
 * header parser test
 */
void test_ws_parse_header(void) {
    // Unmasked, small payload (text frame)
    unsigned char frame1[] = { 0x81, 0x05 }; // FIN + opcode 1, payload len 5
    ws_session_t s = {0};
    s.frame = (char*)frame1;
    s.frame_offset = sizeof(frame1);
    int r = ws_parse_header(&s);
    TEST_ASSERT_EQUAL(0, r);  // too short
    
    // Masked, small payload
    unsigned char frame2[] = { 0x82, 0x85, 0x12, 0x34, 0x56, 0x78 }; // FIN + opcode 2, masked + len 5 + 4 mask bytes
    memset(&s, 0, sizeof(s));
    s.frame = (char*)frame2;
    s.frame_offset = sizeof(frame2);
    r = ws_parse_header(&s);
    TEST_ASSERT_EQUAL(0, r); // there is only header, but no payload yet: read more
    TEST_ASSERT_EQUAL(2, s.mask_offset);
    TEST_ASSERT_EQUAL(5, s.payload_len);

    // Payload length >125 (126 case)
    unsigned char frame3[] = { 0x81, 126, 0x00, 0x7E }; // len = 126 (0x007E = 126)
    memset(&s, 0, sizeof(s));
    s.frame = (char*)frame3;
    s.frame_offset = sizeof(frame3);
    r = ws_parse_header(&s);
    TEST_ASSERT_EQUAL(0, r); // extended payload, still needed more.

    unsigned char frame4[] = { 0x82, 0x85, 0x12, 0x34, 0x56, 0x78, 0x01, 0x02, 0x03, 0x04, 0x05 }; // FIN + opcode 2, masked + len 5 + 4 mask bytes
    memset(&s, 0, sizeof(s));
    s.frame = (char*)frame4;
    s.frame_offset = sizeof(frame4);
    r = ws_parse_header(&s);
    TEST_ASSERT_EQUAL(1, r); // there is all 11 bytes...
    TEST_ASSERT_EQUAL(2, s.mask_offset);
    TEST_ASSERT_EQUAL(5, s.payload_len);
    TEST_ASSERT_EQUAL(2, s.opcode);
}

/**
 * Requirement: ws_parse_header shall handle unmasked frame (Mask bit = 0)
 */
void test_ws_parse_header_unmasked(void) {
    // Unmasked frame with small payload
    unsigned char frame[] = { 0x81, 0x05, 'H', 'e', 'l', 'l', 'o' }; // FIN + text frame, payload len 5, unmasked
    ws_session_t s = {0};
    s.frame = (char*)frame;
    s.frame_offset = sizeof(frame);
    int r = ws_parse_header(&s);
    TEST_ASSERT_EQUAL(1, r);  // should be complete
    TEST_ASSERT_EQUAL(0, s.mask_size); // mask bit should be 0
    TEST_ASSERT_EQUAL(5, s.payload_len);
    TEST_ASSERT_EQUAL(0x81, (unsigned char)frame[0]);
    TEST_ASSERT_EQUAL(0x05, (unsigned char)frame[1]);
}


/**
 * Requirement: WebSocket handler loop shall process a basic text message and exit on close frame.
 */
void test_ws_handle_ws_loop_basic(void) {
    // Mock context
    ClientContext ctx = {0};
    int pipefd[2];
    pipe(pipefd);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    ctx.socket_fd = pipefd[0];
    
    // Prepare minimal WS frame (masked "hello" text frame)
    unsigned char payload[] = "hello";
    size_t payload_len = strlen((char*)payload);
    unsigned char frame[128] = {0};
    frame[0] = 0x81; // FIN + text frame
    frame[1] = 0x80 | (unsigned char)payload_len; // masked payload
    frame[2] = 0x12; frame[3] = 0x34; frame[4] = 0x56; frame[5] = 0x78; // masking key

    for (size_t i = 0; i < payload_len; i++) {
        frame[6 + i] = payload[i] ^ frame[2 + (i % 4)];
    }

    write(pipefd[1], frame, 6 + payload_len);

    // Insert close frame to end loop
    unsigned char close_frame[] = { 0x88, 0x00 }; // opcode=8, no payload
    write(pipefd[1], close_frame, sizeof(close_frame));

    WsRequestParams dummy = {0};
    char buf[BUF_SIZE];
    size_t buf_offset = 0;
    // Call function
    int ret; // = ws_handle_ws_loop(NULL, &ctx, &dummy);
    //TODO : WRITE this test where the actual function is called.
   // ret= ws_process_socket_frame(&ctx, buf, &buf_offset);
    TEST_ASSERT_EQUAL(0, ret);

    close(pipefd[0]);
    close(pipefd[1]);
}

/**
 * Requirement: the statemachine payload processor shall implement the WS specific mask buffer task.
 */
void test_ws_mask_payload(void) {
    ws_session_t ss;
    char tbuf[] = {
        0x81, 0x8B,
        0x12, 0x34, 0x56, 0x78, //mask
        'h' ^ 0x12,
        'e' ^ 0x34,
        'l' ^ 0x56,
        'l' ^ 0x78,
        'o' ^ 0x12
    };
    ss.frame = tbuf;
    ss.frame_capacity = 11;
    ss.frame_offset = 11;
    ss.frame_len = 11;
    ss.payload_len = 5;
    ss.payload = (unsigned char*)&ss.frame[6];
    ss.mask_offset = 2;
    ss.mask_size = 0;
    char* encoded = &tbuf[6];

    ws_mask_payload(&ss); // do nothing
    TEST_ASSERT_EQUAL_UINT8('h'^ 0x12, encoded[0]);

    ss.mask_size = 4;
    ws_mask_payload(&ss); // mask
    TEST_ASSERT_EQUAL_UINT8('h', encoded[0]);
    TEST_ASSERT_EQUAL_UINT8('e', encoded[1]);
    TEST_ASSERT_EQUAL_UINT8('l', encoded[2]);
    TEST_ASSERT_EQUAL_UINT8('l', encoded[3]);
    TEST_ASSERT_EQUAL_UINT8('o', encoded[4]);
}

/**
 * Requirement: Adaptive buffer management, grow the buffer if reach a MARGIN.
 * boundary test.
 */
void test_ws_check_frame_need_to_grow(void){
    ws_session_t s;
    s.state = WS_STATE_READ_HEADER;
    s.frame = malloc(BUF_SIZE); // initial buffer
    s.frame_capacity = BUF_SIZE;
    s.frame_shrink_count = 0;
    s.frame_offset =  BUF_SIZE - WS_FRAME_GROW_MARGIN - 1;
    int ret= ws_check_frame_need_to_grow(&s);
    TEST_ASSERT_EQUAL(ret, 1); // do nothing
    TEST_ASSERT_EQUAL(BUF_SIZE, s.frame_capacity);
    TEST_ASSERT_EQUAL( WS_STATE_READ_HEADER, s.state);

    s.frame_offset =  BUF_SIZE - WS_FRAME_GROW_MARGIN;
    ret= ws_check_frame_need_to_grow(&s);
    TEST_ASSERT_EQUAL(ret, 1); // do nothing
    TEST_ASSERT_EQUAL(BUF_SIZE, s.frame_capacity);
    TEST_ASSERT_EQUAL( WS_STATE_READ_HEADER, s.state);

    s.frame_offset =  BUF_SIZE - WS_FRAME_GROW_MARGIN + 1;
    ret= ws_check_frame_need_to_grow(&s);
    TEST_ASSERT_EQUAL( ret, 1 ); // do nothing
    TEST_ASSERT_EQUAL( BUF_SIZE*2, s.frame_capacity );
    TEST_ASSERT_EQUAL( WS_STATE_READ_HEADER, s.state );

    free(s.frame);

}

/**
 * Requirement: track recent maximum payload size across frames
 */
void test_ws_check_frame_max(void){
    ws_session_t s = {0};
    s.recent_max_payload_len = 100;
    s.payload_len = 200;
    s.frame_shrink_count = 1;

    ws_check_frame_max(&s);
    TEST_ASSERT_EQUAL(200, s.recent_max_payload_len);

    s.payload_len = 150;
    ws_check_frame_max(&s);
    TEST_ASSERT_EQUAL(200, s.recent_max_payload_len); // max should remain

    s.payload_len = 300;
    ws_check_frame_max(&s);
    TEST_ASSERT_EQUAL(300, s.recent_max_payload_len); // updated to new max

    // no update if shrink_count == 0
    s.frame_shrink_count = 0;
    s.payload_len = 500;
    ws_check_frame_max(&s);
    TEST_ASSERT_EQUAL(300, s.recent_max_payload_len); // still same
}

/**
 * Requirement: Adaptive buffer management, shrink buffer based on recent max size
 */
void test_ws_check_frame_need_to_shrink(void){
    ws_session_t s = {0};
    s.frame = malloc(BUF_SIZE * 4);
    s.frame_capacity = BUF_SIZE * 4;
    s.payload_len = BUF_SIZE;
    s.recent_max_payload_len = BUF_SIZE;
    s.frame_shrink_count = WS_FRAME_SHRINK_THRESHOLD - 2;

    ws_check_frame_need_to_shrink(&s);
    TEST_ASSERT_EQUAL(WS_FRAME_SHRINK_THRESHOLD-1, s.frame_shrink_count);
    TEST_ASSERT_EQUAL(BUF_SIZE * 4, s.frame_capacity);

    // Trigger actual shrink
    ws_check_frame_need_to_shrink(&s);
    TEST_ASSERT(s.frame_capacity <= BUF_SIZE * 4);
    TEST_ASSERT(s.frame_capacity >= BUF_SIZE);
    TEST_ASSERT_EQUAL(0, s.frame_shrink_count);

    free(s.frame);
}

/**
 * Requirement: ws_state_step shall handle multiple frames in a single read.
 */
void test_ws_state_step_multiple_frames(void) {
    ClientContext ctx = {0};
    int pipefd[2];
    pipe(pipefd);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    ctx.socket_fd = pipefd[0];

    // Prepare two small text frames (masked "hi" and "ok")
    unsigned char payload1[] = "hi";
    unsigned char mask1[4] = {0x01, 0x02, 0x03, 0x04};
    unsigned char frame1[8] = {
        0x81, 0x82, mask1[0], mask1[1], mask1[2], mask1[3],
        payload1[0] ^ mask1[0], payload1[1] ^ mask1[1]
    };

    unsigned char payload2[] = "ok";
    unsigned char mask2[4] = {0x01, 0x06, 0x07, 0x08};
    unsigned char frame2[8] = {
        0x81, 0x82, mask2[0], mask2[1], mask2[2], mask2[3],
        payload2[0] ^ mask2[0], payload2[1] ^ mask2[1]
    };

    // Combine both into single buffer to simulate back-to-back frames
    unsigned char combo[16];
    memcpy(combo, frame1, 8);
    memcpy(combo + 8, frame2, 8);
    write(pipefd[1], combo, 16);

    WsRequestParams dummy = {0};
    ws_session_t s = {0};
    s.ctx = &ctx;
    s.frame = malloc(BUF_SIZE);
    s.frame_capacity = BUF_SIZE;
    s.state = WS_STATE_READ_HEADER;

    // Step should process both frames
    int total_processed = 0;
    for (int i = 0; i < 10; ++i) {
        int ret = ws_state_step(&s);
        if (s.state == WS_STATE_MASK_AND_DECODE){
            if (ret == 1)
                total_processed++;
        }
    }

    // We expect at least 2 processed steps (one per frame)
    TEST_ASSERT_GREATER_OR_EQUAL(2, total_processed);

    free(s.frame);
    close(pipefd[0]);
    close(pipefd[1]);
}

/**
 * Requirement: ws_state_step shall handle various known opcodes (text, binary, ping, pong, close)
 */
void test_ws_state_step_known_opcodes(void) {
    ClientContext ctx = {0};
    int pipefd[2];
    pipe(pipefd);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    ctx.socket_fd = pipefd[0];

    // Text frame (unmasked)
    unsigned char frame1[] = { 0x81, 0x02, 'O', 'K' }; // text "OK"
    // Binary frame (unmasked)
    unsigned char frame2[] = { 0x82, 0x02, 0xAA, 0xBB };
    // Ping frame (unmasked)
    unsigned char frame3[] = { 0x89, 0x00 };
    // Pong frame (unmasked)
    unsigned char frame4[] = { 0x8A, 0x00 };
    // Close frame (unmasked)
    unsigned char frame5[] = { 0x88, 0x00 };

    unsigned char combo[32];
    int offset = 0;
    memcpy(combo + offset, frame1, sizeof(frame1)); offset += sizeof(frame1);
    memcpy(combo + offset, frame2, sizeof(frame2)); offset += sizeof(frame2);
    memcpy(combo + offset, frame3, sizeof(frame3)); offset += sizeof(frame3);
    memcpy(combo + offset, frame4, sizeof(frame4)); offset += sizeof(frame4);
    memcpy(combo + offset, frame5, sizeof(frame5)); offset += sizeof(frame5);

    write(pipefd[1], combo, offset);

    WsRequestParams dummy = {0};
    ws_session_t s = {0};
    s.ctx = &ctx;
    s.frame = malloc(BUF_SIZE);
    s.frame_capacity = BUF_SIZE;
    s.state = WS_STATE_READ_HEADER;

    int steps = 0;
    for (int i = 0; i < 20; ++i) {
        int ret = ws_state_step(&s);
        // printf("op:%02x  st:%x * ", s.opcode, s.state);
        // for (int j=0; j<s.frame_len; j++) printf("%02x ", 0xFFU&(unsigned char)s.frame[j]);
        // printf("\n");
        if (ret == 1){
            steps++;
        }else{
            break;
        }
    }

    TEST_ASSERT_GREATER_OR_EQUAL(5, steps); // Expect all 5 frames processed

    free(s.frame);
    close(pipefd[0]);
    close(pipefd[1]);
}