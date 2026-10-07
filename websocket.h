#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <libwebsockets.h>
#include <windows.h>
#include "uart.h"

extern struct lws_context *g_context;

DWORD WINAPI websocket_thread(LPVOID arg);
void ws_send_status(const char *agent_id, const char *state, const char *number);
void ws_send_onCall_status(const char *agent_id, const char *state, const char *number, const char *rec);
void ws_send_hangup_status(const char *agent_id, const char *state, const char *number, const int causeCode, const char *rec);
void ws_send_device_info(
        const char *com,
        const char *imei,
        const char *imsi,
        const char *operator_name);
void ws_send_sound_devices(const char *devices);
#endif
