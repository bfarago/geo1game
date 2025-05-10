/**
 * File:    ws.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-01
 * 
 * WebSocket implementation
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
 * 0: continous, 1: text, 2; binary, 8: close, 9: ping, 10: pong.
 * Unknown opcodes (3..7, b..f) handling: If an unknown opcode is
 * received, the receiving endpoint MUST fail the WebSocket connection.
 * Reserved bits: If the endpoint does not understand a non-zero
 * RSV1/2/3 bit and has not negotiated an extension that defines it,
 * it MUST fail the connection.
 * Due to a production ready system need to be protected, we implemented
 * the RFC limitiations, and also addeed a limit for the decodable
 * buffer length max value to a practical value. These all could help
 * against Deny of Service type attacks.
 * The socket shall be non_blocking. In case of more bytes needed
 * according to the protocol and actual frame, then usleep executed on
 * this task. In case of the frame fragmented by MTU, handled.
 * In case of router/switch device MTU allows multiple telegram to be
 * merged (more frame arrives in one read), handled.
 * Handshake (secret negotiation is implemented)
 * 
 * TODO: (implementation is ongoing) Known limitations:
 * - The continous transmission OP:0 & FIN is partially implemented, 
 * it is ready only at header decoder side, and not yet tested.
 * But this one is rearly used. Typically applicable for binary
 * streaming use-case only.
 * 
 * - There is a plan to add more statistics, there are only textual
 * debug logs at this point. Update: some statistics implemented...
 * 
 * - It would be nice, if this application need to shutdown at server
 * side, then first send a close message to the peer, before hungs up
 * the socket...
 * 
 * - Even if TCP manages the connection state, it would be rational to
 * use the statistics, like peer does not answer to the ping, and change
 * some state of the clientcontext, maybe abort it as well. Need to figure
 * out, if this will help on anything or not.
 */
#define _GNU_SOURCE
#include <time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <inttypes.h>
 
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

// #include <json-c/json.h> 

#include "../plugin.h"
#include "../data.h"
#include "../data_geo.h"
#include "../data_sql.h"

#define WS_EXPOSE_INTERNALS
#include "ws.h"

/**
 * Realloc control
 */
// Starts from, and grows in this quantum
#define WS_FRAME_QUANTUM                    (8 * 1024)
// Grows if less than this margin left from the actual buffer
#define WS_FRAME_GROW_MARGIN                (1024)  
// shrink if N times a smaller max(len) payload seen. 2<N<254 !
#define WS_FRAME_SHRINK_THRESHOLD           (10)
// Protection aginst DOS, aborts the connection if higher requested. Max: sizeof(size_t).
#define WS_FRAME_ABSOLUTE_MAX_BUF_LENGTH    (32*1024*1024) // 32 Mbyte

/**
 * Timing related control
 */
// In case of more bytes needed, but read does not provided any, wait in usleep.
#define WS_READ_SLEEP_TIME_US (10*1000) // 10ms
#define WS_WRITE_SLEEP_TIME_US (1*1000)  // 1ms
#define WS_FRAME_SHRINK_TIMEOUT_SEC (60)    // one minute
#define WS_LL_SEND_PING_SEC (10) // 10 sec
#define WS_AGGREGATION_TIME_SEC (5) // calculate statistics in 5sec, can be 60sec later...

/**
 * OPCODE related constants
 */
#define WS_OP_CONTINUATION_FRAME    (0)
#define WS_OP_TEXT_FRAME            (1)
#define WS_OP_BINARY_FRAME          (2)
#define WS_OP_CONNECTION_CLOSE      (3)
#define WS_OP_PING                  (9)
#define WS_OP_PONG                  (10)
#define WS_RESERVED_OPCODE_MASK ((1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15))

// globals
extern const PluginHostInterface *g_host;
extern int g_is_running;
extern int g_keep_running;
extern int g_sleep_is_needed;

static const char * g_wsm_labels[WSM_MAX_ID]={
    [WSM_BYTES_RX] = "RxB",
    [WSM_BYTES_TX] = "TxB",
    [WSM_FRAME_RX] = "RxF",
    [WSM_FRAME_TX] = "TxF",
    [WSM_SLEEP_RX] = "SlR",
    [WSM_SLEEP_TX] = "SlT",
    [WSM_LL_PING] = "LPi",
    [WSM_LL_PONG] = "LPo",
    [WSM_MEMORY_SHRINK] = "Shr",
    [WSM_MEMORY_GROW] = "Grw",
    [WSM_MULTIPLE_FRAMES] = "Mul",
    [WSM_ERROR_ABSOLUTE_MAX_REACHED] = "EAM",
    [WSM_ERROR_MEMORY_ALLOCATION] = "EMA",
    [WSM_ERROR_RESERVED] = "ERs",
    [WSM_ERROR_PROTOCOL_ABORT] = "EPA"
};

/** ws_measure_clear
 * Clear counters
 */
void ws_measure_clear(ws_session_t *s){
    memset(&s->measure, 0, sizeof(s->measure));
}

/** ws_measure_aggregate
 * Aggregate counters
 */
void ws_measure_aggregate(ws_session_t *s){
    for (int i=0; i<WSM_MAX_ID; i++){
        ws_onemeasurement *p= &s->measure.actual[i];
        // unsigned short v= p->counter;
        if (p->max < p->min){
            // there was no fn call
        }else{
            s->measure.avg[i]= ((unsigned long)p->max + (unsigned long)p->min) / 2;
            p->max = 0;
            p->min = 0xFFFFu;
        }
    }
}

/** ws_measure_add
 * Increment a selected counter by a specified signed dig argument, it can be negative as well.
 */
void ws_measure_add(ws_session_t *s, ws_measurement_id id, int dif){
    ws_onemeasurement *p= &s->measure.actual[id];
    long v= p->counter;
    v+=dif;
    if (v > (long)0x0000FFFEul) {
        // saturate and overshoot
        v = 0x0000FFFFul;
    }
    if (v < 0) {
        v = 0;
    }
    if (p->min > v) p->min = v;
    if (p->max < v) p->max = v;
    p->counter = (unsigned short)v;
}

/** ws_measure
 * Usually the layer code just increment by 1
 */
void ws_measure(ws_session_t *s, ws_measurement_id id){
    ws_measure_add(s, id, 1);
}

/** ws_measure_dump_str
 * Dump the textual output of a session's statistics
 */
int ws_measure_dump_str(ws_session_t *s, char *buf, size_t len){
    if (!s) return 0;
    int o=0;
    o+=snprintf(buf, len - o, "WS session ");
    ClientContext *ctx= s->ctx;
    if (ctx){
        if (ctx->client_ip[0] != 0) {
            o+=snprintf(buf, len - o, "ip:%s ", ctx->client_ip);
        }
    }
    for (int i=0; i<WSM_MAX_ID; i++){
        ws_onemeasurement *p = &s->measure.actual[i];
        o+=snprintf(buf, len - o, "%s: %d", g_wsm_labels[i], s->measure.avg[i]);
        if (p->max > p->min){
            o+=snprintf(buf, len - o, "(%d - %d) ", s->measure.actual[i].min, s->measure.actual[i].max);
        }
    }
    o+=snprintf(buf, len - o, " avg (min - max)\n");
    return o;
}
/** ws_gen_acception_key
 * acception_key protocol, part of the handshake phase.
 */

int ws_gen_acception_key(const char* guid, const char* input_key, char* out_buf, size_t out_buf_len)
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
    return 0;
}

/** */
ClientContext* ws_getClientContext(ws_session_t *s)
{
    return s->ctx;
}
/** ws_send_all
 * Send frame to the client in one block at the lowes layer.
 * This implementation is needed due to NON block sockets could reach
 * a full state, when write returns by Error status.
 * Different machanism also possible:
 * a) framing as multi frame
 * b) write queue
 */
ssize_t ws_send_all(ws_session_t *s, const void *buf, size_t len) {
    if (!s || !s->ctx || !buf || !len) return -1;
    int fd = s->ctx->socket_fd;
    size_t total_sent = 0;
    const char *ptr = buf;
    while (total_sent < len) {
        ssize_t sent = write(fd, ptr + total_sent, len - total_sent);
        if (sent < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(WS_WRITE_SLEEP_TIME_US);
                ws_measure(s, WSM_SLEEP_TX);
                continue;
            }
            return -1; // error
        }
        total_sent += sent;
    }
    return total_sent;
}
/** ws_send_frame
 * Send frame to the client. Please use this function all-the-time, due to 
 * the statistics, and the lower layer integration.
 */
int ws_send_frame(ws_session_t *s, char *buf, size_t len){
    int ret = ws_send_all(s, buf, len);
    ws_measure(s, WSM_FRAME_TX);
    ws_measure_add(s, WSM_BYTES_TX, len);
    return ret;
}

/** ws_build_frame
 * Build a frame around the tx payload.
 */
int ws_build_frame(const unsigned char *payload, size_t payload_len, unsigned char opcode, char **out_buf, size_t *out_len) {
    size_t header_len = 2;
    size_t total_len = 0;
    unsigned char header[10];

    header[0] = 0x80 | (opcode & 0x0F); // FIN=1, opcode

    if (payload_len <= 125) {
        header[1] = (unsigned char)payload_len;
        header_len = 2;
    } else if (payload_len <= 0xFFFF) {
        header[1] = 126;
        header[2] = (payload_len >> 8) & 0xFF;
        header[3] = payload_len & 0xFF;
        header_len = 4;
    } else {
        header[1] = 127;
        memset(&header[2], 0, 4); // high 32 bit zero
        header[6] = (payload_len >> 24) & 0xFF;
        header[7] = (payload_len >> 16) & 0xFF;
        header[8] = (payload_len >> 8) & 0xFF;
        header[9] = payload_len & 0xFF;
        header_len = 10;
    }

    total_len = header_len + payload_len;
    char *buf = malloc(total_len);
    if (!buf) return -1;

    memcpy(buf, header, header_len);
    memcpy(buf + header_len, payload, payload_len);

    *out_buf = buf;
    *out_len = total_len;
    return 0;
}

/** ws_send_binary_message
 * Sends a binary message on a WS session.
 */
int ws_send_binary_message(ws_session_t *s, const unsigned char *buf, size_t len){
    if (!buf || !s || !len) return -1;
    char *frame = NULL;
    size_t frame_len = 0;

    if (ws_build_frame(buf, len, WS_OP_BINARY_FRAME, &frame, &frame_len) < 0) {
        g_host->errormsg("WebSocket build frame failed");
        return -1;
    }

    int ret = ws_send_frame(s, frame, frame_len);
    free(frame);
    return ret;
}

/** ws_send_text_message
 * Sends a textual message on a WS session
 */
int ws_send_text_message(ws_session_t *s, const char *msg) {
    if (!msg || !s) return -1;
    size_t len = strlen(msg);
    char *frame = NULL;
    size_t frame_len = 0;

    if (ws_build_frame((const unsigned char*)msg, len, WS_OP_TEXT_FRAME, &frame, &frame_len) < 0) {
        g_host->errormsg("WebSocket build frame failed");
        return -1;
    }

    int ret = ws_send_frame(s, frame, frame_len);
    free(frame);
    return ret;
}

/** ws_mask_payload
 * Mask WebSocket payload
 * RFC's idea to mask the payload with xor mask[idx % 4] is implemented.
 * This behaviour is optional, based on header's MA bit.
 */
void ws_mask_payload(ws_session_t *s){
    if (s->mask_size < 1) return;
    unsigned char *mask = (unsigned char*)&s->frame[s->mask_offset];
    for (size_t i = 0; i < s->payload_len; i++) {
        s->payload[i] ^= mask[i % 4];
    }
}

/** ws_check_frame_need_to_grow()
 * Maintain optimal buffer size, and grow it if needed.
 */
int ws_check_frame_need_to_grow(ws_session_t *s){
    if (s->frame_offset + WS_FRAME_GROW_MARGIN > s->frame_capacity) {
        size_t new_capacity = s->frame_capacity + BUF_SIZE;
        if (new_capacity > WS_FRAME_ABSOLUTE_MAX_BUF_LENGTH) {
            ws_measure(s, WSM_ERROR_ABSOLUTE_MAX_REACHED);
            g_host->debugmsg("WebSocket frame exceeds absolute max buffer size");
            s->state = WS_STATE_ERROR;
            return 0;
        }
        char *new_frame = realloc(s->frame, new_capacity);
        if (!new_frame) {
            ws_measure(s, WSM_ERROR_MEMORY_ALLOCATION);
            g_host->debugmsg("WebSocket frame realloc failed");
            s->state = WS_STATE_ERROR;
            return 0;
        }
        s->frame = new_frame;
        s->frame_capacity = new_capacity;
        s->frame_shrink_count = 0;
        s->recent_max_payload_len = 0; // search for a new max
        g_host->debugmsg("WebSocket frame buffer grown to %zu", s->frame_capacity);
        ws_measure(s, WSM_MEMORY_GROW);
    }
    return 1;
}

/** ws_check_frame_max()
 * Maintain optimal buffer size, by collecting the max frame sizes during a
 * period of time / a number of incoming frames. Adaptive algorithms will
 * keep a higher buffer size until a given amout of frames, and reduce only
 * if there ware shorter requests only...
 */
void ws_check_frame_max(ws_session_t *s){
    if (s->frame_shrink_count){
        if (s->payload_len > s->recent_max_payload_len){
            s->recent_max_payload_len = s->payload_len;
        }
    }
}

/** ws_check_frame_need_to_shrink
 * Maintain optimal buffer size, by seeking the oppurtinity to reduce
 * the buffer size, after N amount of frames, only if there were no
 * higher buffer demand, than a given size. The next target capacity
 * will be rounded up to the amount of quantum BUF_SIZE.
 */
void ws_check_frame_need_to_shrink(ws_session_t *s){
    if (s->frame_capacity > BUF_SIZE && s->payload_len < s->frame_capacity - BUF_SIZE) {
        s->frame_shrink_count++;
        if (s->frame_shrink_count >= WS_FRAME_SHRINK_THRESHOLD) {
            size_t target_capacity = ((s->recent_max_payload_len + BUF_SIZE - 1) / BUF_SIZE) * BUF_SIZE;
            if (target_capacity < s->frame_capacity) {
                char *new_frame = realloc(s->frame, target_capacity);
                if (new_frame) {
                    s->frame = new_frame;
                    s->frame_capacity = target_capacity;
                    ws_measure(s, WSM_MEMORY_SHRINK);
                    g_host->debugmsg("WebSocket frame buffer adaptively shrunk to %zu", target_capacity);
                }else{
                    ws_measure(s, WSM_ERROR_MEMORY_ALLOCATION);
                    s->state = WS_STATE_ERROR;// memory allocation error
                }
            }
            s->frame_shrink_count = 0;
        }
    } else {
        s->frame_shrink_count = 0;
    }
}

/** ws_check_frame_unprocessed
 * In case of the read call provided more bytes to the frame, than one
 * complete telegram, then keep the rest of the received bytes, as new
 * frame (memmove), and set the FSM to process it, set the new frame
 * rx counter (frame_offset) according to the remaining bytes.
 */
void ws_check_frame_unprocessed(ws_session_t *s){
    size_t processed = s->mask_offset + s->payload_len + s->mask_size;
    size_t remaining = s->frame_offset - processed;
    if (remaining > 0) {
        memmove(s->frame, s->frame + processed, remaining);
        s->state = WS_STATE_BUFFER_INSPECT;
        ws_measure(s, WSM_MULTIPLE_FRAMES);
    }else{
        s->state = WS_STATE_READ_HEADER;
    }
    s->frame_offset = remaining;
}

/** ws_parse_header()
 * Parse WebSocket frame header
 */
static int ws_parse_header(ws_session_t *s) {
    if (s->frame_offset < 2) {
        // minimal pre-check for basic header (2 bytes) which is mandatory
        return 0; //read more
    }
    // We will parse extended payload length and mask only after confirming enough bytes are available
    unsigned char payload_len_field = s->frame[1] & 0x7F;
    unsigned char mask_flag = (0x80 & s->frame[1]) ? 1 : 0;
    unsigned char rsv = (s->frame[0] & 0x70) >> 4;
    size_t base_header_len = 2;
    size_t extended_payload_len_len = 0;
    size_t mask_len = mask_flag ? 4 : 0;
    s->fin = (s->frame[0] & 0x80) != 0;
    s->opcode = s->frame[0] & 0x0F;
    s->mask_size = mask_len;
    if (rsv) {
        ws_measure(s, WSM_ERROR_RESERVED);
        g_host->debugmsg("Header RSV:%d bit set, connection shall fallen.", rsv);
        return -1;
    }
    if ((1<<s->opcode) & WS_RESERVED_OPCODE_MASK){
        ws_measure(s, WSM_ERROR_RESERVED);
        g_host->debugmsg("Header OP:%d reserved, connection shall fallen.", s->opcode);
        return -1;
    }
    if (payload_len_field == 126) {
        // Extended payload length uses 2 bytes
        extended_payload_len_len = 2;
        // total header length including mask for this case is base(2) + extended(2) + mask(4) = 8 bytes
        if (s->frame_offset < base_header_len + extended_payload_len_len + mask_len) {
            return 0; // wait for more bytes
        }
        // Now safe to read extended payload length
        uint16_t extended_payload_len = (s->frame[2] << 8) | s->frame[3];
        s->payload_len = extended_payload_len;
        s->mask_offset = base_header_len + extended_payload_len_len;
        s->frame_len = s->mask_offset + s->mask_size + s->payload_len;
    } else if (payload_len_field == 127) {
        // Extended payload length uses 8 bytes
        extended_payload_len_len = 8;
        // total header length including mask for this case is base(2) + extended(8) + mask(4) = 14 bytes
        if (s->frame_offset < base_header_len + extended_payload_len_len + mask_len) {
            return 0; // wait for more bytes
        }
        /* RFC 6455: 64-bit payload length, using only lower 32 bits here for simplicity,
        // memory is limited due to protection against DOS. */
        uint64_t payload_len_64 = 0;
        for (int i = 0; i < 8; i++) {
            payload_len_64 = (payload_len_64 << 8) | (unsigned char)s->frame[2 + i];
        }
        if (payload_len_64 > WS_FRAME_ABSOLUTE_MAX_BUF_LENGTH) {
            g_host->debugmsg("Payload length too large: %" PRIu64, payload_len_64);
            ws_measure(s, WSM_ERROR_ABSOLUTE_MAX_REACHED); // just requested, not reaced actually, but similar.
            return -1;
        }
        s->payload_len = (size_t)payload_len_64;
        s->mask_offset = base_header_len + extended_payload_len_len;
        s->frame_len = s->mask_offset + s->mask_size + s->payload_len;
    } else {
        // No extended payload length
        s->payload_len = payload_len_field;
        s->mask_offset = base_header_len;
        s->frame_len = s->mask_offset + s->mask_size + s->payload_len;
    }

    if (s->frame_offset < s->frame_len) {
        return 0; // read more for full frame
    }

    if (s->opcode == WS_OP_CONTINUATION_FRAME && !s->fragmented) {
        ws_measure(s, WSM_ERROR_PROTOCOL_ABORT);
        g_host->debugmsg("Unexpected continuation frame without initial fragmented message");
        return -1;
    } else if ((s->opcode == WS_OP_TEXT_FRAME || s->opcode == WS_OP_BINARY_FRAME) && !s->fin) {
        s->fragmented = 1;
        s->original_opcode = s->opcode;
    } else if (s->opcode != WS_OP_CONTINUATION_FRAME) {
        s->fragmented = 0;
    }
    s->payload = (unsigned char*)&s->frame[s->mask_offset + s->mask_size];
    return 1;
}

/** ws_state_step
 * WS Final State Machine processor
 * Need to be called cyclically, only process one step at one call.
 * The execution time in all the steps are limited, the longer one
 * probably the Read phase, when usleep executed state is possible.
 */
static int ws_state_step(ws_session_t *s) {
    switch (s->state) {
        case WS_STATE_BUFFER_INSPECT: {
            int header_status = ws_parse_header(s);
            if (header_status < 0) {
                s->state = WS_STATE_ERROR;
                return 0;
            } else if (header_status > 0) {
                switch (s->opcode) {
                    case WS_OP_CONNECTION_CLOSE:
                        g_host->debugmsg("WebSocket close frame received");
                        s->state = WS_STATE_DONE;
                        return 0;
                    break;
                    case WS_OP_PING:{
                        g_host->debugmsg("Ping received from client");
                        ws_measure(s, WSM_LL_PING);
                        ws_check_frame_unprocessed(s);
                        s->ping_received = 1;
                        return 1;
                    }
                    break;

                    case WS_OP_PONG:
                        g_host->debugmsg("Pong received from client");
                        ws_measure(s, WSM_LL_PONG);
                        ws_check_frame_unprocessed(s);
                        //todo: save the timestamp
                        return 1;
            
                    case WS_OP_TEXT_FRAME:
                    case WS_OP_BINARY_FRAME:
                    case WS_OP_CONTINUATION_FRAME:
                        s->state = WS_STATE_MASK_AND_DECODE;
                        ws_measure(s, WSM_FRAME_RX);
                        ws_measure_add(s,WSM_BYTES_RX, s->frame_len);
                        return 1;
            
                    default:
                        g_host->debugmsg("Unsupported WebSocket frame received");
                        ws_check_frame_unprocessed(s);
                        return 1;
                }
            }
            // need more
            if (s->frame_offset<1) {
                s->state = WS_STATE_IDLE;
            }else{
                s->state = WS_STATE_READ_HEADER;
            }
            return 1;
        }
        break;

        case WS_STATE_IDLE: {
            // not in frame, for sure, we can send if needed, or do some statistics...
            time_t now = time(NULL);
            if (s->ping_received){
                char pong_msg[] = { 0x8A, 0x00 };
                ws_send_frame(s, pong_msg, sizeof(pong_msg));
                g_host->debugmsg("Pong sent to client");
                //s->last_pong_sent = now;
                s->ping_received = 0;
            }
            // Low level ping request sending to peer, but yeah probably we need another state for this...
            if (now - s->last_ping_sent >= WS_LL_SEND_PING_SEC) {
                char ping_msg[] = { 0x89, 0x00 };
                ws_send_frame(s, ping_msg, sizeof(ping_msg));
                g_host->debugmsg("Ping sent to client");
                s->last_ping_sent = now;
            }
            // do a periodic measurement aggregation
            if (now - s->last_aggregation >= WS_AGGREGATION_TIME_SEC){
                ws_measure_aggregate(s);
                s->last_aggregation = now;
                // just for development phase, dump out some statistics to log...
                char buf[BUF_SIZE];
                ws_measure_dump_str(s, buf, sizeof(buf));
                g_host->debugmsg("Stat: ", buf);
            }
            // frame memory allocation grow/shrink management after a timeout for sure...
            if (now - s->last_frame_memory_checked >= WS_FRAME_SHRINK_TIMEOUT_SEC){
                // in case of no traffic, we try to trow the memory away after a timeout
                ws_check_frame_need_to_shrink(s);
                s->last_frame_memory_checked =now;
            }
            s->state =WS_STATE_READ_HEADER;
            return 1;
        }
        break;

        case WS_STATE_READ_HEADER: {
            ws_check_frame_need_to_grow(s);
            if (WS_STATE_ERROR == s->state) {
                // ws_measure(s, WSM_ERROR_PROTOCOL_ABORT); // already counted, add this only if needed
                return 0;
            }
            ssize_t len = read(s->ctx->socket_fd, s->frame + s->frame_offset, BUF_SIZE - s->frame_offset);
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // The connection is open, this layer needs more bytes, but kernel buffer is empty now.
                    // This thread can not do more, than wait in a sleep.
                    ws_measure(s, WSM_SLEEP_RX);
                    usleep(WS_READ_SLEEP_TIME_US);
                    return 1;
                } else {
                    g_host->debugmsg("WebSocket read error: %s", strerror(errno));
                    ws_measure(s, WSM_ERROR_PROTOCOL_ABORT);
                    s->state = WS_STATE_ERROR;
                    return 0;
                }
            } else if (len == 0) {
                g_host->debugmsg("WebSocket connection closed by peer");
                s->state = WS_STATE_DONE;
                return 0;
            }
            s->frame_offset += len;
            s->state = WS_STATE_BUFFER_INSPECT;
            return 1;
        }

        case WS_STATE_MASK_AND_DECODE: {
            ws_check_frame_max(s);
            if (s->mask_size) {
                ws_mask_payload(s);
            }
            s->state = WS_STATE_PROCESS_FRAME;
            return 1;
        }

        case WS_STATE_PROCESS_FRAME: {
            CommandResult_t cr = CR_UNKNOWN;
            if (s->opcode == WS_OP_TEXT_FRAME) {
                g_host->debugmsg("WS text message: %.*s", (int)s->payload_len, s->payload);
                if (s->onWsTextFrame){
                    cr = s->onWsTextFrame(s, (const char*)s->payload, s->payload_len, s->user_data);
                }
            }else if (s->opcode == WS_OP_BINARY_FRAME) {
                g_host->debugmsg("WS binary message");
                if (s->onWsBinaryFrame){
                    cr = s->onWsBinaryFrame(s->ctx, s->payload, s->payload_len, s->user_data);
                }
            }
            switch (cr){
                case CR_ERROR:
                case CR_UNKNOWN:
                    ws_check_frame_unprocessed(s); // change the state accordingly
                    return 1;
                break;
                case CR_QUIT:
                    s->state = WS_STATE_DONE;
                    return 0;
                break;
                default:
                    // todo: is this needed?
                    s->state = WS_STATE_SEND_RESPONSE;
                break;
            }
            return 1;
        }

        case WS_STATE_SEND_RESPONSE: {
            //this is just an example echo response, probably we will do it in handler
            char response[BUF_SIZE];
            size_t response_len = 0;
            response[0] = 0x81;
            if (s->payload_len <= 125) {
                response[1] = (unsigned char)s->payload_len;
                memcpy(&response[2], s->payload, s->payload_len);
                response_len = 2 + s->payload_len;
                ws_send_frame(s, response, response_len);
            }
            ws_check_frame_need_to_shrink(s);
            ws_check_frame_unprocessed(s);
            return 1;
        }

        default:
            ws_measure(s, WSM_ERROR_PROTOCOL_ABORT);
            s->state = WS_STATE_ERROR;
            return 0;
    }
}

/**
 * Free memory of the session object
 */
void ws_session_destroy(ws_session_t * s){
    if (!s) return;
    if (!s->frame) return;
    free(s->frame);
    free(s);
}

/**
 * Create session object
 */
ws_session_t *ws_session_create(ClientContext *ctx, const WsProcessorApi_t *callbacks, void *user_data){
    ws_session_t *s= malloc(sizeof(ws_session_t));
    ws_session_t defaults={
        .state = WS_STATE_READ_HEADER, // offset == 0, reed needed...
        .frame_offset = 0,
        .frame_capacity = BUF_SIZE,
        .frame_shrink_count = 0,
        .recent_max_payload_len = 0,
        .ping_received =0,
        .ctx = ctx,
        .user_data = user_data,
        .frame = NULL,
        .onWsBinaryFrame = NULL,
        .onWsTextFrame = NULL
    };
    *s = defaults;
    s->frame = malloc(BUF_SIZE);
    if (callbacks){
        s->onWsBinaryFrame = callbacks->onWsBinaryFrame;
        s->onWsTextFrame = callbacks->onWsTextFrame;
    }
    return s;
}

/** ws_handle_ws_loop()
 * ws protocol's process loop.
 */
int ws_handle_ws_loop(ws_session_t *session) {
    // PluginContext *pc, ClientContext *ctx, WsRequestParams *wsparams, const WsProcessorApi_t *callbacks
    //(void)pc;
    //(void)wsparams;
    //ws_session_t *session = ws_session_create(ctx, callbacks, NULL);
    ws_measure_clear(session);
    g_is_running++; // todo: atomically

    while (g_keep_running && session->state != WS_STATE_DONE) {
        int cont = ws_state_step(session);
        if (cont == 0 || session->state == WS_STATE_ERROR) break;
    }

    g_is_running--; // todo: atomically
    return 0;
}

void ws_get_info(ws_session_t *s, int *flen){
    if (s){
        *flen = s->frame_capacity / 1024;
    }
}
void * ws_get_user_data(ws_session_t* s){
    if (s){
        return s->user_data;
    }
    return NULL;
}
void ws_set_user_data(ws_session_t *s, void *user_data){
    if (s){
        s->user_data = user_data;
    }
}