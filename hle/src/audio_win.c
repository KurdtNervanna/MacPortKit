/* Audio output: 8-bit unsigned mono samples pushed by the emulation thread are
   streamed to Windows waveOut. Producer (audio_push) and consumer (audio_service)
   both run on the emulation thread, so the ring needs no locking. */
#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include <stdio.h>
#include "audio.h"


#define AU_NBUF   6
#define AU_BUFLEN 512                      /* ~23 ms per buffer at 22.2 kHz   */
#define AU_RING   65536
static HWAVEOUT       g_wo;
static WAVEHDR        g_hdr[AU_NBUF];
static unsigned char  g_buf[AU_NBUF][AU_BUFLEN];
static unsigned char  g_ring[AU_RING];
static unsigned       g_rd = 0, g_wr = 0;  /* free-running indices           */
static int            g_ok = 0, g_rate = 22200;
static unsigned char  g_last = 0x80;
static FILE          *g_wav = 0; static unsigned g_wavn = 0;

int audio_init(int rate){
    WAVEFORMATEX f; int i;
    if (getenv(HLE_ENV("NOSOUND"))) return 0;
    memset(&f,0,sizeof f);
    f.wFormatTag=WAVE_FORMAT_PCM; f.nChannels=1; f.wBitsPerSample=8; f.nBlockAlign=1;
    f.nSamplesPerSec=rate; f.nAvgBytesPerSec=rate;
    if (waveOutOpen(&g_wo,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){
        rate=22050; f.nSamplesPerSec=rate; f.nAvgBytesPerSec=rate;
        if (waveOutOpen(&g_wo,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) return 0;
    }
    g_rate=rate;
    for (i=0;i<AU_NBUF;i++){                 /* prime with silence */
        memset(g_buf[i],0x80,AU_BUFLEN); memset(&g_hdr[i],0,sizeof g_hdr[i]);
        g_hdr[i].lpData=(LPSTR)g_buf[i]; g_hdr[i].dwBufferLength=AU_BUFLEN;
        waveOutPrepareHeader(g_wo,&g_hdr[i],sizeof g_hdr[i]);
        waveOutWrite(g_wo,&g_hdr[i],sizeof g_hdr[i]);
    }
    if (getenv(HLE_ENV("WAVDUMP"))){            /* debug: also write the stream to a .wav */
        g_wav=fopen(getenv(HLE_ENV("WAVDUMP")),"wb");
        if (g_wav){ unsigned char h[44]={0}; fwrite(h,1,44,g_wav); }
    }
    g_ok=1; return 1;
}

void audio_push(const unsigned char *s, int n){
    int i;
    if (g_wav){ fwrite(s,1,(size_t)n,g_wav); g_wavn+=(unsigned)n; }
    if (!g_ok) return;
    for (i=0;i<n;i++){ if (g_wr-g_rd >= AU_RING) break; g_ring[g_wr++ & (AU_RING-1)] = s[i]; }
}

void audio_service(void){
    int i,k;
    if (!g_ok) return;
    if (g_wr-g_rd > (unsigned)(g_rate/4)) g_rd = g_wr-(unsigned)(g_rate/8);   /* cap latency */
    for (i=0;i<AU_NBUF;i++) if (g_hdr[i].dwFlags & WHDR_DONE){
        for (k=0;k<AU_BUFLEN;k++){
            if (g_rd!=g_wr) g_last=g_ring[g_rd++ & (AU_RING-1)];
            else g_last = (unsigned char)(0x80 + ((int)g_last-0x80)*7/8);     /* underrun: decay to silence */
            g_buf[i][k]=g_last;
        }
        waveOutWrite(g_wo,&g_hdr[i],sizeof g_hdr[i]);
    }
}

void audio_shutdown(void){
    if (g_wav){ unsigned char h[44]; unsigned r=(unsigned)g_rate, n=g_wavn;
        memcpy(h,"RIFF",4); h[4]=(n+36)&255; h[5]=((n+36)>>8)&255; h[6]=((n+36)>>16)&255; h[7]=((n+36)>>24)&255;
        memcpy(h+8,"WAVEfmt ",8); h[16]=16; h[17]=h[18]=h[19]=0; h[20]=1; h[21]=0; h[22]=1; h[23]=0;
        h[24]=r&255; h[25]=(r>>8)&255; h[26]=(r>>16)&255; h[27]=0; h[28]=r&255; h[29]=(r>>8)&255; h[30]=(r>>16)&255; h[31]=0;
        h[32]=1; h[33]=0; h[34]=8; h[35]=0; memcpy(h+36,"data",4);
        h[40]=n&255; h[41]=(n>>8)&255; h[42]=(n>>16)&255; h[43]=(n>>24)&255;
        fseek(g_wav,0,SEEK_SET); fwrite(h,1,44,g_wav); fclose(g_wav); g_wav=0; }
    if (g_ok){ waveOutReset(g_wo); waveOutClose(g_wo); g_ok=0; }
}
