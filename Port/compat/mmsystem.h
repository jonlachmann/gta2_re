// Port (docs/port.md): the multimedia types mss.h declares its waveOut functions with.
#pragma once
#include "windows.h"

DECLARE_HANDLE(HWAVEOUT);
DECLARE_HANDLE(HWAVEIN);
DECLARE_HANDLE(HMIDIOUT);
typedef HWAVEOUT* LPHWAVEOUT;
typedef HMIDIOUT* LPHMIDIOUT;
typedef struct waveformat_tag
{
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
} WAVEFORMAT, *LPWAVEFORMAT;
typedef struct pcmwaveformat_tag
{
    WAVEFORMAT wf;
    WORD wBitsPerSample;
} PCMWAVEFORMAT;
typedef struct wavehdr_tag
{
    LPSTR lpData;
    DWORD dwBufferLength;
    DWORD dwBytesRecorded;
    DWORD_PTR dwUser;
    DWORD dwFlags;
    DWORD dwLoops;
    struct wavehdr_tag* lpNext;
    DWORD_PTR reserved;
} WAVEHDR, *LPWAVEHDR;
typedef struct midihdr_tag
{
    LPSTR lpData;
    DWORD dwBufferLength;
    DWORD dwBytesRecorded;
    DWORD_PTR dwUser;
    DWORD dwFlags;
    struct midihdr_tag* lpNext;
    DWORD_PTR reserved;
    DWORD dwOffset;
    DWORD_PTR dwReserved[8];
} MIDIHDR, *LPMIDIHDR;
#define WAVE_FORMAT_PCM 1
