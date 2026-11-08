/* SPDX-License-Identifier: MIT -- on-demand48kHz stereo diagnostic, one owner. */
#include <time.h>
#include "SLAudio.h"
#include "edid.h"
#include "surfaces.h"
#define SM_AUDIO_RATE 48000U
#define SM_AUDIO_CHUNK 480U
#define SM_AUDIO_PEAK 2048
#define SM_AUDIO_LEAD 24000U
#define SM_AUDIO_TAIL 24000U
#define SM_AUDIO_PRIME_LIMIT 24000U
#define SM_AUDIO_RUNNING_LIMIT 4800U
#ifndef SM_AUDIO_POLICY_FILE
#define SM_AUDIO_POLICY_FILE "/mnt/config/system/audio_driver.txt"
#endif
struct sm_audio_api {
    __typeof__(SLAudio_CreateContext) *context;
    __typeof__(SLAudio_GetSpeakerCount) *speakers;
    __typeof__(SLAudio_CreateStream) *stream;
    __typeof__(SLAudio_BeginFrame) *begin;
    __typeof__(SLAudio_SubmitFrame) *submit;
    __typeof__(SLAudio_GetQueuedAudioSamples) *queued;
    __typeof__(SLAudio_FreeStream) *free_stream;
    __typeof__(SLAudio_FreeContext) *free_context;
};
struct sm_audio_owner { void *library;struct sm_audio_api api;CSLAudioContext *context;uint32_t sink_identity; };
struct sm_audio_result {unsigned submitted,max_queue,empty_queue,queue_decreases,observed_drops,final_queue,program_submitted,visual_flashes,visual_regressions;int okay,drained,program_drained,speakers,consumer_progress,primed;uint64_t elapsed_us,initialization_us,first_progress_us,last_progress_us;const char *error;};
static inline uint64_t sm_now_us(void)
{struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return 0;return (uint64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;}
static inline void sm_pause_us(unsigned usec){(void)usleep(usec);}
static inline int16_t sm_wave(unsigned sample,unsigned frequency,unsigned position,unsigned duration)
{
    static const int16_t wave[32]={0,400,784,1138,1448,1703,1892,2009,2048,2009,1892,1703,1448,1138,784,400,
        0,-400,-784,-1138,-1448,-1703,-1892,-2009,-2048,-2009,-1892,-1703,-1448,-1138,-784,-400};
    unsigned envelope=240;
    if(position<envelope)envelope=position;
    if(duration-position<envelope)envelope=duration-position;
    return (int16_t)((int)wave[((uint64_t)sample*frequency*32/SM_AUDIO_RATE)%32]*(int)envelope/240);
}
static inline void sm_audio_fill(int16_t *pcm,unsigned first,unsigned frames,int sync,FILE *speech)
{
    memset(pcm,0,(size_t)frames*4);
    for(unsigned i=0;i<frames;i++){
        unsigned at=first+i,position=0,duration=0;int left=0,right=0;
        if(sync){position=at%SM_AUDIO_RATE;duration=240;if(position<duration)left=right=1;}
        else if(at<96000){position=at;duration=96000;left=1;}
        else if(at>=120000&&at<216000){position=at-120000;duration=96000;right=1;}
        else if(at>=240000&&at<336000){position=at-240000;duration=96000;left=right=1;}
        if(left)pcm[i*2]=sm_wave(position,sync?1000:440,position,duration);
        if(right)pcm[i*2+1]=sm_wave(position,sync?1000:660,position,duration);
    }
    if(!sync&&first>=384000&&speech){
        size_t read=fread(pcm,4,frames,speech);
        for(unsigned i=(unsigned)read*2;i<frames*2;i++)pcm[i]=0;
        for(unsigned i=0;i<frames*2;i++){if(pcm[i]>SM_AUDIO_PEAK)pcm[i]=SM_AUDIO_PEAK;if(pcm[i]<-SM_AUDIO_PEAK)pcm[i]=-SM_AUDIO_PEAK;}
    }
}
static void sm_audio_log(void *context,ESLAudioLog level,const char *message)
{(void)context;fprintf(stderr,"SLAudio[%d]: %s",(int)level,message);}
static inline int sm_audio_load(struct sm_audio_owner *owner)
{
    if(owner->library)return 1;
    void *library=dlopen("/usr/lib/libSLAudio.so.1",RTLD_NOW|RTLD_LOCAL);if(!library)return 0;
#define SM_AUDIO_SYMBOL(field,name) do {void *symbol=dlsym(library,#name);if(!symbol){dlclose(library);return 0;}memcpy(&owner->api.field,&symbol,sizeof(symbol));}while(0)
    SM_AUDIO_SYMBOL(context,SLAudio_CreateContext);SM_AUDIO_SYMBOL(speakers,SLAudio_GetSpeakerCount);
    SM_AUDIO_SYMBOL(stream,SLAudio_CreateStream);SM_AUDIO_SYMBOL(begin,SLAudio_BeginFrame);SM_AUDIO_SYMBOL(submit,SLAudio_SubmitFrame);
    SM_AUDIO_SYMBOL(queued,SLAudio_GetQueuedAudioSamples);SM_AUDIO_SYMBOL(free_stream,SLAudio_FreeStream);SM_AUDIO_SYMBOL(free_context,SLAudio_FreeContext);
    /* Enable route/stream diagnostics only on the explicit audio-load path. */
    __typeof__(SLAudio_SetLogLevel) *log_level;
    __typeof__(SLAudio_SetLogFunction) *log_function;
    void *symbol=dlsym(library,"SLAudio_SetLogLevel");if(!symbol){dlclose(library);return 0;}memcpy(&log_level,&symbol,sizeof(symbol));
    symbol=dlsym(library,"SLAudio_SetLogFunction");if(!symbol){dlclose(library);return 0;}memcpy(&log_function,&symbol,sizeof(symbol));
    log_function(sm_audio_log,NULL);log_level(k_ESLAudioLogDebug);
#undef SM_AUDIO_SYMBOL
    owner->library=library;return 1;
}
static inline int sm_hdmi_policy(void)
{
    int fd=open(SM_AUDIO_POLICY_FILE,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
    char value[16];ssize_t bytes=read(fd,value,sizeof(value));close(fd);
    return bytes==8&&!memcmp(value,"marvell\n",8);
}
/* Queue estimates sum two independently sampled vendor counters. They can move
 * backward, but a displayed phase must never replay. This is a presentation
 * timeline only; raw counters still govern all drain and stall decisions. */
struct sm_audio_timeline {unsigned played,regressions,flashes,last_sync;int sync_started;uint64_t flash_until;};
static unsigned sm_audio_phase(struct sm_audio_timeline *t,unsigned played,uint64_t current,int sync)
{
    if(played<t->played)t->regressions++;else t->played=played;
    unsigned program=t->played>=SM_AUDIO_LEAD?t->played-SM_AUDIO_LEAD:0;
    if(sync){
        if(t->played>=SM_AUDIO_LEAD&&program<480000){
            unsigned index=program/SM_AUDIO_RATE;
            if(!t->sync_started||index>t->last_sync){
                t->sync_started=1;t->last_sync=index;t->flashes++;t->flash_until=current+100000;
            }
        }
        return t->sync_started&&current<t->flash_until;
    }
    return t->played<SM_AUDIO_LEAD||program>=624000?0:program<96000?1:program<120000?0:program<216000?2:program<240000?0:program<336000?3:program<384000?0:4;
}
/* Audio remains uninitialized until an explicit operator request. */
static struct sm_audio_result sm_audio_run(struct sm_audio_owner *owner,int sync,FILE *speech,
    void (*visual)(void *,unsigned,int),void *visual_context,uint64_t (*now)(void),void (*pause)(unsigned))
{
    struct sm_audio_result result={0};
    uint64_t start=now();if(!start){result.error="monotonic-clock-failed";return result;}
    if(!owner->context)owner->context=owner->api.context();
    if(!owner->context){result.error="audio-context-failed";return result;}
    result.speakers=owner->api.speakers(owner->context);
    if(result.speakers<2){result.error="stereo-speakers-not-configured";return result;}
    CSLAudioStream *stream=owner->api.stream(owner->context,48000,2,1920,0);
    if(!stream){result.error="audio-stream-failed";return result;}
    uint64_t initialized=now();if(!initialized||initialized<start){owner->api.free_stream(stream);result.error="monotonic-clock-failed";return result;}
    result.initialization_us=initialized-start;start=initialized;
    unsigned program_frames=SM_AUDIO_RATE*(sync?10:13),program_end=program_frames+SM_AUDIO_LEAD;
    unsigned total=program_end+SM_AUDIO_TAIL,last_visual=UINT32_MAX;
    unsigned previous_queue=0;int observed=0,seen_nonzero=0;
    struct sm_audio_timeline timeline={0};
    uint64_t deadline=start+((sync?10U:13U)+5)*1000000ULL;
    while(!stopping){
        uint64_t current=now();if(!current||current<start){result.error="monotonic-clock-failed";break;}
        unsigned queued=owner->api.queued(stream);
        result.final_queue=queued;
        if(queued>96000||queued>result.submitted){result.error="invalid-audio-queue";break;}
        /* A queue decrease cannot be caused by adding another chunk. Initial
         * zero or stale counters alone do not establish consumer progress. */
        if(observed&&seen_nonzero&&queued<previous_queue){
            result.queue_decreases++;
            result.observed_drops+=previous_queue-queued;
            result.last_progress_us=current-start;
            if(!result.consumer_progress){result.consumer_progress=1;result.first_progress_us=current-start;}
        }
        /* One startup counter drop is insufficient: revision2 tightened the
         * queue before startup buffering finished, deadlocking at4800 frames.
         * Keep the bounded prime budget until a full silent lead has shown
         * queue decreases. These conservative deltas exclude enqueue growth. */
        if(result.observed_drops>=SM_AUDIO_LEAD)result.primed=1;
        if(queued)seen_nonzero=1;
        previous_queue=queued;observed=1;
        if(queued>result.max_queue)result.max_queue=queued;
        /* SLAudio reports input-ring occupancy plus PE pipeline samples; it
         * exposes no EOS/flush call. Feed a bounded silent tail and require the
         * queue estimate to pass the entire program boundary. Remaining silence
         * is reported explicitly, never mislabeled as an empty hardware queue. */
        result.program_drained=result.submitted>=program_end&&queued<=result.submitted-program_end&&result.primed;
        if(result.submitted==total&&result.program_drained){result.drained=!queued;result.okay=1;break;}
        unsigned played=result.consumer_progress?result.submitted-queued:0;
        unsigned phase=sm_audio_phase(&timeline,played,current,sync);
        unsigned program=timeline.played>=SM_AUDIO_LEAD?timeline.played-SM_AUDIO_LEAD:0;
        if(phase!=last_visual){fprintf(stderr,"audio: sync=%d phase=%u estimated_program_sample=%u queued=%u submitted=%u\n",sync,phase,program,queued,result.submitted);if(sync&&phase)result.visual_flashes++;visual(visual_context,phase,sync);last_visual=phase;}
        if(current-start-result.last_progress_us>2000000){result.error=result.consumer_progress?"audio-output-stalled":"audio-output-not-consuming";break;}
        if(current>deadline){result.error="audio-drain-timeout";break;}
        uint64_t paced=(current-start)*SM_AUDIO_RATE/1000000+2400;
        unsigned queue_limit=result.primed?SM_AUDIO_RUNNING_LIMIT:SM_AUDIO_PRIME_LIMIT;
        if(result.submitted<total&&queued<queue_limit&&result.submitted<paced){
            if(!queued&&result.submitted)result.empty_queue++;
            int16_t *pcm=owner->api.begin(stream);if(!pcm){result.error="audio-buffer-failed";break;}
            if(result.submitted<SM_AUDIO_LEAD||result.submitted>=program_end)memset(pcm,0,SM_AUDIO_CHUNK*4);
            else {sm_audio_fill(pcm,result.submitted-SM_AUDIO_LEAD,SM_AUDIO_CHUNK,sync,speech);result.program_submitted+=SM_AUDIO_CHUNK;}
            owner->api.submit(stream);result.submitted+=SM_AUDIO_CHUNK;
        }else pause(1000);
    }
    if(stopping){result.okay=0;result.error="interrupted";}
    result.visual_regressions=timeline.regressions;
    if(result.okay&&sync&&result.visual_flashes!=10){result.okay=0;result.error="sync-visual-pulses-missed";}
    uint64_t end=now();result.elapsed_us=end>=start?end-start:0;
    owner->api.free_stream(stream);return result;
}
struct sm_audio_visual {struct sp_pool *pool;};
static inline void sm_audio_visualize(void *context,unsigned phase,int sync)
{
    struct sm_audio_visual *v=context;
    struct sp_pool *pool=v->pool;struct sp_surface *surface=sp_back(pool);
    for(unsigned y=0;y<pool->height;y++)for(unsigned x=0;x<pool->width;x++){
        uint32_t color=0xff101820;
        if(sync)color=phase?0xffffffff:0xff000000;
        else if((phase==1&&x<pool->width/2)||(phase==2&&x>=pool->width/2)||phase>=3)
            color=phase==1?0xff226688:phase==2?0xff884466:phase==3?0xff228866:0xff446688;
        surface->pixels[(size_t)y*(surface->pitch/4)+x]=color;
    }
    sp_present(pool);
}
static int sm_audio_command(const char *data,size_t bytes,char nonce[17])
{
    if(bytes<22||bytes>26||data[16]!=' ')return -1;
    char prefix[17];memcpy(prefix,data,16);prefix[16]='\n';
    if(!sm_nonce(prefix,17))return -1;
    memcpy(nonce,data,16);nonce[16]=0;
    if(bytes==22&&!memcmp(data+17,"sink\n",5))return 0;
    if(bytes==22&&!memcmp(data+17,"sync\n",5))return 2;
    if(bytes==26&&!memcmp(data+17,"channels\n",9))return 1;
    return -1;
}
static inline void sm_audio_request(const char *directory,struct sm_audio_owner *owner,struct sm_audio_visual *visual)
{
    char request[512],result_path[512],nonce[17]={0};
    if(snprintf(request,sizeof(request),"%s/audio-request",directory)>=(int)sizeof(request)||
       snprintf(result_path,sizeof(result_path),"%s/audio-result",directory)>=(int)sizeof(result_path))return;
    int fd=open(request,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;
    struct stat st;char data[32];ssize_t bytes=read(fd,data,sizeof(data));
    int mode=-1;
    if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&077)&&bytes>=0)mode=sm_audio_command(data,(size_t)bytes,nonce);
    close(fd);unlink(request);if(mode<0){fprintf(stderr,"audio: invalid private request\n");return;}
    char busy[512];if(snprintf(busy,sizeof(busy),"%s/audio-busy",directory)>=(int)sizeof(busy))return;
    fd=open(busy,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return;close(fd);
    /* HDMI has its own DDC master. Generic TWSI reads are not qualified HDMI
     * capability evidence. Use the public SLAudio speaker configuration only
     * after an explicit listening request, never fabricate a fresh EDID pass. */
    struct sm_sink sink={0};sink.error=7;struct sm_audio_result result={0};
    if(!mode){result.okay=1;if(owner->context)result.speakers=owner->api.speakers(owner->context);}
    else if(!sm_audio_load(owner))result.error="audio-library-unavailable";
    else if(!sm_hdmi_policy())result.error="explicit-marvell-hdmi-policy-required";
    else{
        FILE *speech=NULL;
        if(mode==1){
            fd=open("/opt/steamlink-diagnostics/speech.pcm",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
            if(fd>=0){if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==960000){speech=fdopen(fd,"rb");if(!speech)close(fd);}else close(fd);}
        }
        if(mode==1&&!speech)result.error="diagnostic-speech-sample-missing";
        else{
            for(unsigned count=3;count&&!stopping;count--){sm_audio_visualize(visual,count%2,1);usleep(1000000);}
            if(!stopping)result=sm_audio_run(owner,mode==2,speech,sm_audio_visualize,visual,sm_now_us,sm_pause_us);
        }
        if(speech)fclose(speech);
    }
    unlink(busy);
    fd=open(result_path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return;
    FILE *out=fdopen(fd,"w");if(!out){close(fd);return;}
    fprintf(out,"{\"request\":\"%s\",\"mode\":\"%s\",\"okay\":%s,\"error\":\"%s\",\"sink_capability_source\":\"sdk-at-test-start\",\"fresh_edid_qualified\":false,\"sink_valid\":%s,\"sink_name\":\"%s\",\"sink_id\":%u,\"lpcm_stereo_48k16\":null,\"edid_extensions\":%d,\"read_error\":%d,\"io_errno\":%d,\"speakers\":%d,\"submitted_sample_frames\":%u,\"max_queued_sample_frames\":%u,\"empty_queue_observations\":%u,\"drained\":%s,\"elapsed_us\":%llu,\"sample_rate\":48000,\"channels\":2,\"sample_bits\":16,\"peak_amplitude\":2048,",
        nonce,!mode?"sink":mode==1?"channels":"sync",result.okay?"true":"false",result.error?result.error:"",
        sink.valid?"true":"false",sink.name,sink.identity,sink.extensions,sink.error,sink.io_errno,result.speakers,
        result.submitted,result.max_queue,result.empty_queue,result.drained?"true":"false",(unsigned long long)result.elapsed_us);
    fprintf(out,"\"initialization_us\":%llu,\"consumer_progress_observed\":%s,\"queue_decreases\":%u,\"first_progress_us\":%llu,\"last_progress_us\":%llu,\"observed_queue_drop_sample_frames\":%u,\"startup_primed\":%s,\"silent_lead_in_sample_frames\":24000,\"silent_tail_sample_frames\":24000,\"program_sample_frames_submitted\":%u,\"program_drained\":%s,\"final_queued_sample_frames\":%u,\"visual_sync_flashes\":%u,\"visual_estimate_regressions\":%u,\"visual_flash_minimum_us\":100000,\"output_policy\":\"marvell-hdmi\",\"audio_test_revision\":5}\n",
        (unsigned long long)result.initialization_us,result.consumer_progress?"true":"false",result.queue_decreases,(unsigned long long)result.first_progress_us,
        (unsigned long long)result.last_progress_us,result.observed_drops,result.primed?"true":"false",result.program_submitted,result.program_drained?"true":"false",result.final_queue,result.visual_flashes,result.visual_regressions);
    fclose(out);
}
