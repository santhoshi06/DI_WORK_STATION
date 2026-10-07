// audio.h

#ifndef AUDIO_H
#define AUDIO_H

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

#include "uart.h"

#pragma comment(lib, "winmm.lib")

#define BUFFER_SIZE 2048
#define NUM_BUFFERS 4

typedef struct {

    HWAVEIN hWaveInEC20;
    HWAVEIN hWaveInHead;

    HWAVEOUT hWaveOutEC20;
    HWAVEOUT hWaveOutHead;

    WAVEHDR *ec20_hdrs;
    WAVEHDR *head_hdrs;

    WAVEFORMATEX wfx;

    int running;

    int ec20_in;
    int ec20_out;

    int head_in;
    int head_out;

} AUDIO_ENGINE;

extern AUDIO_ENGINE audio;

int init_audio_devices(void);

void init_recording(DI_SERIAL *dev);

int start_audio_engine(void);

void stop_audio_engine(void);

void write_wav_header(FILE *f, int data_size);

void get_audio_devices(void);

void update_audio_devices(int in, int out);

#endif
