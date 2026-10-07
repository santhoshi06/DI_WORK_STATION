#ifndef UART_H
#define UART_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ACTIVE      1
#define INACTIVE    0

#define CONNECTED       0
#define DISCONNECTED    1
#define FAILURE         2

#define RUNNING     0
#define PAUSE       1
#define BLOCKED     2

#define QUEUE_SIZE 32

#define AT_COMMAND(index) cmds[index]

typedef enum {
    CMD_ATE0=0,
    CMD_CSCA,
    CMD_CGMM,
    CMD_CGMR,
    CMD_CMEE,
    CMD_CGSN,
    CMD_CIMI,
    CMD_CPIN,
    CMD_COPS,
    CMD_CREG_0,
    CMD_CREG_Q,
    CMD_CLIP,
    CMD_CSCS,
    CMD_CSQ,
    CMD_QDAI,
    CMD_QAUDMOD,
    CMD_QCFG,
    CMD_QPCMV0,
    CMD_CLVL,
    CMD_QPCMV1,
    CMD_CLCC,
    CMD_CEER,
    CMD_ATD,
    CMD_CHLD,
    CMD_MUTE0,
    CMD_MUTE1,
    CMD_ATA,
    CMD_ATH,
    CMD_CFUN,
    CMD_COUNT
} CommandIndex;

typedef enum {
    RES_OK = 0,
    IDLE,
    RING,
    DIALING,
    REMOTE_RING,
    ON_CALL,
    ON_HOLD,
    HANGUP,
    READ_ERROR,
    RES_IVLD,
    RES_TIMEOUT,
    RES_ERROR,
    COM_STUCK
} STATE;

typedef struct {
    CommandIndex cmd;
    char custom_cmd[64];   // for ATD etc
    int is_custom;
} ATCommand;

typedef struct {
    ATCommand queue[QUEUE_SIZE];
    int front;
    int rear;

    CRITICAL_SECTION lock;
    HANDLE event;   // signal when new command arrives
} ATQueue;

typedef struct {
    char recording_file[128];
    FILE *rec_fd;
    int recording_status;

    CRITICAL_SECTION rec_lock;
} Recording;

typedef struct {
    HANDLE hSerial;          // serial handle

    HANDLE ws_thread;

    HANDLE uart_thread;

    HANDLE uart_monitor;

    volatile int dongle;
    volatile int sim;

    char comPort[36];        // COM port name
    char friendly_name[256];
    char imei[64];
    char imsi[64];
    char operator_name[64];
    int rssi;
    
    char agent_id[20];       // agent name
    char number[20];         // dial number

    CommandIndex cmdi;       // last command index
    int callID;
    STATE call_state;
    bool mute;
    HANDLE thread_handle;    // thread handle

    volatile int di_running;

    ATQueue at_queue;

    volatile int uart_thread_status;
    volatile DWORD uart_last_alive;
    volatile int uart_busy;

    Recording call_recording;

    int hangup_requested;

} DI_SERIAL;


// APIs
int check_dongle(void);
HANDLE open_ec20_at_port(DI_SERIAL *dev);
void close_ec20_port(DI_SERIAL *dev);
const char * get_cmd(DI_SERIAL *dev);
int send_at_command(DI_SERIAL *dev, CommandIndex cmd);
int send_at_string(DI_SERIAL *dev, const char *cmd);
int read_at_response(DI_SERIAL *dev, CommandIndex cmd);
const char* state_to_string(STATE s);

void init_queue(ATQueue *q);
void enqueue_custom(DI_SERIAL *dev, const char *cmd);
void enqueue_command(DI_SERIAL *dev, CommandIndex cmd);
int dequeue_command(DI_SERIAL *dev, ATCommand *out);
void flush_queue(DI_SERIAL *dev);
DWORD WINAPI uart_worker_thread(LPVOID arg);

#endif
