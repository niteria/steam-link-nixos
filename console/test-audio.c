/* SPDX-License-Identifier: MIT -- no audio device or sound is used. */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "SLVideo.h"
static volatile sig_atomic_t stopping;
#include "media.h"
#define SM_AUDIO_POLICY_FILE "./audio-policy-fixture"
#include "audio.h"
static unsigned context_count,stream_count,free_count,queued_count,submitted_count,flashes;
static unsigned prime_needed,zero_until,consume_limit,initialization_delay,startup_consume,residual_queue;
static int context,stream,fail_begin,bad_queue,stall,always_zero;
static int16_t buffer[960];static uint64_t clock_us=1000000;
static CSLAudioContext *context_create(void){context_count++;return (void *)&context;}
static int speakers(CSLAudioContext *c){assert(c==(void *)&context);return 2;}
static CSLAudioStream *stream_create(CSLAudioContext *c,int hz,int channels,int bytes,int low)
{assert(c==(void *)&context&&hz==48000&&channels==2&&bytes==1920&&!low);stream_count++;queued_count=0;submitted_count=0;clock_us+=initialization_delay;return (void *)&stream;}
static void *begin(CSLAudioStream *s){assert(s==(void *)&stream);return fail_begin?NULL:buffer;}
static void submit(CSLAudioStream *s){assert(s==(void *)&stream);for(unsigned i=0;i<960;i++){assert(abs(buffer[i])<=2048);if(submitted_count<SM_AUDIO_LEAD)assert(!buffer[i]);}queued_count+=480;submitted_count+=480;}
static uint32_t queued(CSLAudioStream *s){assert(s==(void *)&stream);return bad_queue?96001:always_zero||submitted_count<zero_until?0:queued_count;}
static void stream_free(CSLAudioStream *s){assert(s==(void *)&stream);free_count++;}
static void context_free(CSLAudioContext *c){(void)c;abort();}
static uint64_t now(void){return clock_us;}
static void pause_time(unsigned usec){clock_us+=usec;unsigned frames=usec*48/1000;if(!stall&&(submitted_count>=prime_needed||submitted_count<=startup_consume)&&(!consume_limit||submitted_count<consume_limit)){unsigned floor=queued_count<residual_queue?queued_count:residual_queue;queued_count=queued_count-floor>frames?queued_count-frames:floor;}}
static void visual(void *data,unsigned phase,int sync){assert(data==(void *)&stream);if(sync&&phase)flashes++;}
static void checksum(unsigned char *block){block[127]=0;unsigned sum=0;for(unsigned i=0;i<127;i++)sum+=block[i];block[127]=(unsigned char)(0-sum);}
static void test_sink(void)
{
    unsigned char data[256]={0,255,255,255,255,255,255,0};checksum(data);
    struct sm_sink sink=sm_parse_edid(data,128);assert(sink.valid&&!sink.lpcm_stereo_48k16);
    data[126]=1;checksum(data);data[128]=2;data[130]=8;data[132]=0x23;data[133]=0x09;data[134]=4;data[135]=1;checksum(data+128);
    sink=sm_parse_edid(data,256);assert(sink.valid&&sink.lpcm_stereo_48k16);
    data[135]=0;checksum(data+128);assert(!sm_parse_edid(data,256).lpcm_stereo_48k16);
    data[131]=0x40;checksum(data+128);assert(sm_parse_edid(data,256).lpcm_stereo_48k16);
    data[132]=0x3f;checksum(data+128);assert(!sm_parse_edid(data,256).valid);
    data[2]=0;assert(!sm_parse_edid(data,256).valid);assert(!sm_parse_edid(data,127).valid);
}
static void test_timeline(void)
{
    struct sm_audio_timeline t={0};
    /* Replay offsets from the real sync journal: the estimate crosses a flash
     * boundary, retreats, then crosses it again. Each click gets one100ms flash. */
    for(unsigned second=0;second<10;second++){
        unsigned base=SM_AUDIO_LEAD+second*SM_AUDIO_RATE;uint64_t at=(uint64_t)second*1000000;
        assert(sm_audio_phase(&t,base+384,at,1)==1);
        assert(sm_audio_phase(&t,base-1000,at+10000,1)==1);
        assert(sm_audio_phase(&t,base+2496,at+50000,1)==1);
        assert(sm_audio_phase(&t,base+2496,at+100000,1)==0);
        assert(sm_audio_phase(&t,base+1728,at+110000,1)==0);
        assert(sm_audio_phase(&t,base+4800,at+120000,1)==0);
        assert(t.flashes==second+1);
    }
    assert(t.regressions==20);
    assert(!sm_audio_phase(&t,SM_AUDIO_LEAD+480000,10000000,1)&&t.flashes==10);
    t=(struct sm_audio_timeline){0};
    assert(sm_audio_phase(&t,SM_AUDIO_LEAD,0,0)==1);
    assert(sm_audio_phase(&t,SM_AUDIO_LEAD+96000,2000000,0)==0);
    assert(sm_audio_phase(&t,SM_AUDIO_LEAD+95520,2010000,0)==0);
    assert(sm_audio_phase(&t,SM_AUDIO_LEAD+120000,2500000,0)==2);
    assert(sm_audio_phase(&t,SM_AUDIO_LEAD+116160,2510000,0)==2);
}
int main(void)
{
    test_sink();test_timeline();int16_t pcm[960];
    assert(!sm_hdmi_policy());FILE *policy=fopen(SM_AUDIO_POLICY_FILE,"wb");assert(policy);fputs("automatic\n",policy);fclose(policy);assert(!sm_hdmi_policy());
    policy=fopen(SM_AUDIO_POLICY_FILE,"wb");assert(policy);fputs("marvell\n",policy);fclose(policy);assert(sm_hdmi_policy());unlink(SM_AUDIO_POLICY_FILE);
    sm_audio_fill(pcm,48000,480,0,NULL);for(unsigned i=0;i<480;i++)assert(pcm[i*2+1]==0&&abs(pcm[i*2])<=2048);
    sm_audio_fill(pcm,144000,480,0,NULL);for(unsigned i=0;i<480;i++)assert(pcm[i*2]==0&&abs(pcm[i*2+1])<=2048);
    sm_audio_fill(pcm,100000,480,0,NULL);for(unsigned i=0;i<960;i++)assert(!pcm[i]);
    FILE *speech=tmpfile();assert(speech);for(unsigned i=0;i<960;i++)pcm[i]=i&1?-32768:32767;
    assert(fwrite(pcm,2,960,speech)==960);rewind(speech);sm_audio_fill(pcm,384000,480,0,speech);fclose(speech);
    for(unsigned i=0;i<960;i++)assert(abs(pcm[i])==2048);
    struct sm_audio_owner owner={0};owner.api=(struct sm_audio_api){context_create,speakers,stream_create,begin,submit,queued,stream_free,context_free};
    struct sm_audio_result r=sm_audio_run(&owner,0,NULL,visual,&stream,now,pause_time);
    assert(r.okay&&r.program_drained&&r.submitted==672000&&r.program_submitted==624000&&r.elapsed_us>=13900000&&r.max_queue<=2880&&r.consumer_progress);
    r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);
    assert(r.okay&&r.submitted==528000&&r.program_submitted==480000&&flashes==10&&r.visual_flashes==10&&context_count==1&&stream_count==2&&free_count==2);
    /* Reproduce the observed counters: zero initially, then5760 held frames.
     * A consumer needing250ms of priming deadlocks under the old50ms limit. */
    prime_needed=12000;zero_until=5760;flashes=0;initialization_delay=3000000;
    r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);
    assert(r.okay&&r.consumer_progress&&r.max_queue>=5760&&r.initialization_us==3000000&&flashes==10);
    /* Revision2 saw one early decrease, tightened to4800, then never reached
     * the startup threshold. Transient progress must not end priming. */
    zero_until=0;initialization_delay=0;startup_consume=2880;flashes=0;
    r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);
    assert(r.okay&&r.program_drained&&r.max_queue>=9000&&flashes==10);
    startup_consume=0;
    prime_needed=0;zero_until=0;initialization_delay=0;
    bad_queue=1;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);assert(!r.okay&&!strcmp(r.error,"invalid-audio-queue"));bad_queue=0;
    fail_begin=1;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);assert(!r.okay);fail_begin=0;
    stall=1;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);assert(!r.okay&&!strcmp(r.error,"audio-output-not-consuming")&&r.elapsed_us<2100000);stall=0;
    consume_limit=48000;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);assert(!r.okay&&r.consumer_progress&&!strcmp(r.error,"audio-output-stalled")&&r.elapsed_us-r.last_progress_us<2100000);consume_limit=0;
    prime_needed=SM_AUDIO_PRIME_LIMIT*2;startup_consume=2880;
    r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);
    assert(!r.okay&&r.consumer_progress&&!r.primed&&!strcmp(r.error,"audio-output-stalled")&&r.elapsed_us<2500000);
    prime_needed=0;startup_consume=0;
    /* A PE pipeline can retain a final quantum without EOS. Only silence may
     * remain on success; a residual reaching into the program must still fail. */
    residual_queue=1152;flashes=0;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);
    assert(r.okay&&r.program_drained&&!r.drained&&r.final_queue>0&&r.final_queue<=SM_AUDIO_TAIL&&flashes==10);
    residual_queue=SM_AUDIO_TAIL+480;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);
    assert(!r.okay&&!r.program_drained&&!strcmp(r.error,"audio-output-not-consuming"));residual_queue=0;
    consume_limit=SM_AUDIO_LEAD+480000-480;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);
    assert(!r.okay&&!r.program_drained&&!strcmp(r.error,"audio-output-stalled"));consume_limit=0;
    always_zero=1;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);assert(!r.okay&&!r.consumer_progress);always_zero=0;
    stopping=1;r=sm_audio_run(&owner,1,NULL,visual,&stream,now,pause_time);assert(!r.okay&&!strcmp(r.error,"interrupted"));stopping=0;
    char nonce[17];assert(sm_audio_command("0123456789abcdef channels\n",26,nonce)==1);
    assert(sm_audio_command("0123456789abcdef sync\n",22,nonce)==2);assert(sm_audio_command("../bad sink\n",12,nonce)<0);
    /* Reference other media handlers without ever creating a display context. */
    (void)sm_ready;(void)sm_request;
    puts("PASS: stereo/silence, startup regressions, priming, init clock, stall watchdog, program-drain/silent-tail boundaries, cancel, regressing-estimate replay and10 single100ms sync flashes");
}
