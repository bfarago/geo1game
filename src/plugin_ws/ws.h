/**
 * File:    ws.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-01
 * 
 * See ws.c file for the details.
 */

#ifndef WS_H_
#define WS_H_

#include <time.h>
#include "plugin.h"

typedef enum CommandResult_t {
    CR_UNKNOWN,
    CR_QUIT,
    CR_PROCESSED,
    CR_ERROR
} CommandResult_t;

#ifdef CMOCK_VERSION
#define WS_EXPOSE_INTERNALS
#endif

#ifdef WS_EXPOSE_INTERNALS
/**
 * WS FSM states
 */
typedef enum {
    WS_STATE_IDLE,               // Not in frame
    WS_STATE_BUFFER_INSPECT,    // checks the frame header
    WS_STATE_READ_HEADER,       // read header and payload
    WS_STATE_WAIT_PAYLOAD,      // actually not implemented, part of the read state. TODO: is this needed ?
    WS_STATE_MASK_AND_DECODE,   // mask and some booking
    WS_STATE_PROCESS_FRAME,     // frame ready to process.
    WS_STATE_SEND_RESPONSE,     // optional state after rcv
    WS_STATE_DONE,              // normally exited by peer request
    WS_STATE_ERROR              // some error happend, abort
} ws_state_t;

/**
 * Statistics, measurement.
 */
typedef enum {
    WSM_BYTES_RX,
    WSM_BYTES_TX,
    WSM_FRAME_RX,                   // some frame was decoded, rx
    WSM_FRAME_TX,
    WSM_SLEEP_RX,                   // sleep due to read (timeout), fairly normal state..
    WSM_SLEEP_TX,                   // sleep during write, an indicator of tx queue is full.
    WSM_LL_PING,                    // Lower Layer PING received (at binary protocol layer)
    WSM_LL_PONG,                    // Lower layer PONG received.
    WSM_MEMORY_SHRINK,              // buffer reallocated to a shorter one
    WSM_MEMORY_GROW,                // buffer reallocated to a longer one
    WSM_MULTIPLE_FRAMES,            // rcv multiple frames in one shot
    WSM_ERROR_ABSOLUTE_MAX_REACHED, // an abort, based on config, memory limitation
    WSM_ERROR_MEMORY_ALLOCATION,    // an abort, based on free memory limitations
    WSM_ERROR_RESERVED,             // an abort based on protocol standard
    WSM_ERROR_PROTOCOL_ABORT,       // other protocol aborts
    WSM_MAX_ID
} ws_measurement_id;

typedef struct {
    unsigned short counter;
    unsigned short min, max;
}ws_onemeasurement;

typedef struct{
    ws_onemeasurement actual[WSM_MAX_ID];
    unsigned short avg[WSM_MAX_ID];
}ws_measurement;

#endif // WS_EXPOSE_INTERNALS

struct ws_session_t;
typedef struct ws_session_t ws_session_t;

typedef CommandResult_t (*onWsTextFrame_fn)(struct ws_session_t * s, const char *txt, size_t len, void *user_data);
typedef CommandResult_t (*onWsBinaryFrame_fn)(ClientContext* ctx, const unsigned char *buf, size_t len, void *user_data);

typedef struct {
    onWsBinaryFrame_fn onWsBinaryFrame;
    onWsTextFrame_fn onWsTextFrame;
} WsProcessorApi_t;


#ifdef WS_EXPOSE_INTERNALS
/**
 * WS Session
 */
typedef struct ws_session_t {
    ws_state_t state;
    size_t frame_offset;
    size_t payload_len;
    size_t mask_offset;
    size_t frame_len;
    unsigned char fin;
    unsigned char fragmented;
    unsigned char ping_received;
    unsigned char mask_size;
    unsigned char opcode;
    unsigned char original_opcode;
    unsigned char *payload; //pointer into the frame, where payload starts.
    char *frame;
    size_t frame_capacity;
    int frame_shrink_count;
    size_t recent_max_payload_len; // adaptive shrink size
    ws_measurement measure;
    time_t last_frame_memory_checked;
    time_t last_ping_sent;
    time_t last_aggregation;
    //callbacks
    onWsTextFrame_fn onWsTextFrame;
    onWsBinaryFrame_fn onWsBinaryFrame;
    //layers
    ClientContext *ctx; // lower layer
    void *user_data; // to the upper layer
} ws_session_t;

#endif // WS_EXPOSE_INTERNALS

ws_session_t *ws_session_create(ClientContext *ctx, const WsProcessorApi_t *callbacks, void *user_data);
void ws_session_destroy(ws_session_t * s);
int ws_gen_acception_key(const char* guid, const char* input_key, char* out_buf, size_t out_buf_len);

int ws_handle_ws_loop(ws_session_t *session);
// int ws_handle_ws_loop(PluginContext *pc, ClientContext *ctx, WsRequestParams *wsparams, const WsProcessorApi_t *callbacks);
ClientContext* ws_getClientContext(ws_session_t *);

/** ws_send_text_message
 * sends a text message on WS session.
 */
int ws_send_text_message(struct ws_session_t *s, const char *msg);
int ws_send_binary_message(struct ws_session_t *s, const unsigned char *buf, size_t len);

/** textual information about one session */
int ws_measure_dump_str(ws_session_t *s, char *buf, size_t len);
void ws_get_info(ws_session_t *s, int *flen);
void * ws_get_user_data(ws_session_t* s);
void ws_set_user_data(ws_session_t* s, void *user_data);
#endif // WS_H_