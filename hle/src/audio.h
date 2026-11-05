#ifndef HLE_AUDIO_H
#define HLE_AUDIO_H
#include "config.h"
int  audio_init(int rate);                         /* 8-bit unsigned mono; 0 if unavailable */
void audio_push(const unsigned char *s, int n);    /* queue samples (emulation thread)      */
void audio_service(void);                          /* refill waveOut buffers                */
void audio_shutdown(void);
#endif
