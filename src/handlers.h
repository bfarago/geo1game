#ifndef HANDLERS_H
#define HANDLERS_H
void handle_status_html(ClientContext *ctx, RequestParams *params);
void handle_status_json(ClientContext *ctx, RequestParams *params);
void infopage(ClientContext *ctx, RequestParams *params);
void test_image(ClientContext *ctx, RequestParams *params);
#endif // HANDLERS_H
