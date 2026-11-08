// SPDX-License-Identifier: MIT
// Host-only SLAudio stand-in for pcm-sink tests: records every submitted
// frame and consumes the queue in real time, like the HDMI output would.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "SLAudio.h"

struct CSLAudioContext { int speakers; };
struct CSLAudioStream { int rate,bytes,begun; unsigned char *buffer; uint64_t submitted,started; FILE *out; };
static struct CSLAudioContext context={2};
static FILE *journal;

static uint64_t now_us(void)
{
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static void note(const char *format,long long a,long long b,long long c)
{
    if(!journal){journal=fopen(getenv("MOCK_JOURNAL"),"a");if(!journal)abort();setvbuf(journal,NULL,_IOLBF,0);}
    fprintf(journal,format,a,b,c);
}
void SLAudio_SetLogLevel(ESLAudioLog level){(void)level;}
void SLAudio_SetLogFunction(SLAudio_LogFunction function,void *pointer){(void)function;(void)pointer;}
CSLAudioContext *SLAudio_CreateContext(void){note("context %lld %lld %lld\n",0,0,0);return &context;}
int SLAudio_GetSpeakerCount(CSLAudioContext *pointer){return pointer->speakers;}
CSLAudioStream *SLAudio_CreateStream(CSLAudioContext *pointer,int rate,int channels,int bytes,int low_latency)
{
    if(pointer!=&context||channels!=2||bytes<=0||low_latency)abort();
    CSLAudioStream *s=calloc(1,sizeof(*s));if(!s)abort();
    s->rate=rate;s->bytes=bytes;s->buffer=malloc((size_t)bytes);
    s->out=fopen(getenv("MOCK_PCM"),"ab");if(!s->buffer||!s->out)abort();
    note("create %lld %lld %lld\n",rate,channels,bytes);
    return s;
}
void *SLAudio_BeginFrame(CSLAudioStream *s){if(s->begun)abort();s->begun=1;memset(s->buffer,0xa5,(size_t)s->bytes);return s->buffer;}
void SLAudio_SubmitFrame(CSLAudioStream *s)
{
    if(!s->begun)abort();
    s->begun=0;
    if(fwrite(s->buffer,1,(size_t)s->bytes,s->out)!=(size_t)s->bytes||fflush(s->out))abort();
    if(!s->submitted)s->started=now_us();
    s->submitted+=(uint64_t)s->bytes/4;
}
uint32_t SLAudio_GetQueuedAudioSamples(CSLAudioStream *s)
{
    if(!s->submitted)return 0;
    uint64_t played=(now_us()-s->started)*(uint64_t)s->rate/1000000;
    return played>=s->submitted?0:(uint32_t)(s->submitted-played);
}
void SLAudio_FreeStream(CSLAudioStream *s)
{
    note("free %lld %lld %lld\n",(long long)s->submitted,0,0);
    fclose(s->out);free(s->buffer);free(s);
}
