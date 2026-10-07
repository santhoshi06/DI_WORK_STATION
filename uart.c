#include "uart.h"
#include "audio.h"
#include "websocket.h"
#include <stdio.h>
#include <string.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>

#include "debug.h"
#pragma comment(lib, "setupapi.lib")

extern volatile int keep_running;

static const char *cmds[CMD_COUNT] = {
    [CMD_ATE0]    = "ATE0",
    [CMD_CSCA]    = "AT+CSCA?",
    [CMD_CGMM]    = "AT+CGMM",
    [CMD_CGMR]    = "AT+CGMR",
    [CMD_CMEE]    = "AT+CMEE=2",
    [CMD_CGSN]    = "AT+CGSN",
    [CMD_CIMI]    = "AT+CIMI",
    [CMD_CPIN]    = "AT+CPIN?",
    [CMD_COPS]    = "AT+COPS?",
    [CMD_CREG_0]  = "AT+CREG=0",
    [CMD_CREG_Q]  = "AT+CREG?",
    [CMD_CLIP]    = "AT+CLIP=0",
    [CMD_CSCS]    = "AT+CSCS=\"GSM\"",
    [CMD_CSQ]     = "AT+CSQ",
    [CMD_QDAI]    = "AT+QDAI=1,1,0,4,0,0,1,1",
    [CMD_QAUDMOD] = "AT+QAUDMOD=1",
    [CMD_QCFG]    = "AT+QCFG=\"usbcfg\",0x2C7C,0x0125,1,1,1,1,1,0,1",
    [CMD_QPCMV0]  = "AT+QPCMV=0",
    [CMD_CLVL]    = "AT+CLVL=4",
    [CMD_QPCMV1]  = "AT+QPCMV=1,2",
    [CMD_CLCC]    = "AT+CLCC",
    [CMD_CEER]    = "AT+CEER",
    [CMD_ATD]     = "ATD",
    [CMD_CHLD]    = "AT+CHLD=2",
    [CMD_MUTE0]   = "AT+CMUT=0",
    [CMD_MUTE1]   = "AT+CMUT=1",
    [CMD_ATA]     = "ATA",
    [CMD_ATH]     = "ATH",
    [CMD_CFUN]    = "AT+CFUN=1,1"
};

const char * get_cmd(DI_SERIAL *dev) {
    return AT_COMMAND(dev->cmdi);
}


int configure_serial_port(HANDLE h)
{
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(h, &dcb)) {
        LOG_NOTICE("GetCommState failed: %lu\n", GetLastError());
        return 0;
    }

    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;

    if (!SetCommState(h, &dcb)) {
        LOG_NOTICE("SetCommState failed: %lu\n", GetLastError());
        return 0;
    }

    COMMTIMEOUTS timeouts = {0};

    timeouts.ReadIntervalTimeout = 50;        

    timeouts.ReadTotalTimeoutConstant = 1500; 
    timeouts.ReadTotalTimeoutMultiplier = 0;

    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    SetCommTimeouts(h, &timeouts);

    if (!SetCommTimeouts(h, &timeouts)) {
        LOG_NOTICE("SetCommTimeouts failed: %lu\n", GetLastError());
        return 0;
    }

    LOG_NOTICE("Serial port configured\n");
    return 1;
}

HANDLE open_ec20_at_port(DI_SERIAL *dev){
    HDEVINFO hDevInfo;
    SP_DEVINFO_DATA devInfo;
    DWORD i;

    hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

    for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); i++) {
        char name[256];

        if (SetupDiGetDeviceRegistryProperty(
                    hDevInfo, &devInfo, SPDRP_FRIENDLYNAME,
                    NULL, (PBYTE)name, sizeof(name), NULL)) {

            if (strstr(name, "Quectel") && strstr(name, "AT")) {

                LOG_NOTICE("Found: %s\n", name);

                strcpy(dev->friendly_name, name);

                char *com = strstr(name, "(COM");
                if (!com) continue;

                char port[32];
                sscanf(com, "(%[^)])", port);

                sprintf(dev->comPort, "\\\\.\\%s", port);
              
                HANDLE h = CreateFile(dev->comPort,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
                        0,
                        NULL);
                if (h == INVALID_HANDLE_VALUE) {
                   LOG_NOTICE("Open failed: %lu\n", GetLastError());
                    continue;
                }

                DCB dcbSerialParams = {0};
                dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

                GetCommState(h, &dcbSerialParams);

                dcbSerialParams.BaudRate = CBR_115200;
                dcbSerialParams.ByteSize = 8;
                dcbSerialParams.StopBits = ONESTOPBIT;
                dcbSerialParams.Parity   = NOPARITY;

                SetCommState(h, &dcbSerialParams);

                COMMTIMEOUTS timeouts = {0};
                timeouts.ReadIntervalTimeout = 50;
                timeouts.ReadTotalTimeoutConstant = 50;
                timeouts.ReadTotalTimeoutMultiplier = 10;
                timeouts.WriteTotalTimeoutConstant = 50;
                timeouts.WriteTotalTimeoutMultiplier = 10;

                SetCommTimeouts(h, &timeouts);

                LOG_NOTICE("Opened %s successfully\n", dev->comPort);
                SetupDiDestroyDeviceInfoList(hDevInfo);
                return h;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return INVALID_HANDLE_VALUE;
}

void close_ec20_port(DI_SERIAL *dev)
{
    if (dev->hSerial == INVALID_HANDLE_VALUE)
        return;

    LOG_NOTICE("Closing EC20 port: %s\n", dev->comPort);

    // Flush pending TX/RX
    PurgeComm(dev->hSerial,
              PURGE_RXCLEAR |
              PURGE_TXCLEAR |
              PURGE_RXABORT |
              PURGE_TXABORT);

    // Close COM handle
    CloseHandle(dev->hSerial);

    dev->hSerial = INVALID_HANDLE_VALUE;

    LOG_NOTICE("EC20 port closed\n");
}

int check_dongle(void)
{
    HDEVINFO hDevInfo;
    SP_DEVINFO_DATA devInfo;
    DWORD i;
    int found = 0;

    hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

    for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); i++) {
        char name[256];

        if (SetupDiGetDeviceRegistryProperty(
                    hDevInfo, &devInfo, SPDRP_FRIENDLYNAME,
                    NULL, (PBYTE)name, sizeof(name), NULL)) {

            if (strstr(name, "Quectel") && strstr(name, "AT")) {
                LOG_NOTICE("Found: %s\n", name);
                found = 1;
                break;
            }
        }
    }
    LOG_NOTICE("check_device: found: %d\n", found);
    if (found)
        return CONNECTED;
    else 
        return DISCONNECTED;
}

HANDLE reopen_com_port(DI_SERIAL *dev)
{
    LOG_NOTICE("Reopening COM port...\n");

    if (dev->hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(dev->hSerial);
        dev->hSerial = INVALID_HANDLE_VALUE;
    }

    Sleep(1000);

    HANDLE h = CreateFile(dev->comPort,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL, OPEN_EXISTING, 0, NULL);

    if (h == INVALID_HANDLE_VALUE) {
        LOG_NOTICE("Reopen failed: %lu\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    if (!configure_serial_port(h)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    LOG_NOTICE("Reopened successfully\n");

    dev->hSerial = h;
    return h;
}

void init_queue(ATQueue *q) {
    q->front = q->rear = 0;

    InitializeCriticalSection(&q->lock);

    q->event = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (q->event == NULL) {
        LOG_NOTICE("Failed to create event\n");
    }
}

int send_at_command(DI_SERIAL *dev, CommandIndex cmd) {

    dev->cmdi = cmd;  // store last command

    return send_at_string(dev, AT_COMMAND(cmd));
}

int send_at_string(DI_SERIAL *dev, const char *cmd) {
    DWORD written;

    LOG_PRIORITY("DEBUG LOGS [WRITE] >>> ENTER : %s\n", cmd);
    if (dev->cmdi >= CMD_COUNT) {
        LOG_PRIORITY("DEBUG LOGS [WRITE] <<< EXIT : %s\n", cmd);
        return -1;
    }
    char buf[256];

    switch (dev->cmdi) {
        case CMD_ATE0:
        case CMD_CSCA:
        case CMD_CGMM:
        case CMD_CGMR:
        case CMD_CMEE:
        case CMD_CGSN:
        case CMD_CIMI:
        case CMD_CPIN:
        case CMD_COPS:
        case CMD_CREG_0:
        case CMD_CREG_Q:
        case CMD_CLIP:
        case CMD_CSCS:
        case CMD_CSQ:
        case CMD_QDAI:
        case CMD_QAUDMOD:
        case CMD_QCFG:
        case CMD_QPCMV0:
        case CMD_QPCMV1:
        case CMD_CLVL:
        case CMD_CLCC:
        case CMD_CEER:
        case CMD_CHLD:
        case CMD_MUTE0:
        case CMD_MUTE1:
        case CMD_ATA:
        case CMD_ATH:
            sprintf(buf,"%s\r\n", cmd);
            LOG_PRIORITY("DEBUG LOGS [TX] %s", buf);
            if (!WriteFile(dev->hSerial, buf, strlen(buf), &written, NULL)) {
                printf("Write failed: %lu\n", GetLastError());
                LOG_PRIORITY("DEBUG LOGS [WRITE] <<< EXIT : %s\n", cmd);
                return -1;
            }
            LOG_PRIORITY("DEBUG LOGS [TX] Written %lu bytes\n", written);
            LOG_PRIORITY("DEBUG LOGS [WRITE] <<< EXIT : %s\n", cmd);
            return 0;
        case CMD_ATD:
            //start_audio_engine();
            sprintf(buf,"%s\r\n", cmd);
            if (!WriteFile(dev->hSerial, buf, strlen(buf), &written, NULL)) {
                LOG_NOTICE("Write failed: %lu\n", GetLastError());
                return -1;
            }
            LOG_PRIORITY("DEBUG LOGS [WRITE] <<< EXIT : %s\n", cmd);
            return 0;
        case CMD_CFUN:
            LOG_NOTICE("Writing Reset command: %s\n", cmd);
            sprintf(buf,"%s\r\n", cmd);
            if (!WriteFile(dev->hSerial, buf, strlen(buf), &written, NULL)) {
                LOG_NOTICE("Write failed: %lu\n", GetLastError());
                return -1;
            
            }
            LOG_PRIORITY("DEBUG LOGS [WRITE] <<< EXIT : %s\n", cmd);
            return 0;
        default:
            LOG_NOTICE("Write_CMD: Wrong index\n");
    }
    LOG_PRIORITY("DEBUG LOGS [WRITE] <<< EXIT : %s\n", cmd);
    return 0;
}

void enqueue_command(DI_SERIAL *dev, CommandIndex cmd) {
    ATQueue *q = &dev->at_queue;

    EnterCriticalSection(&q->lock);

    int next = (q->rear + 1) % QUEUE_SIZE;

    if (cmd == CMD_CLCC) {

        int i = q->front;

        while (i != q->rear) {

            if (q->queue[i].cmd == CMD_CLCC) {
                LeaveCriticalSection(&q->lock);
                return;
            }

            i = (i + 1) % QUEUE_SIZE;
        }
    }

    if (next != q->front) {
        q->queue[q->rear].cmd = cmd;
        q->queue[q->rear].is_custom = 0;
        q->rear = next;

        SetEvent(q->event);   // signal worker
    } else {
        LOG_NOTICE("Queue full\n");
        //keep_running = 0;
    }

    LeaveCriticalSection(&q->lock);
}

void enqueue_custom(DI_SERIAL *dev, const char *cmd) {
    ATQueue *q = &dev->at_queue;

    EnterCriticalSection(&q->lock);

    int next = (q->rear + 1) % QUEUE_SIZE;

    if (next != q->front) {
        strcpy(q->queue[q->rear].custom_cmd, cmd);
        q->queue[q->rear].is_custom = 1;
        q->rear = next;

        SetEvent(q->event);
    }

    LeaveCriticalSection(&q->lock);
}

int dequeue_command(DI_SERIAL *dev, ATCommand *out) {
    ATQueue *q = &dev->at_queue;

    EnterCriticalSection(&q->lock);

    if (q->front == q->rear) {
        LeaveCriticalSection(&q->lock);
        return 0;
    }

    *out = q->queue[q->front];
    q->front = (q->front + 1) % QUEUE_SIZE;

    LeaveCriticalSection(&q->lock);
    return 1;
}

void flush_queue(DI_SERIAL *dev)
{
    ATQueue *q = &dev->at_queue;

    EnterCriticalSection(&q->lock);

    q->front = 0;
    q->rear  = 0;

    // clear memory of queue entries
    memset(q->queue, 0, sizeof(q->queue));

    // Reset event state
    ResetEvent(q->event);

    LeaveCriticalSection(&q->lock);

    LOG_NOTICE("Queue flushed\n");
}

int parse_clcc(DI_SERIAL *dev, const char *res)
{
    int idx, dir, state, mode, mpty, type;
    char clcc_number[100] = {0};
    int ret = DIALING;

    char *dup = strdup(res);
    if (!dup)  return RES_IVLD;

    char *line = strtok(dup, "\r\n");
    int found = 0;
    while (line != NULL) {
        if (strstr(line, "+CLCC:") != NULL) {
            int matched = sscanf(line, "+CLCC: %d,%d,%d,%d,%d,\"%99[^\"]\",%d",
                    &idx, &dir, &state, &mode, &mpty, clcc_number, &type);

            found = 1;

            if (matched == 7 && strlen(clcc_number) > 0) {
                fprintf(stderr, "CLCC Parsed: idx=%d, dir=%d, state=%d, number=%s\n", idx, dir, state, clcc_number);

                if (state == 0)
                    ret = ON_CALL;
                else if (state == 1)
                    ret = ON_HOLD;
                else if (state == 2)
                    ret = DIALING;
                else if (state == 3)
                    ret = REMOTE_RING;
                else if (state == 4) {
                    ret = RING;
                    if (dev->call_state == IDLE) {
                        memset(dev->number, 0, sizeof(dev->number));
                        strcpy(dev->number, clcc_number);
                    }
                }

                if (dev->callID == 0)
                    dev->callID = idx;
                break;
            }else
                ret = IDLE;

        }

        line = strtok(NULL, "\r\n");
    }
    free(dup);

    return found ? ret : RES_IVLD;
}

int parse_ceer_response(const char *line) {
   // char *dup = strdup(line);
   // if (!dup) return -1;

    int cause_class = -1;
    int code = -1;

    const char *p = strstr(line, "+CEER:");
    if (sscanf(p, "+CEER: %d,%d", &cause_class, &code) != 2) {
       // free(dup);
        return -1;
    }

    fprintf(stderr,"[CEER] Cause class: %d, code: %d - ", cause_class, code);
    switch (code) {
        case 16: printf("Normal call clearing\n"); break;
        case 17: printf("User busy\n"); break;
        case 18: printf("No user responding\n"); break;
        case 19: printf("User alerting, no answer\n"); break;
        case 21: printf("Call rejected\n"); break;
        case 22: printf("Number changed\n"); break;
        case 25: printf("Call rejected\n"); break;
        case 27: printf("Destination out of order\n"); break;
        case 28: printf("Invalid number format\n"); break;
        case 29: printf("Facility rejected\n"); break;
        case 30: printf("Response to STATUS ENQUIRY\n"); break;
        case 31: printf("Normal, unspecified\n"); break;
        case 34: printf("No circuit/channel available\n"); break;
        case 38: printf("Network out of order\n"); break;
        case 41: printf("Temporary failure\n"); break;
        case 42: printf("Switching equipment congestion\n"); break;
        case 47: printf("Resource unavailable, unspecified\n"); break;
        default: printf("Unknown cause code\n"); break;
    }

    LOG_PRIORITY("[PARSE CEER] : OUT OF SWITCH CASE\n");
    //free(dup);
    LOG_PRIORITY("[PARSE CEER] : EXIT\n");
    return code;
}

void flush_uart(HANDLE h)
{
    if (h == INVALID_HANDLE_VALUE)
        return;

    if (!PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR)) {
       LOG_NOTICE("PurgeComm failed: %lu\n", GetLastError());
    }
}

char* remove_crlf(char *str) {
    str[strcspn(str, "\r\n")] = '\0';
    return str;
}

int read_at_response(DI_SERIAL *dev, CommandIndex cmd) {

    LOG_PRIORITY("DEBUG LOGS [READ] >>> ENTER : %s\n", AT_COMMAND(cmd));
    char buffer[1024] = {0};
    DWORD bytesRead;
    int total = 0;

    char *res;
    DWORD start = GetTickCount();

    while (GetTickCount() - start < 3000) {

        if (!ReadFile(dev->hSerial,
                    buffer + total,
                    sizeof(buffer) - total - 1,
                    &bytesRead,
                    NULL)) {

            LOG_NOTICE("Read error: %lu\n", GetLastError());
            return READ_ERROR;
        }

        if (bytesRead > 0) {
            dev->uart_last_alive = GetTickCount();

            total += bytesRead;
            buffer[total] = '\0';
            //printf("[RX] %s", buffer + total - bytesRead);

            if (strstr(buffer, "OK") ||
                    strstr(buffer, "ERROR")) {
                break;
            }
        }
    }
    LOG_PRIORITY("DEBUG LOGS [READ] >>> READ COMPLETE : %s\n", AT_COMMAND(cmd));
  //  printf("[READ] >>> READ COMPLETE\n");
    // printf("\n========== COMPLETE RESPONSE ==========\n");
    // printf("%s\n", buffer);
    // printf("=======================================\n");

    if (!(strstr(buffer, "OK") || strstr(buffer, "ERROR"))) {

        LOG_NOTICE("AT response timeout\n");
        return RES_TIMEOUT;
    }

    if (strstr(buffer, "NO CARRIER")) {
        LOG_NOTICE("Call ended (NO CARRIER)\n");
        return HANGUP;
    }

    switch (cmd) {
        case CMD_ATE0:
        case CMD_CSCA:
        case CMD_CGMM:
        case CMD_CGMR:
        case CMD_CMEE:
            {
                res = buffer;

                if (strstr(res, "OK")) {

                    while (*res == '\r' || *res == '\n')
                        res++;

                    res = remove_crlf(res);

                    LOG_NOTICE("Got Response 'OK', cmd: [%s], res: [%s]\n",
                            AT_COMMAND(cmd), res);

                    return RES_OK;
                }
                else if (strstr(res, "ERROR")) {

                    LOG_NOTICE("Got Response 'ERROR', cmd: [%s]\n",
                            AT_COMMAND(cmd));

                    return RES_ERROR;
                }

                LOG_PRIORITY("DEBUG LOGS [READ] <<< EXIT : %s\n", AT_COMMAND(cmd));
                return RES_IVLD;
            }

        case CMD_CGSN:
            
            res = buffer;

            if (strstr(res, "OK")) {
                char *p = strstr(res, "\r\n");

                if (p) {

                    p += 2;

                    sscanf(p, "%63s", dev->imei);
                }


                while (*res == '\r' || *res == '\n')
                    res++;

                res = remove_crlf(res);

                LOG_NOTICE("Got Response 'OK', cmd: [%s], res: [%s]\n",
                        AT_COMMAND(cmd), res);

                return RES_OK;
            }
        case CMD_CIMI:

            res = buffer;

            if (strstr(res, "OK")) {
                char *p = strstr(res, "\r\n");

                if (p) {

                    p += 2;

                    sscanf(p, "%63s", dev->imsi);

                    // printf("IMSI Saved: %s\n", dev->imsi);
                }

                while (*res == '\r' || *res == '\n')
                    res++;

                res = remove_crlf(res);

                LOG_NOTICE("Got Response 'OK', cmd: [%s], res: [%s]\n",
                        AT_COMMAND(cmd), res);

                return RES_OK;
            } else {
                LOG_NOTICE("Got Response 'ERROR', cmd: [%s]\n", AT_COMMAND(cmd));
                return RES_ERROR;
            }

        case CMD_COPS:

            res = buffer;

            if (strstr(res, "OK")) {

                char *p1 = strchr(res, '"');
                char *p2 = p1 ? strchr(p1 + 1, '"') : NULL;

                if (p1 && p2) {

                    int len = p2 - p1 - 1;

                    strncpy(dev->operator_name, p1 + 1, len);

                    dev->operator_name[len] = '\0';

                    // printf("Operator Saved: %s\n", dev->operator_name);
                }

                while (*res == '\r' || *res == '\n')
                    res++;

                res = remove_crlf(res);

                LOG_NOTICE("Got Response 'OK', cmd: [%s], res: [%s]\n",
                        AT_COMMAND(cmd), res);

                return RES_OK;
            } else {
                LOG_NOTICE("Got Response 'ERROR', cmd: [%s]\n", AT_COMMAND(cmd));
                return RES_ERROR;
            }
            

        case CMD_CPIN:

            res = buffer;

            if (strstr(res, "OK")) {
                if (strstr(res, "+CPIN: READY")) {
                    dev->sim = CONNECTED;
                } else if (strstr(res, "+CME ERROR: SIM failure")) {
                    dev->sim = FAILURE;
                } else {
                    dev->sim = DISCONNECTED;
                }

                while (*res == '\r' || *res == '\n')
                    res++;

                res = remove_crlf(res);

                LOG_NOTICE("Got Response 'OK', cmd: [%s], res: [%s]\n",
                        AT_COMMAND(cmd), res);

                return RES_OK;
            } else {
                LOG_NOTICE("Got Response 'ERROR', cmd: [%s]\n", AT_COMMAND(cmd));
                dev->sim = DISCONNECTED;
                return RES_ERROR;
            }

        case CMD_CSQ:

            res = buffer;

            int rssi = 0;
            if (strstr(res, "OK")) {
                res = strstr(res, "+CSQ:");
                if (sscanf(res, "+CSQ: %d", &rssi) == 1) {
                    //printf("RSSI Value = %d\n", rssi);
                    dev->rssi = rssi;
                } else {
                    LOG_NOTICE("Failed to parse CSQ\n");
                }

                return RES_OK;
            } else {
                LOG_NOTICE("Got Response 'ERROR', cmd: [%s]\n", AT_COMMAND(cmd));
                return RES_ERROR;
            }

        case CMD_CREG_0:
        case CMD_CREG_Q:
        case CMD_CLIP:
        case CMD_CSCS:
        case CMD_QDAI:
        case CMD_QAUDMOD:
        case CMD_QCFG:
        case CMD_QPCMV0:
        case CMD_QPCMV1:
        case CMD_CLVL:
        case CMD_ATA:
        case CMD_ATH:
        case CMD_CFUN:
            res = buffer;
            if (strstr(res, "OK")) {
                while (*res == '\r' || *res == '\n') res++;
                res = remove_crlf(res);
               LOG_NOTICE("Got Response 'OK', cmd: [%s], res: [%s]\n", AT_COMMAND(cmd), res);
                return RES_OK;
            } else if (strstr(res, "ERROR")) {
                res = remove_crlf(res);
                LOG_NOTICE("Got Response 'ERROR', cmd: [%s]\n", AT_COMMAND(cmd));
                return RES_ERROR;
            } else {
                res = remove_crlf(res);
                LOG_NOTICE("Got Unknown Response, cmd: [%s]\n", AT_COMMAND(cmd));
                return RES_IVLD;
            }

        case CMD_ATD:
            if (strstr(buffer, "OK")) {
                LOG_NOTICE("Got Response 'OK', cmd: [%s]\n", AT_COMMAND(cmd));
                return DIALING;
            } else if (strstr(buffer, "ERROR")) {
                LOG_NOTICE("Got Response 'ERROR', cmd: [%s]\n", AT_COMMAND(cmd));
                return HANGUP;
            } else {
                LOG_NOTICE("ATD invalid Received, buf: %s\n", buffer);
                return RES_IVLD;
            }

        case CMD_CHLD:
            if (strstr(buffer, "OK")) {
                return RES_OK;
            } else if (strstr(buffer, "ERROR")) {
                return RES_ERROR;
            } else {
                return RES_OK;
            }

        case CMD_MUTE0:
            if (strstr(buffer, "OK")) {
                dev->mute = 0;
                return RES_OK;
            } else if (strstr(buffer, "ERROR")) {
                return RES_ERROR;
            }
            return RES_IVLD;

        case CMD_MUTE1:
            if (strstr(buffer, "OK")) {
                dev->mute = 1;
                return RES_OK;
            } else if (strstr(buffer, "ERROR")) {
                return RES_ERROR;
            }
            return RES_IVLD;

        case CMD_CLCC:
            if (strstr(buffer, "+CLCC:")) {
                return parse_clcc(dev, buffer);
            } else {
                return IDLE;
            }

        case CMD_CEER:
            if (strstr(buffer, "+CEER:")) {
                int ceer_ret;
                ceer_ret = parse_ceer_response(buffer);
                LOG_NOTICE("Returned from CEER parse\n");
                return ceer_ret;
            } else {
                return -1;
            }

        default:
            fprintf(stderr, "Read_Res: Unknown command index %d\n", cmd);
  
            return -1;
    }
}

const char* state_to_string(STATE s) {
    switch (s) {
        case IDLE: return "IDLE";
        case RING: return "RING";
        case DIALING: return "DIALING";
        case ON_CALL: return "ON_CALL";
        case ON_HOLD: return "ON_HOLD";
        case HANGUP: return "HANGUP";
        default: return "UNKNOWN";
    }
}
void generate_filename(char *out, size_t size, const char *number)
{
    SYSTEMTIME t;
    GetLocalTime(&t);

    snprintf(out, size,
            "recordings/rec_%04d%02d%02d_%02d%02d%02d_%03d_%s.wav",
            t.wYear, t.wMonth, t.wDay,
            t.wHour, t.wMinute, t.wSecond,
            t.wMilliseconds,
            number ? number : "unknown");
}

static const char* get_filename_only(const char *path)
{
    const char *base = strrchr(path, '/');
    if (!base)
        base = strrchr(path, '\\');

    return base ? base + 1 : path;
}

static void disconnect_dongle(DI_SERIAL *dev)
{
    memset(dev->comPort, 0, sizeof(dev->comPort));
    memset(dev->friendly_name, 0, sizeof(dev->friendly_name));
    memset(dev->imei, 0, sizeof(dev->imei));
    memset(dev->imsi, 0, sizeof(dev->imsi));
    memset(dev->operator_name, 0, sizeof(dev->operator_name));
    dev->rssi = 99;
    dev->dongle = DISCONNECTED;

    flush_queue(dev);
    close_ec20_port(dev);
    ws_send_device_info(NULL, NULL, NULL, NULL);
}

DWORD WINAPI uart_worker_thread(LPVOID arg) {
    DI_SERIAL *dev = (DI_SERIAL*)arg;

    Recording *rec = &dev->call_recording;

    ATCommand cmd;

    int ret;
    int code = -1;

    dev->call_state = IDLE;

    dev->uart_thread_status = RUNNING;
    dev->uart_last_alive = GetTickCount();
    dev->uart_busy = 0;

    while (dev->di_running) {

        WaitForSingleObject(dev->at_queue.event, INFINITE);
        dev->uart_last_alive = GetTickCount();

        while (dequeue_command(dev, &cmd)) {

            if (cmd.is_custom) {
                send_at_string(dev, cmd.custom_cmd);
            } else {
                dev->uart_busy = 1;
                dev->uart_last_alive = GetTickCount();
                send_at_command(dev, cmd.cmd);
            }

            ret = read_at_response(dev, cmd.cmd);
            dev->uart_busy = 0;
            dev->uart_last_alive = GetTickCount();
            LOG_PRIORITY("[READ] <<< EXIT : %s\n", AT_COMMAND(cmd.cmd));
            // printf("[RESULT] %s -> %d\n",
            //         AT_COMMAND(cmd.cmd),
            //         ret);

            dev->uart_thread_status = RUNNING;

            // printf("Got return value: %d,cmd.cmd:%d\n", ret, cmd.cmd);

            if (cmd.cmd == CMD_CLCC) {
                switch (ret) {
                    case IDLE:
                       // LOG_NOTICE("Came to Idle State\n");
                        if (dev->call_state != IDLE) {

                            if (dev->call_state != HANGUP) {

                              //  LOG_NOTICE("Executing Idle State\n");

                                EnterCriticalSection(&rec->rec_lock);

                                if (rec->recording_status && rec->rec_fd) {
                                    int size = ftell(rec->rec_fd) - 44;

                                    fseek(rec->rec_fd, 0, SEEK_SET);
                                    write_wav_header(rec->rec_fd, size);

                                    fflush(rec->rec_fd);
                                    fclose(rec->rec_fd);

                                    rec->rec_fd = NULL;
                                    rec->recording_status = INACTIVE;

                                    LOG_NOTICE("Recording saved: recording.wav\n");
                                }

                                LeaveCriticalSection(&rec->rec_lock);

                                send_at_command(dev, CMD_CEER);
                                Sleep(100);
                                code = read_at_response(dev, CMD_CEER);
                                EnterCriticalSection(&rec->rec_lock);

                                const char *base = NULL;

                                if (rec->recording_file[0] != '\0')
                                    base = get_filename_only(rec->recording_file);

                                LeaveCriticalSection(&rec->rec_lock);

                                ws_send_hangup_status(dev->agent_id, "HANGUPCALL", dev->number, code, base);
                                memset(rec->recording_file, 0, sizeof(rec->recording_file));


                                dev->hangup_requested = 0;
                            }

                            stop_audio_engine();
                            dev->call_state = IDLE;
                            dev->callID = 0;
                            memset(dev->number, 0, sizeof(dev->number));

                            enqueue_command(dev, CMD_QPCMV0);
                        }
                       // LOG_NOTICE("Exit from Idle state\n");
                        break;

                    case RING:
                        if (dev->call_state != RING) {
                            start_audio_engine();
                            LOG_NOTICE("System State: Ring, Number: %s\n\n", dev->number);
                            dev->call_state = RING;
                            strcpy(dev->agent_id, "ring");
                            enqueue_command(dev, CMD_QPCMV1);
                            ws_send_status(dev->agent_id, "INCOMING", dev->number);
                        }
                        break;

                    case DIALING:
                        if (dev->call_state != DIALING) {
                            LOG_NOTICE("System State: Dialing, Number: %s\n\n", dev->number);
                            dev->call_state = DIALING;
                            enqueue_command(dev, CMD_QPCMV1);
                            ws_send_status(dev->agent_id, "DIALING", dev->number);
                        }
                        break;

                    case REMOTE_RING:       // When Callee starts ringing, open audio engine to listen Ring
                        if (dev->call_state != REMOTE_RING) {
                            LOG_NOTICE("System State: Remote Ringing, Number: %s\n\n", dev->number);
                            dev->call_state = REMOTE_RING;
                            Sleep(1000);
                            start_audio_engine();
                        }
                        break;

                    case ON_CALL:
                        if (dev->call_state != ON_CALL) {
                            dev->call_state = ON_CALL;
                            LOG_NOTICE("System State: Talking, Number: %s\n\n", dev->number);

                            EnterCriticalSection(&rec->rec_lock);

                            char filename[128];

                            generate_filename(filename, sizeof(filename), dev->number);

                            strcpy(rec->recording_file, filename);

                            rec->rec_fd = fopen(filename, "wb");

                            char empty[44] = {0};
                            fwrite(empty, 1, 44, rec->rec_fd);

                            rec->recording_status = ACTIVE;

                            LOG_NOTICE("Recording started...\n");

                            LeaveCriticalSection(&rec->rec_lock);

                            const char *base = get_filename_only(rec->recording_file);

                            ws_send_onCall_status(dev->agent_id, "ONCALL", dev->number, base);
                        }
                        break;

                    case HANGUP:
			            LOG_NOTICE("came to Hangup state\n");
                        if (dev->call_state != HANGUP) {
                            dev->call_state = HANGUP;
			                LOG_NOTICE("executing Hangup state\n");

                            EnterCriticalSection(&rec->rec_lock);

                            if (rec->recording_status && rec->rec_fd) {

                                int size = ftell(rec->rec_fd) - 44;

                                fseek(rec->rec_fd, 0, SEEK_SET);
                                write_wav_header(rec->rec_fd, size);

                                fflush(rec->rec_fd);   
                                fclose(rec->rec_fd);

                                rec->rec_fd = NULL;
                                rec->recording_status = 0;

                                LOG_NOTICE("Recording saved: recording.wav\n");
                            }

                            LeaveCriticalSection(&rec->rec_lock);

                            send_at_command(dev, CMD_CEER);
                            Sleep(100);
                            code = read_at_response(dev, CMD_CEER);
                                                            
                             const char *base = NULL;

                            if (rec->recording_file[0] != '\0')
                                base = get_filename_only(rec->recording_file);

                            ws_send_hangup_status(dev->agent_id, "HANGUPCALL", dev->number, code, base);
                            memset(rec->recording_file, 0, sizeof(rec->recording_file));
                        }
                        LOG_NOTICE("Exit from Hangup state\n");
                        break;

                    case READ_ERROR:
                    case RES_TIMEOUT:
                        if (check_dongle() == DISCONNECTED)
                        {
                            disconnect_dongle(dev);
                        }
                        break;
                }
            } else {
                switch (ret) {			
                    case READ_ERROR:
                    case RES_TIMEOUT:
                        if (check_dongle() == DISCONNECTED)
                        {
                            disconnect_dongle(dev);
                        }
                        break;
                }
            }
        }
    }

    LOG_NOTICE("UART thread Exited\n");

    return 0;
}
