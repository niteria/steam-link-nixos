// SPDX-License-Identifier: MIT
// Play S16LE stereo PCM written to a FIFO through the vendor SLAudio HDMI path.
// Runs inside the headless PE chroot and never creates a display context.
// Each writer session (open..close, e.g. a Bluetooth play..pause) is one burst.
// The SDK stream outlives short pauses and is freed after an idle period.
// Optionally 44.1 kHz input is resampled and played as a 48 kHz stream.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "SLAudio.h"
#include "resampler-coefficients.h"
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif
/* Vendor paths inside the PE chroot; the host tests substitute a mock SDK. */
#ifndef PCM_SINK_POLICY
#define PCM_SINK_POLICY "/mnt/config/system/audio_driver.txt"
#endif
#ifndef PCM_SINK_SHM
#define PCM_SINK_SHM "/home/steam/lib/libshm.so"
#endif
#ifndef PCM_SINK_SLAUDIO
#define PCM_SINK_SLAUDIO "/usr/lib/libSLAudio.so.1"
#endif
#define TAIL_CHUNKS 2U

static volatile sig_atomic_t stopping;
static void stop(int signal){(void)signal;stopping=1;}
static struct {
    __typeof__(SLAudio_CreateContext) *create_context;
    __typeof__(SLAudio_GetSpeakerCount) *speakers;
    __typeof__(SLAudio_CreateStream) *create;
    __typeof__(SLAudio_BeginFrame) *begin;
    __typeof__(SLAudio_SubmitFrame) *submit;
    __typeof__(SLAudio_GetQueuedAudioSamples) *queued;
    __typeof__(SLAudio_FreeStream) *free_stream;
    __typeof__(SLAudio_SetLogLevel) *log_level;
    __typeof__(SLAudio_SetLogFunction) *log_function;
} sl;
struct sink {
    /* Input chunks are 50 ms at in_rate; the SDK stream runs at rate. */
    unsigned in_rate,in_bytes,rate,chunk_frames,chunk_bytes,limit,hard,verbose,resample;
    CSLAudioContext *context;
    CSLAudioStream *stream;
    unsigned char *pending;
    unsigned pending_bytes;
    /* Per stream: frames submitted and the last consumption estimate. */
    uint64_t submitted,consumed,progress_at,forced_at,idle_since;
    /* Per writer session, for the summary line. */
    uint64_t session_at,session_frames,report_at,report_consumed;
    unsigned low_queue,max_queue,forced,stalled;
    /* Resampler: RS_TAPS-1 history frames followed by one input chunk. */
    int16_t *window;
    unsigned rs_index,rs_phase;
};

static uint64_t now_us(void)
{
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))abort();
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
static void sdk_log(void *context,ESLAudioLog level,const char *message)
{(void)context;fprintf(stderr,"SLAudio[%d]: %s",(int)level,message);}
static int load(unsigned verbose)
{
    /* libPEAgent resolves libshm symbols from the global scope. */
    if(!dlopen(PCM_SINK_SHM,RTLD_NOW|RTLD_GLOBAL)){fprintf(stderr,"pcm-sink: %s\n",dlerror());return -1;}
    void *library=dlopen(PCM_SINK_SLAUDIO,RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"pcm-sink: %s\n",dlerror());return -1;}
#define SYMBOL(field,name) do{void *symbol=dlsym(library,#name);if(!symbol){fprintf(stderr,"pcm-sink: missing %s\n",#name);return -1;}memcpy(&sl.field,&symbol,sizeof(symbol));}while(0)
    SYMBOL(create_context,SLAudio_CreateContext);SYMBOL(speakers,SLAudio_GetSpeakerCount);
    SYMBOL(create,SLAudio_CreateStream);SYMBOL(begin,SLAudio_BeginFrame);SYMBOL(submit,SLAudio_SubmitFrame);
    SYMBOL(queued,SLAudio_GetQueuedAudioSamples);SYMBOL(free_stream,SLAudio_FreeStream);
    SYMBOL(log_level,SLAudio_SetLogLevel);SYMBOL(log_function,SLAudio_SetLogFunction);
#undef SYMBOL
    sl.log_function(sdk_log,NULL);sl.log_level(verbose?k_ESLAudioLogInfo:k_ESLAudioLogWarning);
    return 0;
}
/* Without the explicit policy SLAudio falls back to SDL/ALSA, which this
 * system lacks; refuse to start rather than play into a dummy device. */
static int hdmi_policy(void)
{
    char value[16];int fd=open(PCM_SINK_POLICY,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return 0;
    ssize_t bytes=read(fd,value,sizeof(value));close(fd);
    return bytes==8&&!memcmp(value,"marvell\n",8);
}
/* Polyphase 160/147. One 2205-frame input chunk yields exactly one 2400-frame
 * output chunk, so the chunk accounting and pacing below are unchanged. */
static void resample(struct sink *s,const int16_t *in,int16_t *out)
{
    const unsigned history=RS_TAPS-1,frames=s->in_bytes/4;
    int16_t *x=s->window;
    if(in)memcpy(x+2*history,in,(size_t)frames*4);else memset(x+2*history,0,(size_t)frames*4);
    unsigned i=s->rs_index,p=s->rs_phase,n=0;
    while(i<history+frames){
        const int16_t *c=rs_coefficients[p];
        /* The taps' absolute sum exceeds 2: int64 keeps full-scale input exact. */
        int64_t left=1<<14,right=1<<14;
        for(unsigned k=0;k<RS_TAPS;k++){left+=c[k]*x[2*(i-k)];right+=c[k]*x[2*(i-k)+1];}
        left>>=15;right>>=15;
        out[2*n]=(int16_t)(left>32767?32767:left<-32768?-32768:left);
        out[2*n+1]=(int16_t)(right>32767?32767:right<-32768?-32768:right);
        n++;
        for(p+=RS_M;p>=RS_L;p-=RS_L)i++;
    }
    if(n!=s->chunk_frames)abort();
    memmove(x,x+2*frames,(size_t)history*4);
    s->rs_index=i-frames;s->rs_phase=p;
}
static int open_stream(struct sink *s)
{
    uint64_t start=now_us();
    /* PE plays one PCM stream at a time; after a client died it may report
     * busy (0x80007065) until it has released that stream. SLAudio itself
     * only retries for about 100 ms. */
    for(unsigned attempt=0;attempt<10&&!stopping;attempt++){
        s->stream=sl.create(s->context,(int)s->rate,2,(int)s->chunk_bytes,0);
        if(s->stream)break;
        usleep(500000);
    }
    if(!s->stream){fprintf(stderr,"pcm-sink: SLAudio_CreateStream(%u Hz) failed\n",s->rate);return -1;}
    s->submitted=s->consumed=s->forced_at=0;s->stalled=0;s->progress_at=now_us();
    if(s->resample){memset(s->window,0,(RS_TAPS-1)*4);s->rs_index=RS_TAPS-1;s->rs_phase=0;}
    fprintf(stderr,"pcm-sink: stream open rate=%u input=%u chunk=%u frames limit=%u frames in %llu us\n",
        s->rate,s->in_rate,s->chunk_frames,s->limit,(unsigned long long)(s->progress_at-start));
    return 0;
}
static void close_stream(struct sink *s,const char *reason)
{
    if(!s->stream)return;
    fprintf(stderr,"pcm-sink: stream closed (%s) after %llu frames\n",reason,(unsigned long long)s->submitted);
    sl.free_stream(s->stream);s->stream=NULL;
}
/* The queue estimate is an RPC into PE; polling it every millisecond starved
 * PE. Sleep until about one chunk of room is expected, at most 50 ms. */
static int wait_room(struct sink *s)
{
    for(int first=1;;first=0){
        if(stopping)return -1;
        uint64_t now=now_us();
        unsigned queued=sl.queued(s->stream);
        /* Less than one chunk left after the first second: a likely underrun. */
        if(first&&s->session_frames>s->rate&&queued<s->chunk_frames)s->low_queue++;
        uint64_t consumed=s->submitted>queued?s->submitted-queued:0;
        if(consumed>s->consumed){s->consumed=consumed;s->progress_at=now;s->stalled=0;}
        if(!s->stalled&&now-s->progress_at>=5000000){
            s->stalled=1;fprintf(stderr,"pcm-sink: HDMI audio not consuming; queued=%u\n",queued);
        }
        if(queued>s->max_queue)s->max_queue=queued;
        if(s->verbose&&now-s->report_at>=1000000){
            uint64_t span=now-s->report_at;
            fprintf(stderr,"pcm-sink: submitted=%llu queued=%u consumed=%llu rate=%.1f\n",(unsigned long long)s->submitted,
                queued,(unsigned long long)s->consumed,(double)(s->consumed-s->report_consumed)*1e6/(double)span);
            s->report_at=now;s->report_consumed=s->consumed;
        }
        if(queued<=s->hard){
            if(queued+s->chunk_frames<=s->limit)return 0;
            /* The decoder may hold a startup quantum before consuming. Never
             * deadlock behind the soft limit; the hard limit still applies. */
            if(now-s->progress_at>=1000000&&now-s->forced_at>=1000000){s->forced++;s->forced_at=now;return 0;}
        }
        uint64_t wait=(uint64_t)(queued+s->chunk_frames-s->limit)*1000000/s->rate;
        usleep(wait<5000?5000:wait>50000?50000:(useconds_t)wait);
    }
}
/* Submit one input chunk (NULL: silence) as one stream chunk. */
static int submit_chunk(struct sink *s,const void *pcm)
{
    if(wait_room(s))return -1;
    void *buffer=NULL;
    for(unsigned attempt=0;!buffer&&attempt<20&&!stopping;attempt++){
        buffer=sl.begin(s->stream);
        if(!buffer)usleep(50000);
    }
    if(!buffer)return -1;
    if(s->resample)resample(s,pcm,buffer);
    else if(pcm)memcpy(buffer,pcm,s->chunk_bytes);else memset(buffer,0,s->chunk_bytes);
    sl.submit(s->stream);
    s->submitted+=s->chunk_frames;s->session_frames+=s->chunk_frames;
    return 0;
}
static void begin_session(struct sink *s)
{
    s->session_at=s->report_at=now_us();s->session_frames=0;s->report_consumed=s->consumed;
    s->low_queue=s->max_queue=s->forced=0;s->idle_since=0;
}
/* SLAudio has no EOS/flush: a partial chunk is padded and a short silent tail
 * pushes the decoder's retained quantum (about 1536 frames) out to HDMI. */
static int end_session(struct sink *s)
{
    int result=0;
    if(s->stream){
        if(s->pending_bytes){
            memset(s->pending+s->pending_bytes,0,s->in_bytes-s->pending_bytes);
            result=submit_chunk(s,s->pending);
        }
        for(unsigned i=0;!result&&i<TAIL_CHUNKS;i++)result=submit_chunk(s,NULL);
    }
    uint64_t elapsed=now_us()-s->session_at;
    fprintf(stderr,"pcm-sink: session end frames=%llu seconds=%.2f max_queue=%u low_queue=%u forced=%u\n",
        (unsigned long long)s->session_frames,(double)elapsed/1e6,s->max_queue,s->low_queue,s->forced);
    s->pending_bytes=0;s->idle_since=now_us();
    return result;
}
/* Modern headers redirect strtoul to a symbol the vendor glibc 2.19 lacks. */
static unsigned number(const char *text,unsigned low,unsigned high,const char *what)
{
    unsigned long value=0;const char *digit=text;
    for(;*digit>='0'&&*digit<='9'&&value<=high;digit++)value=value*10+(unsigned long)(*digit-'0');
    if(digit==text||*digit||value<low||value>high){fprintf(stderr,"pcm-sink: invalid %s: %s\n",what,text);exit(2);}
    return (unsigned)value;
}
int main(int argc,char **argv)
{
    unsigned queue_ms=300,idle_s=15,pipe_bytes=16384,verbose=0,out_rate=0;int option;
    while((option=getopt(argc,argv,"vq:i:p:o:"))!=-1){
        if(option=='v')verbose=1;
        else if(option=='o')out_rate=number(optarg,44100,48000,"output rate");
        else if(option=='q')queue_ms=number(optarg,100,1000,"queue milliseconds");
        else if(option=='i')idle_s=number(optarg,1,3600,"idle seconds");
        else if(option=='p')pipe_bytes=number(optarg,4096,1048576,"pipe bytes");
        else return 2;
    }
    if(argc-optind!=2){fprintf(stderr,"usage: pcm-sink [-v] [-o 48000] [-q queue_ms] [-i idle_s] [-p pipe_bytes] RATE FIFO\n");return 2;}
    struct sink s={.verbose=verbose};
    s.in_rate=number(argv[optind],44100,48000,"rate");
    if(s.in_rate!=44100&&s.in_rate!=48000){fprintf(stderr,"pcm-sink: rate must be 44100 or 48000\n");return 2;}
    s.rate=out_rate?out_rate:s.in_rate;
    if(s.rate!=s.in_rate&&!(s.in_rate==44100&&s.rate==48000)){fprintf(stderr,"pcm-sink: only 44100 -> 48000 is resampled\n");return 2;}
    s.resample=s.rate!=s.in_rate;
    const char *path=argv[optind+1];
    /* 50 ms chunks, as in the qualified 48 kHz bridge: 2205 or 2400 frames. */
    s.in_bytes=s.in_rate/20*4;s.chunk_frames=s.rate/20;s.chunk_bytes=s.chunk_frames*4;
    s.limit=s.rate*queue_ms/1000;s.hard=s.rate*2;
    s.pending=malloc(s.in_bytes);s.window=malloc((RS_TAPS-1+s.in_bytes/4)*4);
    if(!s.pending||!s.window)return 1;
    setvbuf(stderr,NULL,_IOLBF,0);
    struct sigaction action={.sa_handler=stop};
    sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);
    signal(SIGPIPE,SIG_IGN);
    if(!hdmi_policy()){fprintf(stderr,"pcm-sink: %s must select marvell\n",PCM_SINK_POLICY);return 1;}
    if(load(verbose))return 1;
    s.context=sl.create_context();
    if(!s.context){fprintf(stderr,"pcm-sink: SLAudio_CreateContext failed\n");return 1;}
    int speakers=sl.speakers(s.context);
    if(speakers<2){fprintf(stderr,"pcm-sink: %d speakers configured; stereo required\n",speakers);return 1;}
    fprintf(stderr,"pcm-sink: ready on %s, %u Hz stereo S16LE%s, speakers=%d\n",path,s.in_rate,s.resample?" resampled to 48000 Hz":"",speakers);
    int failed=0;
    while(!stopping&&!failed){
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if(fd<0){fprintf(stderr,"pcm-sink: open %s: %s\n",path,strerror(errno));sleep(1);continue;}
        struct stat st;
        if(fstat(fd,&st)||!S_ISFIFO(st.st_mode)){fprintf(stderr,"pcm-sink: %s is not a FIFO\n",path);close(fd);return 1;}
        /* Bound the latency hidden in the pipe: 16 KiB is about 93 ms. */
        if(fcntl(fd,F_SETPIPE_SZ,(int)pipe_bytes)<0)fprintf(stderr,"pcm-sink: F_SETPIPE_SZ: %s\n",strerror(errno));
        int active=0;
        while(!stopping){
            /* Linux reports POLLHUP to this reader only after a writer has
             * come and gone, so an idle FIFO simply times out. */
            struct pollfd waiter={.fd=fd,.events=POLLIN};
            int ready=poll(&waiter,1,1000);
            if(ready<0){if(errno==EINTR)continue;fprintf(stderr,"pcm-sink: poll: %s\n",strerror(errno));failed=1;break;}
            if(!ready){
                if(!active&&s.stream&&now_us()-s.idle_since>=(uint64_t)idle_s*1000000)close_stream(&s,"idle");
                continue;
            }
            ssize_t bytes=0;
            if(waiter.revents&POLLIN){
                bytes=read(fd,s.pending+s.pending_bytes,s.in_bytes-s.pending_bytes);
                if(bytes<0&&(errno==EAGAIN||errno==EINTR))continue;
                if(bytes<0){fprintf(stderr,"pcm-sink: read: %s\n",strerror(errno));failed=1;break;}
            }
            if(bytes>0){
                if(!active){
                    active=1;begin_session(&s);
                    if(!s.stream&&open_stream(&s)){failed=1;break;}
                }
                s.pending_bytes+=(unsigned)bytes;
                if(s.pending_bytes==s.in_bytes){
                    if(submit_chunk(&s,s.pending)){if(!stopping){fprintf(stderr,"pcm-sink: no SDK buffer\n");failed=1;}break;}
                    s.pending_bytes=0;
                }
                continue;
            }
            /* EOF, or a hangup without data: every writer has gone. */
            if(active&&end_session(&s)&&!stopping){fprintf(stderr,"pcm-sink: no SDK buffer\n");failed=1;}
            break;
        }
        close(fd);
    }
    close_stream(&s,stopping?"stopping":"failure");
    free(s.pending);free(s.window);
    /* The context is left to process exit; PE releases agent clients itself. */
    return failed;
}
