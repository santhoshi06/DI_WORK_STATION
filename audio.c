// audio.c

#include "audio.h"
#include "websocket.h"
#include <string.h>
#include <stdlib.h>
#include "debug.h"

//AUDIO_ENGINE audio = {0};

AUDIO_ENGINE audio = {
    .head_in = -1,
    .head_out = -1
};

Recording *rec = NULL;

char ec20_mix_buf[BUFFER_SIZE];
char head_mix_buf[BUFFER_SIZE];

int ec20_ready = 0;
int head_ready = 0;

// ======================================================
// Find Audio Devices
// ======================================================

int init_audio_devices()
{
    int i;

    audio.ec20_in  = -1;
    audio.ec20_out = -1;

   // audio.head_in  = 1;
   // audio.head_out = 1;

    WAVEINCAPS  wic;
    WAVEOUTCAPS woc;

    LOG_NOTICE("Searching audio devices...\n\n");

    // -----------------------------
    // INPUT DEVICES
    // -----------------------------
    int in_count = waveInGetNumDevs();

    for (i = 0; i < in_count; i++) {

        waveInGetDevCaps(i, &wic, sizeof(wic));

        LOG_NOTICE("INPUT %d : %s\n", i, wic.szPname);

        // EC20 MIC
        if (strstr(wic.szPname, "AC Interface")) {
            audio.ec20_in = i;
        }
        else {
            // First non-EC20 device = headset mic
            if (audio.head_in == -1) {
                audio.head_in = i;
            }
        }
    }

    // -----------------------------
    // OUTPUT DEVICES
    // -----------------------------
    int out_count = waveOutGetNumDevs();

    for (i = 0; i < out_count; i++) {

        waveOutGetDevCaps(i, &woc, sizeof(woc));

        LOG_NOTICE("OUTPUT %d : %s\n", i, woc.szPname);

        // EC20 Speaker
        if (strstr(woc.szPname, "AC Interface")) {
            audio.ec20_out = i;
        }
        else {
            // First non-EC20 device = headset speaker
            if (audio.head_out == -1) {
                audio.head_out = i;
            }
        }
    }

    LOG_NOTICE("\n");

    LOG_NOTICE("EC20_IN   : %d\n", audio.ec20_in);
    LOG_NOTICE("EC20_OUT  : %d\n", audio.ec20_out);

    LOG_NOTICE("HEAD_IN   : %d\n", audio.head_in);
    LOG_NOTICE("HEAD_OUT  : %d\n", audio.head_out);

    if (audio.ec20_in < 0 ||
        audio.ec20_out < 0 ||
        audio.head_in < 0 ||
        audio.head_out < 0)
    {
        LOG_NOTICE("Failed to detect audio devices\n");
        return -1;
    }

    return 0;
}

void get_audio_devices(void)
{
    int i;
             
    char devices[5000];

    WAVEINCAPS  wic;
    WAVEOUTCAPS woc;

    devices[0] = '\0';

    strcat(devices, "{");
    strcat(devices, "\"event\":\"sound_devices\",");
    strcat(devices, "\"input_devices\":[");

    // -----------------------------
    // INPUT DEVICES
    // -----------------------------
    int in_count = waveInGetNumDevs();

    // if (5 < in_count)
    //     in_count = 5;

    int first = 1;

    for (i = 0; i < in_count; i++) {

        waveInGetDevCaps(i, &wic, sizeof(wic));

       // LOG_NOTICE("INPUT %d : %s\n", i, wic.szPname);

        // Skip AC Interface
        if (!strstr(wic.szPname, "AC Interface")) {

            char temp[512];

            if (!first)
                strcat(devices, ",");

            snprintf(temp, sizeof(temp),
                     "{\"id\":%d,\"name\":\"%s\"}",
                     i,
                     wic.szPname);

            strcat(devices, temp);

            first = 0;
        }
    }

    strcat(devices, "],");

    // -----------------------------
    // OUTPUT DEVICES
    // -----------------------------
    strcat(devices, "\"output_devices\":[");

    int out_count = waveOutGetNumDevs();

    // if (5 < out_count)
    //     out_count = 5;

    first = 1;

    for (i = 0; i < out_count; i++) {

        waveOutGetDevCaps(i, &woc, sizeof(woc));

       // LOG_NOTICE("OUTPUT %d : %s\n", i, woc.szPname);

        // Skip AC Interface
        if (!strstr(woc.szPname, "AC Interface")) {

            char temp[512];

            if (!first)
                strcat(devices, ",");

            snprintf(temp, sizeof(temp),
                     "{\"id\":%d,\"name\":\"%s\"}",
                     i,
                     woc.szPname);

            strcat(devices, temp);

            first = 0;
        }
    }

    strcat(devices, "]}");

    //printf("\nSound Devices: %s\n\n", devices);

    ws_send_sound_devices(devices);
}

void update_audio_devices(int in, int out)
{
    audio.head_in  = in;
    audio.head_out = out;

    LOG_NOTICE("Updated headphone index input: %d, output: %d\n", in, out);
}

void init_recording(DI_SERIAL *dev)
{
    rec = &dev->call_recording;

    CreateDirectory("recordings", NULL);

    InitializeCriticalSection(&rec->rec_lock);

}

// ======================================================
// Playback Helper
// ======================================================

void play_audio(HWAVEOUT outDev, char *data, DWORD size)
{
    if (!audio.running)
        return;

    char *copy = (char*)malloc(size);

    if (!copy)
        return;

    memcpy(copy, data, size);

    WAVEHDR *hdr = (WAVEHDR*)malloc(sizeof(WAVEHDR));

    if (!hdr) {
        free(copy);
        return;
    }

    memset(hdr, 0, sizeof(WAVEHDR));

    hdr->lpData = copy;
    hdr->dwBufferLength = size;

    waveOutPrepareHeader(outDev, hdr, sizeof(WAVEHDR));

    waveOutWrite(outDev, hdr, sizeof(WAVEHDR));
}


// ======================================================
// Output Callback
// ======================================================

void CALLBACK waveOutProc(HWAVEOUT hwo,
                          UINT msg,
                          DWORD_PTR inst,
                          DWORD_PTR param1,
                          DWORD_PTR param2)
{
    if (msg == WOM_DONE) {

        WAVEHDR *hdr = (WAVEHDR*)param1;

        waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));

        free(hdr->lpData);

        free(hdr);
    }
}


// ======================================================
// EC20 -> Headphone
// ======================================================

void CALLBACK ec20_callback(HWAVEIN hwi,
        UINT msg,
        DWORD_PTR inst,
        DWORD_PTR param1,
        DWORD_PTR param2)
{
//    LOG_NOTICE("EC20 CALLBACK ENTER msg=%u", msg);

    if (!audio.running)
    {
  //      LOG_NOTICE("EC20 CALLBACK EXIT (audio.running=0)");
        return;
    }

    if (msg == WIM_DATA) {

        WAVEHDR *hdr = (WAVEHDR*)param1;

        if (hdr->dwBytesRecorded > 0) {

           // LOG_NOTICE("Before play_audio");
            play_audio(audio.hWaveOutHead,
                    hdr->lpData,
                    hdr->dwBytesRecorded);
           // LOG_NOTICE("After play_audio");

            EnterCriticalSection(&rec->rec_lock);
            if (rec->recording_status && rec->rec_fd) {

                memcpy(ec20_mix_buf, hdr->lpData, hdr->dwBytesRecorded);
                ec20_ready = hdr->dwBytesRecorded;
                if (head_ready > 0) {

                    int samples = ec20_ready / 2; // 16-bit audio

                    short *ec20 = (short*)ec20_mix_buf;
                    short *head = (short*)head_mix_buf;

                    for (int i = 0; i < samples; i++) {
                        int mixed = ec20[i] + head[i];

                        if (mixed > 32767) mixed = 32767;
                        if (mixed < -32768) mixed = -32768;

                        short out = (short)mixed;
                        fwrite(&out, sizeof(short), 1, rec->rec_fd);
                    }

                    ec20_ready = 0;
                    head_ready = 0;
                }
            }
            LeaveCriticalSection(&rec->rec_lock);

        }

      //  LOG_NOTICE("Before waveInAddBuffer EC20");

        MMRESULT ret = waveInAddBuffer(audio.hWaveInEC20,
                hdr,
                sizeof(WAVEHDR));

        //LOG_NOTICE("After waveInAddBuffer EC20 ret=%u", ret);
    }
//    LOG_NOTICE("EC20 CALLBACK EXIT");
}


// ======================================================
// Headphone -> EC20
// ======================================================

void CALLBACK head_callback(HWAVEIN hwi,
        UINT msg,
        DWORD_PTR inst,
        DWORD_PTR param1,
        DWORD_PTR param2)
{
  //  LOG_NOTICE("HEAD CALLBACK ENTER msg=%u\n", msg);
    if (!audio.running)
    {
    //    LOG_NOTICE("HEAD CALLBACK EXIT: audio.running = 0\n");
        return;
    }

    if (msg == WIM_DATA) {

        WAVEHDR *hdr = (WAVEHDR*)param1;
      //  LOG_NOTICE("HEAD CALLBACK: Bytes Recorded = %lu\n", hdr->dwBytesRecorded);


        if (hdr->dwBytesRecorded > 0) {
        //    LOG_NOTICE("Before play_audio()");

            play_audio(audio.hWaveOutEC20,
                    hdr->lpData,
                    hdr->dwBytesRecorded);
          //  LOG_NOTICE("After play_audio()");

           // LOG_NOTICE("Before EnterCriticalSection");

            EnterCriticalSection(&rec->rec_lock);
            if (rec->recording_status && rec->rec_fd) {

                memcpy(head_mix_buf, hdr->lpData, hdr->dwBytesRecorded);
                head_ready = hdr->dwBytesRecorded;

            }
            LeaveCriticalSection(&rec->rec_lock);
           // LOG_NOTICE("After LeaveCriticalSection");
        }

        //  LOG_NOTICE("Before waveInAddBuffer");

        MMRESULT ret = waveInAddBuffer(audio.hWaveInHead,
                hdr,
                sizeof(WAVEHDR));

      //  LOG_NOTICE("After waveInAddBuffer ret=%u", ret);
    }
   // LOG_NOTICE("HEAD CALLBACK EXIT");
}


// ======================================================
// Start Audio Engine
// ======================================================

int start_audio_engine(void)
{
    int i;
    MMRESULT ret;

    if (audio.running) {
     //   LOG_NOTICE("Audio already running\n");
        return 0;
    }

    LOG_NOTICE("Starting audio engine...\n");

    // Detect devices
    if (init_audio_devices() < 0) {
        return -1;
    }

    // Audio Format
    audio.wfx.wFormatTag = WAVE_FORMAT_PCM;
    audio.wfx.nChannels = 1;
    audio.wfx.nSamplesPerSec = 8000;
    audio.wfx.wBitsPerSample = 16;
    audio.wfx.nBlockAlign = 2;
    audio.wfx.nAvgBytesPerSec = 16000;
    audio.wfx.cbSize = 0;


    // --------------------------------------------------
    // OPEN OUTPUTS
    // --------------------------------------------------


    LOG_PRIORITY("DEBUG LOGS Before waveOutOpen EC20\n");
    ret = waveOutOpen(&audio.hWaveOutEC20,
            audio.ec20_out,
            &audio.wfx,
            (DWORD_PTR)waveOutProc,
            0,
            CALLBACK_FUNCTION);

    LOG_PRIORITY("DEBUG LOGS After waveOutOpen EC20 ret=%u\n", ret);
    if (ret != MMSYSERR_NOERROR)
    {
      LOG_NOTICE("Failed opening EC20 output\n");
        return -1;
    }


    LOG_PRIORITY("DEBUG LOGS Before waveOutOpen Head\n");
    ret = waveOutOpen(&audio.hWaveOutHead,
            audio.head_out,
            &audio.wfx,
            (DWORD_PTR)waveOutProc,
            0,
            CALLBACK_FUNCTION);

    LOG_PRIORITY("DEBUG LOGS After waveOutOpen Head ret=%u\n", ret);
    if (ret != MMSYSERR_NOERROR)
    {
        LOG_NOTICE("Failed opening Headphone output\n");

        waveOutClose(audio.hWaveOutEC20);

        return -1;
    }

    // --------------------------------------------------
    // OPEN INPUTS
    // --------------------------------------------------

   LOG_PRIORITY("DEBUG LOGS Before waveInOpen EC20\n");
    ret = waveInOpen(&audio.hWaveInEC20,
            audio.ec20_in,
            &audio.wfx,
            (DWORD_PTR)ec20_callback,
            0,
            CALLBACK_FUNCTION);
    LOG_PRIORITY("DEBUG LOGS After waveInOpen EC20 ret=%u\n", ret);
    if (ret != MMSYSERR_NOERROR)
    {
        LOG_NOTICE("Failed opening EC20 input\n");

        waveOutClose(audio.hWaveOutEC20);
        waveOutClose(audio.hWaveOutHead);

        return -1;
    }


    LOG_PRIORITY("DEBUG LOGS Before waveInOpen Head\n");

    ret = waveInOpen(&audio.hWaveInHead,
            audio.head_in,
            &audio.wfx,
            (DWORD_PTR)head_callback,
            0,
            CALLBACK_FUNCTION);

    LOG_PRIORITY("DEBUG LOGS After waveInOpen Head ret=%u\n", ret);

    if (ret != MMSYSERR_NOERROR)
    {
        LOG_NOTICE("Failed opening Headphone input\n");

        waveInClose(audio.hWaveInEC20);

        waveOutClose(audio.hWaveOutEC20);
        waveOutClose(audio.hWaveOutHead);

        return -1;
    }



    // --------------------------------------------------
    // ALLOCATE BUFFERS
    // --------------------------------------------------

    audio.ec20_hdrs =
        (WAVEHDR*)calloc(NUM_BUFFERS, sizeof(WAVEHDR));

    audio.head_hdrs =
        (WAVEHDR*)calloc(NUM_BUFFERS, sizeof(WAVEHDR));

    if (!audio.ec20_hdrs || !audio.head_hdrs)
    {
        LOG_NOTICE("Buffer allocation failed\n");

        if (audio.ec20_hdrs)
            free(audio.ec20_hdrs);

        if (audio.head_hdrs)
            free(audio.head_hdrs);

        waveInClose(audio.hWaveInEC20);
        waveInClose(audio.hWaveInHead);

        waveOutClose(audio.hWaveOutEC20);
        waveOutClose(audio.hWaveOutHead);

        return -1;
    }
    // --------------------------------------------------
    // PREPARE + QUEUE BUFFERS
    // --------------------------------------------------

    for (i = 0; i < NUM_BUFFERS; i++) {

        // EC20 input buffers
        audio.ec20_hdrs[i].lpData =
            (char*)malloc(BUFFER_SIZE);

        audio.ec20_hdrs[i].dwBufferLength =
            BUFFER_SIZE;

        audio.ec20_hdrs[i].dwFlags = 0;

        waveInPrepareHeader(audio.hWaveInEC20,
                            &audio.ec20_hdrs[i],
                            sizeof(WAVEHDR));

        waveInAddBuffer(audio.hWaveInEC20,
                        &audio.ec20_hdrs[i],
                        sizeof(WAVEHDR));

        // Headphone input buffers
        audio.head_hdrs[i].lpData =
            (char*)malloc(BUFFER_SIZE);

        audio.head_hdrs[i].dwBufferLength =
            BUFFER_SIZE;

        audio.head_hdrs[i].dwFlags = 0;

        waveInPrepareHeader(audio.hWaveInHead,
                            &audio.head_hdrs[i],
                            sizeof(WAVEHDR));

        waveInAddBuffer(audio.hWaveInHead,
                        &audio.head_hdrs[i],
                        sizeof(WAVEHDR));
    }

    // --------------------------------------------------
    // START CAPTURE
    // --------------------------------------------------


    LOG_PRIORITY("DEBUG LOGS Before waveInStart EC20\n");
    ret = waveInStart(audio.hWaveInEC20);
    LOG_PRIORITY("DEBUG LOGS After waveInStart EC20 ret=%u\n", ret);
    if (ret != MMSYSERR_NOERROR)
    {
        LOG_NOTICE("waveInStart EC20 failed\n");
        return -1;
    }


    LOG_PRIORITY("DEBUG LOGS Before waveInStart Head\n");
    ret = waveInStart(audio.hWaveInHead);
    LOG_PRIORITY("DEBUG LOGS After waveInStart Head ret=%u\n", ret);
    if (ret != MMSYSERR_NOERROR)
    {
        LOG_NOTICE("waveInStart Head failed\n");
        return -1;
    }
    
    audio.running = 1;

    LOG_NOTICE("Audio engine started\n");

    return 0;
}


// ======================================================
// Stop Audio Engine
// ======================================================

void stop_audio_engine(void)
{
    int i;
    MMRESULT ret;

    if (!audio.running)
        return;

    LOG_NOTICE("Stopping audio engine...\n");

    audio.running = 0;

    // Stop capture
  LOG_PRIORITY("DEBUG LOGS Before waveInStop EC20\n");
    ret = waveInStop(audio.hWaveInEC20);
   LOG_PRIORITY("DEBUG LOGS waveInStop EC20 ret=%u\n", ret);


   LOG_PRIORITY("DEBUG LOGS Before waveInStop Head\n");
    ret = waveInStop(audio.hWaveInHead);
    LOG_PRIORITY("DEBUG LOGS waveInStop Head ret=%u\n", ret);
    

    // Flush pending buffers
    LOG_PRIORITY("DEBUG LOGS Before waveInReset EC20\n");
    ret = waveInReset(audio.hWaveInEC20);
    LOG_PRIORITY("DEBUG LOGS waveInReset EC20 ret=%u\n", ret);


    LOG_PRIORITY("DEBUG LOGS Before waveInReset Head\n");
    ret = waveInReset(audio.hWaveInHead);
    LOG_PRIORITY("DEBUG LOGS waveInReset Head ret=%u\n", ret);

    Sleep(200);

    // Free EC20 buffers
    for (i = 0; i < NUM_BUFFERS; i++) {

        waveInUnprepareHeader(audio.hWaveInEC20,
                              &audio.ec20_hdrs[i],
                              sizeof(WAVEHDR));

        free(audio.ec20_hdrs[i].lpData);

        waveInUnprepareHeader(audio.hWaveInHead,
                              &audio.head_hdrs[i],
                              sizeof(WAVEHDR));

        free(audio.head_hdrs[i].lpData);
    }

    free(audio.ec20_hdrs);
    free(audio.head_hdrs);

    audio.ec20_hdrs = NULL;
    audio.head_hdrs = NULL;

    // Close devices

    LOG_PRIORITY("DEBUG LOGS Before waveInClose EC20\n");
    ret = waveInClose(audio.hWaveInEC20);
    LOG_PRIORITY("DEBUG LOGS waveInClose EC20 ret=%u\n", ret);


    LOG_PRIORITY("DEBUG LOGS Before waveInClose Head\n");
    ret = waveInClose(audio.hWaveInHead);
    LOG_PRIORITY("DEBUG LOGS waveInClose Head ret=%u\n", ret);
    

    LOG_PRIORITY("DEBUG LOGS Before waveOutClose EC20\n");
    ret = waveOutClose(audio.hWaveOutEC20);
    LOG_PRIORITY("DEBUG LOGS waveOutClose EC20 ret=%u\n", ret);


    LOG_PRIORITY("DEBUG LOGS Before waveOutClose Head\n");
    ret = waveOutClose(audio.hWaveOutHead);
    LOG_PRIORITY("DEBUG LOGS waveOutClose Head ret=%u\n", ret);

    LOG_PRIORITY("DEBUG LOGS Audio engine stopped\n");
}

void write_wav_header(FILE *f, int data_size)
{
    int sampleRate = 8000;
    short channels = 1;
    short bits = 16;

    int byteRate = sampleRate * channels * bits / 8;
    short blockAlign = channels * bits / 8;

    fwrite("RIFF", 1, 4, f);
    int chunkSize = 36 + data_size;
    fwrite(&chunkSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    int subChunk1Size = 16;
    short audioFormat = 1;
    fwrite(&subChunk1Size, 4, 1, f);
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bits, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
}
