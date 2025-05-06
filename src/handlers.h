/*
 * File:    handlers.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Main handlers are statistics, and exaples for the APIs only
 * Key features:
 *  status report, info, test page
 */
#ifndef HANDLERS_H
#define HANDLERS_H
void handle_status_html(ClientContext *ctx, RequestParams *params);
void handle_status_json(ClientContext *ctx, RequestParams *params);
void infopage(ClientContext *ctx, RequestParams *params);
void test_image(ClientContext *ctx, RequestParams *params);
#endif // HANDLERS_H
