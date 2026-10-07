#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <cjson/cJSON.h>
#include <windows.h>
#include "uart.h"
#include "audio.h"
#include  "debug.h"

static DI_SERIAL *g_dev = NULL;
#define MAX_CLIENTS 100

/* Latest call state - used when a client connects/reconnects */
static char current_call_state[64] = "IDLE";
static char current_call_number[32] = "";
static char current_call_agent[128] = "";
static struct lws *clients[MAX_CLIENTS] = {0};
struct lws_context *g_context = NULL;

static int callback_http(struct lws *wsi,
                         enum lws_callback_reasons reason,
                         void *user,
                         void *in,
                         size_t len);
#define WS_QUEUE_SIZE 100
#define WS_MSG_SIZE   5000

typedef struct {
    char msgs[WS_QUEUE_SIZE][WS_MSG_SIZE];
    int head;
    int tail;
} WSQueue;

WSQueue ws_queue = {0};

CRITICAL_SECTION ws_lock;
static const struct lws_protocols protocols[];
void ws_send_device_info(
        const char *device_name,
        const char *imei,
        const char *imsi,
	const char *operator_name);
    void ws_enqueue(const char *msg);
static void ws_send_current_call_state(struct lws *wsi)
{
    (void)wsi;

    char temp[512];

    EnterCriticalSection(&ws_lock);

    if (strcmp(current_call_state, "IDLE") == 0) {
        LeaveCriticalSection(&ws_lock);
        return;
    }

    snprintf(temp, sizeof(temp),
        "{ \"event\": \"call\", \"agent-id\": \"%s\", \"state\": \"%s\", \"number\": \"%s\" }",
        current_call_agent,
        current_call_state,
        current_call_number);

    LeaveCriticalSection(&ws_lock);

    ws_enqueue(temp);

    LOG_NOTICE("Current call state queued: %s\n", temp);

    if (g_context) {
        lws_cancel_service(g_context);
    }
}
static int callback_ws(struct lws *wsi,
        enum lws_callback_reasons reason,
        void *user,
        void *in,
        size_t len)
{
    switch (reason) {

        case LWS_CALLBACK_ESTABLISHED:
            LOG_NOTICE("Client connected\n");

            EnterCriticalSection(&ws_lock);

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == NULL) {
                    clients[i] = wsi;
                    break;
                }
            }

            LeaveCriticalSection(&ws_lock);

           if (g_dev->dongle == DISCONNECTED)
{
    ws_send_device_info(NULL, NULL, NULL, NULL);
}
else
{
    ws_send_device_info(
        g_dev->friendly_name,
        g_dev->imei,
        g_dev->imsi,
        g_dev->operator_name);
}

//ws_send_current_call_state(wsi);

            break;

        case LWS_CALLBACK_CLOSED:
            LOG_NOTICE("Client disconnected\n");

            EnterCriticalSection(&ws_lock);

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] == wsi) {
                    clients[i] = NULL;
                    break;
                }
            }

            LeaveCriticalSection(&ws_lock);
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            LOG_NOTICE("Connection error\n");
            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
            {
                char local_msg[WS_MSG_SIZE] = {0};
                int has_msg = 0;
                EnterCriticalSection(&ws_lock);
                if (ws_queue.head != ws_queue.tail) {
                    strcpy(local_msg, ws_queue.msgs[ws_queue.head]);
                    ws_queue.head =
                        (ws_queue.head + 1) % WS_QUEUE_SIZE;
                    has_msg = 1;
                }
                LeaveCriticalSection(&ws_lock);
                if (has_msg) {
                    unsigned char buf[LWS_PRE + WS_MSG_SIZE];
                    unsigned char *p = &buf[LWS_PRE];
                    int len = strlen(local_msg);
                    memcpy(p, local_msg, len);
                    int n = lws_write(wsi, p, len, LWS_WRITE_TEXT);
                    if (n < len) {
                        LOG_NOTICE("lws_write failed\n");
                        return -1;
                    }
                    LOG_NOTICE("WS sent: %s\n", local_msg);
                    EnterCriticalSection(&ws_lock);
                    int more =
                        (ws_queue.head != ws_queue.tail);
                    LeaveCriticalSection(&ws_lock);
                    if (more)
                        lws_callback_on_writable(wsi);
                }
            }
            break;

       case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
{
    LOG_NOTICE("LWS event wait cancelled - scheduling clients\n");

    EnterCriticalSection(&ws_lock);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) {
            lws_callback_on_writable(clients[i]);
        }
    }

    LeaveCriticalSection(&ws_lock);
}
break;

        case LWS_CALLBACK_RECEIVE:
            char msg[1024];
            memset(msg, 0, sizeof(msg));
            if (len >= sizeof(msg)) {
                LOG_NOTICE("Message too large\n");
                break;
            }
            memcpy(msg, in, len);
            msg[len] = '\0';
            LOG_NOTICE("WS RX JSON: %s\n", msg);
            cJSON *root = cJSON_Parse(msg);
            if (!root) {
                LOG_NOTICE("Invalid JSON\n");
                break;
            }

            cJSON *event = cJSON_GetObjectItem(root, "event");
            cJSON *number = cJSON_GetObjectItem(root, "number");
            cJSON *agent = cJSON_GetObjectItem(root, "agent-id");

            if (!cJSON_IsString(event)) {
                LOG_NOTICE("Missing event\n");
                cJSON_Delete(root);
                break;
            }
            if (strcmp(event->valuestring, "call") == 0) {
                if (g_dev->dongle == DISCONNECTED)
                {
                    ws_send_device_info(NULL, NULL, NULL, NULL);
                    break;
                }
                cJSON *state = cJSON_GetObjectItem(root, "state");
                if (cJSON_IsString(state) && strcmp(state->valuestring, "HANGUPCALL") == 0) {
                    LOG_NOTICE("HANGUP requested from UI\n");
                    //if (g_dev->call_state != ON_CALL && g_dev->call_state != DIALING) {
                    if (g_dev->call_state != ON_CALL &&
                            g_dev->call_state != DIALING &&
                            g_dev->call_state != REMOTE_RING &&
                            g_dev->call_state != RING) {
                        LOG_NOTICE("Ignoring stale HANGUP (no active call)\n");
                        cJSON_Delete(root);
                        break;
                    }
                    g_dev->hangup_requested = 1;
                    enqueue_command(g_dev, CMD_ATH);
                    cJSON_Delete(root);
                    break;
                }
                char number_str[32] = {0};

                if (cJSON_IsString(number)) {
                    strncpy(number_str, number->valuestring,
                            sizeof(number_str) - 1);
                }
                else if (cJSON_IsNumber(number)) {
                    snprintf(number_str,
                            sizeof(number_str),
                            "%.0f",
                            number->valuedouble);
                }
                else {
                    LOG_NOTICE("Invalid number field\n");
                    cJSON_Delete(root);
                    break;
                }

                char dialcmd[64] = {0};

                LOG_NOTICE("Dial request: %s\n", number_str);

                strcpy(g_dev->number, number_str);

                if (cJSON_IsString(agent))
                    strcpy(g_dev->agent_id, agent->valuestring);

                g_dev->cmdi = CMD_ATD;

                snprintf(dialcmd,
                        sizeof(dialcmd),
                        "%s%s;",
                        get_cmd(g_dev),
                        number_str);

                LOG_NOTICE("Dial Command: %s\n", dialcmd);

                enqueue_custom(g_dev, dialcmd);
            }
            else if (strcmp(event->valuestring, "answer") == 0) {
                if (g_dev->dongle == DISCONNECTED)
                {
                    ws_send_device_info(NULL, NULL, NULL, NULL);
                    break;
                }
                enqueue_command(g_dev, CMD_ATA);
                if (cJSON_IsString(agent))
                    strcpy(g_dev->agent_id, agent->valuestring);
            }
            else if (strcmp(event->valuestring, "hangup") == 0) {
                if (g_dev->dongle == DISCONNECTED)
                {
                    ws_send_device_info(NULL, NULL, NULL, NULL);
                    break;
                }
                //if (g_dev->call_state != ON_CALL && g_dev->call_state != DIALING) {
                if (g_dev->call_state != ON_CALL &&
                        g_dev->call_state != DIALING &&
                        g_dev->call_state != REMOTE_RING &&
                        g_dev->call_state != RING) {
                    printf("Ignoring stale hangup event\n");
                    cJSON_Delete(root);
                    break;
                }

                g_dev->hangup_requested = 1;
                enqueue_command(g_dev, CMD_ATH);
            }
            else if (strcmp(event->valuestring, "mute") == 0) {
                if (g_dev->dongle == DISCONNECTED)
                {
                    ws_send_device_info(NULL, NULL, NULL, NULL);
                    break;
                }
                enqueue_command(g_dev, CMD_MUTE1);
            }

            else if (strcmp(event->valuestring, "unmute") == 0) {
                if (g_dev->dongle == DISCONNECTED)
                {
                    ws_send_device_info(NULL, NULL, NULL, NULL);
                    break;
                }
                enqueue_command(g_dev, CMD_MUTE0);
            }

            else if (strcmp(event->valuestring, "hold") == 0) {
                if (g_dev->dongle == DISCONNECTED)
                {
                    ws_send_device_info(NULL, NULL, NULL, NULL);
                    break;
                }
                enqueue_command(g_dev, CMD_CHLD);
            }

            else if (strcmp(event->valuestring, "resume") == 0) {
                if (g_dev->dongle == DISCONNECTED)
                {
                    ws_send_device_info(NULL, NULL, NULL, NULL);
                    break;
                }
                enqueue_command(g_dev, CMD_CHLD);
            }
            else if (strcmp(event->valuestring, "login") == 0)
            {
                LOG_NOTICE("Login event received from UI\n");

                if (cJSON_IsString(agent))
                {
                    strcpy(g_dev->agent_id, agent->valuestring);
                }

                ws_send_device_info(
                        g_dev->friendly_name,
                        g_dev->imei,
                        g_dev->imsi,
                        g_dev->operator_name);
            }
            else if (strcmp(event->valuestring, "upload_recording") == 0)
            {
                LOG_NOTICE("Upload request received\n");

                cJSON *file = cJSON_GetObjectItem(root, "file");
                cJSON *server_ip = cJSON_GetObjectItem(root, "server_ip");
                cJSON *call_mode = cJSON_GetObjectItem(root, "call_mode");

                if (cJSON_IsString(file) && cJSON_IsString(server_ip) && cJSON_IsString(call_mode))
                {
                    printf("File: %s\n", file->valuestring);
                    printf("Server IP: %s\n", server_ip->valuestring);
                    printf("call_mode: %s\n", call_mode->valuestring);

                    char cmd[512];
                    snprintf(cmd, sizeof(cmd),
                            "cd /d C:\\scripts && start /B php uploader.php \"%s\" \"%s\" \"%s\"",
                            file->valuestring,
                            server_ip->valuestring,
                            call_mode->valuestring
                            );

                    LOG_NOTICE("Executing async: %s\n", cmd);
                    STARTUPINFO si;
                    PROCESS_INFORMATION pi;

                    ZeroMemory(&si, sizeof(si));
                    si.cb = sizeof(si);
                    ZeroMemory(&pi, sizeof(pi));
                    char fullcmd[1024];
                    snprintf(fullcmd, sizeof(fullcmd),
                            "cmd.exe /C %s", cmd);

                    if (CreateProcess(
                                NULL,
                                fullcmd,
                                NULL,
                                NULL,
                                FALSE,
                                CREATE_NO_WINDOW,
                                NULL,
                                NULL,
                                &si,
                                &pi))
                    {
                        LOG_NOTICE("Uploader started successfully\n");

                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                    else
                    {
                        LOG_NOTICE("CreateProcess failed: %lu\n", GetLastError());
                    }

                }
            }
            else if(strcmp(event->valuestring, "get_audio_list") == 0)
            {
                get_audio_devices();
            }
            else if(strcmp(event->valuestring, "sound_devices") == 0)
            {
                int output_id = -1;
                int input_id  = -1;

                cJSON *output_index = cJSON_GetObjectItem(root, "output_device_id");
                cJSON *input_index = cJSON_GetObjectItem(root, "input_device_id");

                if (cJSON_IsString(output_index) && output_index->valuestring != NULL)
                {
                    output_id = atoi(output_index->valuestring);
                }

                if (cJSON_IsString(input_index) && input_index->valuestring != NULL)
                {
                    input_id = atoi(input_index->valuestring);
                }
                update_audio_devices(input_id, output_id);
            }
            cJSON_Delete(root);
            break;
        default:
            break;
    }

    return 0;
}
static const struct lws_protocols protocols[] = {

    {
        "http",
        callback_http,
        0,
        0,
    },

    {
        "ws-protocol",
        callback_ws,
        0,
        4096,
    },

    { NULL, NULL, 0, 0 }
};
static int callback_http(struct lws *wsi,
                         enum lws_callback_reasons reason,
                         void *user,
                         void *in,
                         size_t len)
{
    switch (reason) {

    case LWS_CALLBACK_HTTP:
        return lws_serve_http_file(
            wsi,
            "C:/tdy/www/index.html",
            "text/html",
            NULL,
            0);

    default:
        break;
    }

    return 0;
}

/*
struct lws_protocols protocols[] = {
    {
        "ws-protocol",
        callback_ws,
        0,
        1024,
    },
    { NULL, NULL, 0, 0 }
};*/

DWORD WINAPI websocket_thread(LPVOID arg) {
    DI_SERIAL *dev = (DI_SERIAL*)arg;
    g_dev = dev;

struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = 9000;
    info.protocols = protocols;

    info.ssl_cert_filepath = "C:/DI_WORK/server.crt";
    info.ssl_private_key_filepath = "C:/DI_WORK/server.key";
    info.options =
        LWS_SERVER_OPTION_VALIDATE_UTF8 |
        LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;


    struct lws_context *context = lws_create_context(&info);

    if (!context) {
        LOG_NOTICE("WebSocket init failed\n");
        return 1;
    }

    g_context = context;

    InitializeCriticalSection(&ws_lock);

    LOG_NOTICE("WebSocket server started on port 9000\n");

    while (dev->di_running) {
        lws_service(context, 50);
    }

    lws_cancel_service(context);
    Sleep(100);
    LOG_NOTICE("Stopping WebSocket server...\n");

    lws_context_destroy(context);

    DeleteCriticalSection(&ws_lock);

    LOG_NOTICE("WS thread Exited\n");
    return 0;
}

void ws_enqueue(const char *msg)
{
    EnterCriticalSection(&ws_lock);

    int next = (ws_queue.tail + 1) % WS_QUEUE_SIZE;

    if (next == ws_queue.head) {
        LOG_NOTICE("WS queue full, dropping message\n");
        LeaveCriticalSection(&ws_lock);
        return;
    }

    strncpy(ws_queue.msgs[ws_queue.tail], msg, WS_MSG_SIZE - 1);

    ws_queue.msgs[ws_queue.tail][WS_MSG_SIZE - 1] = '\0';

    ws_queue.tail = next;

    LeaveCriticalSection(&ws_lock);
}
void ws_send_status(const char *agent_id, const char *state, const char *number)
{

    char temp[512];

    snprintf(temp, sizeof(temp),
            "{ \"event\": \"call\", \"agent-id\": \"%s\", \"state\": \"%s\", \"number\": \"%s\" }", agent_id, state, number);

    ws_enqueue(temp);

    LOG_NOTICE("WS queued: %s\n", temp);
    EnterCriticalSection(&ws_lock);

    strncpy(current_call_state, state, sizeof(current_call_state) - 1);
    current_call_state[sizeof(current_call_state) - 1] = '\0';

    strncpy(current_call_number, number, sizeof(current_call_number) - 1);
    current_call_number[sizeof(current_call_number) - 1] = '\0';

    strncpy(current_call_agent, agent_id, sizeof(current_call_agent) - 1);
    current_call_agent[sizeof(current_call_agent) - 1] = '\0';

    LeaveCriticalSection(&ws_lock);

    if (g_context) {

        lws_cancel_service(g_context);

        
    }


}

void ws_send_onCall_status(
        const char *agent_id,
        const char *state,
        const char *number,
        const char *rec)
{
    char temp[512];

    snprintf(temp, sizeof(temp),
        "{ \"event\": \"call\", \"agent-id\": \"%s\", "
        "\"state\": \"%s\", \"number\": \"%s\", "
        "\"recording\": \"%s\" }",
        agent_id,
        state,
        number,
        rec);

    ws_enqueue(temp);

    LOG_NOTICE("WS queued: %s\n", temp);

    EnterCriticalSection(&ws_lock);

    strncpy(current_call_state, state,
            sizeof(current_call_state) - 1);
    current_call_state[sizeof(current_call_state) - 1] = '\0';

    strncpy(current_call_number, number,
            sizeof(current_call_number) - 1);
    current_call_number[sizeof(current_call_number) - 1] = '\0';

    strncpy(current_call_agent, agent_id,
            sizeof(current_call_agent) - 1);
    current_call_agent[sizeof(current_call_agent) - 1] = '\0';

    LeaveCriticalSection(&ws_lock);

    if (g_context) {
        lws_cancel_service(g_context);
    }
}

void ws_send_hangup_status(const char *agent_id, const char *state, const char *number, const int causeCode, const char *rec)
{

    char temp[512];

    snprintf(temp, sizeof(temp),
            "{ \"event\": \"call\", \"agent-id\": \"%s\", \"state\": \"%s\", \"number\": \"%s\", \"causeCode\": \"%d\", \"recording\": \"%s\" }", agent_id, state, number, causeCode, rec);

    ws_enqueue(temp);

    LOG_NOTICE("WS queued: %s\n", temp);
    EnterCriticalSection(&ws_lock);

    strncpy(current_call_state, state, sizeof(current_call_state) - 1);
    current_call_state[sizeof(current_call_state) - 1] = '\0';

    strncpy(current_call_number, number, sizeof(current_call_number) - 1);
    current_call_number[sizeof(current_call_number) - 1] = '\0';

    strncpy(current_call_agent, agent_id, sizeof(current_call_agent) - 1);
    current_call_agent[sizeof(current_call_agent) - 1] = '\0';

    LeaveCriticalSection(&ws_lock);

    if (g_context) {

        lws_cancel_service(g_context);

       
    }
}

void ws_send_device_info(
        const char *device_name,
        const char *imei,
        const char *imsi,
        const char *operator_name)
{
    char temp[512];

    if (g_dev->dongle == DISCONNECTED)
    {
        snprintf(temp, sizeof(temp),
                "{"
                "\"event\":\"device_info\","
                "\"device\":\"%s\","
                "\"imei\":\"%s\","
                "\"imsi\":\"%s\","
                "\"operator\":\"%s\","
                "\"signal\":\"%s\""
                "}",
                "Disconnected",
                "N/A",
                "N/A",
                "N/A",
                "N/A");
    }
    else if ((g_dev->sim == DISCONNECTED) || (g_dev->sim == FAILURE))
    {
        snprintf(temp, sizeof(temp),
                "{"
                "\"event\":\"device_info\","
                "\"device\":\"%s\","
                "\"imei\":\"%s\","
                "\"imsi\":\"%s\","
                "\"operator\":\"%s\","
                "\"signal\":\"%s\""
                "}",
                (g_dev->sim == DISCONNECTED) ? "NO SIM" : "SIM FAILURE",
                "N/A",
                "N/A",
                "N/A",
                "N/A");
    }
    else 
    {
        snprintf(temp, sizeof(temp),
                "{"
                "\"event\":\"device_info\","
                "\"device\":\"%s\","
                "\"imei\":\"%s\","
                "\"imsi\":\"%s\","
                "\"operator\":\"%s\","
                "\"signal\":\"%d\""
                "}",
                "Connected",
                imei,
                imsi,
                operator_name,
                g_dev->rssi);
    }

    ws_enqueue(temp);

    LOG_NOTICE("WS queued: %s\n", temp);

    if (g_context) {

        lws_cancel_service(g_context);

       
    }
}

void ws_send_sound_devices(const char *devices)
{

    ws_enqueue(devices);

    LOG_NOTICE("WS queued: %s\n", devices);

    if (g_context) {

        lws_cancel_service(g_context);

     
    }
}

