#include "audio.h"
#include "uart.h"
#include "websocket.h"
#include <mmsystem.h>
#include <stdio.h>
#include "debug.h"

#pragma comment(lib, "winmm.lib")

volatile int keep_running = 1;

BOOL WINAPI console_handler(DWORD signal) {
    switch (signal) {

        case CTRL_C_EVENT:
            LOG_NOTICE("\nCtrl+C received\n");
            keep_running = 0;
            return TRUE;

        case CTRL_CLOSE_EVENT:
            LOG_NOTICE("\nConsole closing\n");
            keep_running = 0;
            return TRUE;

        case CTRL_BREAK_EVENT:
            LOG_NOTICE("\nCtrl+Break received\n");
            keep_running = 0;
            return TRUE;

        default:
            return FALSE;
    }
}

void init_dongle(DI_SERIAL *dev)
{
    for (int i = 0; i < (CMD_COUNT - 10); ++i) {
        send_at_command(dev, i);
        read_at_response(dev, dev->cmdi);
        Sleep(10);
    }

}

#if 0
DWORD WINAPI stress_test_thread(LPVOID arg)
{
	DI_SERIAL *dev = (DI_SERIAL *)arg;
	int count = 1;

	while (dev->di_running)
	{
		printf("\n====================================\n");
		printf("STRESS TEST CALL : %d\n", count++);
		printf("====================================\n");

		printf("dongle=%d sim=%d call_state=%d\n",
				dev->dongle,
				dev->sim,
				dev->call_state);



		if (dev->dongle != CONNECTED)
		{
			printf("Skipping: Dongle disconnected\n");
			Sleep(2000);
			continue;
		}

		if (dev->sim != CONNECTED)
		{
			printf("Skipping: SIM disconnected\n");
			Sleep(2000);
			continue;
		}

		if (dev->call_state != IDLE)
		{
			printf("Skipping: call_state=%d\n", dev->call_state);
			Sleep(1000);
			continue;
		}

		strcpy(dev->agent_id, "stress");
		strcpy(dev->number, "8121127068");

		char dialcmd[64];

		sprintf(dialcmd, "ATD%s;", dev->number);

		dev->cmdi = CMD_ATD;

		enqueue_custom(dev, dialcmd);

		printf("Dial queued\n");

		Sleep(25000);

		if (dev->call_state != IDLE)
		{
			enqueue_command(dev, CMD_ATH);
			printf("Hangup queued\n");
		}

		Sleep(5000);
	}

	return 0;
}
#endif

DWORD WINAPI uart_monitor_thread(LPVOID arg) {
    DI_SERIAL *dev = (DI_SERIAL*)arg;

    while(dev->di_running) {

        DWORD now = GetTickCount();

        if (!dev->uart_busy)
        {
            Sleep(1000);
            continue;
        }

        if ((now - dev->uart_last_alive) < 8000)
        {
            Sleep(1000);
            continue;
        }
        if (dev->uart_busy){

            LOG_PRIORITY("DEBUG LOGS ******* Uart thread Blocked ******\n");

            dev->uart_thread_status = BLOCKED;

            flush_queue(dev);

            TerminateThread(dev->uart_thread, 0);

            CloseHandle(dev->uart_thread);

            dev->uart_thread =NULL;

            LOG_PRIORITY("DEBUG LOGS ****** Thread terminated *********\n");

            close_ec20_port(dev);

            LOG_PRIORITY("DEBUG LOGS ****** Port closed *********\n");

            Sleep(100);

            dev->hSerial = open_ec20_at_port(dev);

            LOG_PRIORITY("DEBUG LOGS ******* Port Reopened ********\n");

            if (dev->hSerial == INVALID_HANDLE_VALUE) {

                dev->dongle = DISCONNECTED;

                LOG_PRIORITY("DEBUG LOGS Dongle not found!\n");

            }else

                dev->dongle = CONNECTED;

            Sleep(2000);

            dev->uart_thread = CreateThread(
                    NULL,
                    0,
                    uart_worker_thread,
                    dev,
                    0,
                    NULL
            );

            LOG_PRIORITY("DEBUG LOGS ******** Uart Thread Created *********\n");

            if (dev->uart_thread == NULL) {

              LOG_PRIORITY("DEBUG LOGS Failed to create UART thread\n");

                keep_running = 0;
            }

        } else {

            LOG_PRIORITY("DEBUG LOGS UART thread is OK\n");
        }
    }
    LOG_PRIORITY("DEBUG LOGS UART Monitor Thread Existed\n");
}

int main() {

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    SetConsoleCtrlHandler(console_handler, TRUE);

    DI_SERIAL dev = {0};

    Recording *rec = &dev.call_recording;

    init_recording(&dev);

    init_queue(&dev.at_queue);

    dev.hSerial = open_ec20_at_port(&dev);
    if (dev.hSerial == INVALID_HANDLE_VALUE) {
        dev.dongle = DISCONNECTED;
        LOG_NOTICE("Dongle not found!\n");
        //return -1;
    }else
        dev.dongle = CONNECTED;

    dev.ws_thread = NULL;

    dev.uart_thread = NULL;

    dev.uart_monitor = NULL;

    dev.di_running = 1;

    dev.ws_thread = CreateThread(
            NULL,
            0,
            websocket_thread,
            &dev,
            0,
            NULL
            );

    if (dev.ws_thread == NULL) {

        LOG_NOTICE("Failed to create WebSocket thread\n");

        goto cleanup;
    }

    dev.uart_thread = CreateThread(
            NULL,
            0,
            uart_worker_thread,
            &dev,
            0,
            NULL
            );

    if (dev.uart_thread == NULL) {
       LOG_NOTICE("Failed to create UART thread\n");
        goto cleanup;
    }

    if (dev.dongle == CONNECTED)
    {
        init_dongle(&dev);
        if (dev.sim == CONNECTED)
            LOG_NOTICE("Span Ready to Make Call\n\n");

/*	CreateThread(
			NULL,
			0,
			stress_test_thread,
			&dev,
			0,
			NULL);*/

    }

    dev.uart_monitor = CreateThread(
            NULL,
            0,
            uart_monitor_thread,
            &dev,
            0,
            NULL
            );

    if (dev.uart_monitor == NULL) {
        LOG_NOTICE("Failed to create UART monitor thread\n");
        goto cleanup;
    }

    while (keep_running) {
        if (dev.dongle == CONNECTED)
        {
            if (dev.uart_thread_status == BLOCKED) {
                Sleep(2000);
                continue;
            }
            if (dev.sim == CONNECTED && dev.call_state == IDLE)
            {
                enqueue_command(&dev, CMD_CSQ);
                Sleep(100);
            }
            enqueue_command(&dev, CMD_CLCC);
        }
        else {
            dev.hSerial = open_ec20_at_port(&dev);
            if (dev.hSerial != INVALID_HANDLE_VALUE)
            {
                Sleep(5000);
                dev.dongle = CONNECTED;
                init_dongle(&dev);
                LOG_NOTICE("Span Ready to Make Call\n\n");
                ws_send_device_info(
                        dev.friendly_name,
                        dev.imei,
                        dev.imsi,
                        dev.operator_name);
            }
        }
        Sleep(1000);
    }

    LOG_NOTICE("Stopping WS thread...\n");

cleanup:
    dev.di_running = 0; 

    if (g_context) {
        lws_cancel_service(g_context);
    }

    SetEvent(dev.at_queue.event);

    if (dev.ws_thread)
        WaitForSingleObject(dev.ws_thread, INFINITE);
    if (dev.uart_thread)
        WaitForSingleObject(dev.uart_thread, INFINITE);
    if (dev.uart_monitor)
        WaitForSingleObject(dev.uart_monitor, INFINITE);

    CloseHandle(dev.ws_thread);
    CloseHandle(dev.uart_thread);
    CloseHandle(dev.uart_monitor);

    DeleteCriticalSection(&dev.at_queue.lock);
    CloseHandle(dev.at_queue.event);

    LOG_NOTICE("Audio thread Existed\n");
    EnterCriticalSection(&rec->rec_lock);

    if (rec->recording_status && rec->rec_fd) {
        int size = ftell(rec->rec_fd) - 44;
        fseek(rec->rec_fd, 0, SEEK_SET);
        write_wav_header(rec->rec_fd, size);

        fclose(rec->rec_fd);
        rec->rec_fd = NULL;
        rec->recording_status = INACTIVE;

        LOG_NOTICE("Recording safely closed on exit\n");
    }

    LeaveCriticalSection(&rec->rec_lock);
    DeleteCriticalSection(&rec->rec_lock);

  LOG_NOTICE("============= Di Program Exited Successfully =============\n");
    exit(0);
}
