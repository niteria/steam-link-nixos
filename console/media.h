/* SPDX-License-Identifier: MIT
 * Playback uses the presenter's existing SLVideo context. Never create or free
 * another display context for a clip: vendor display teardown is global. */
#include <sys/stat.h>
#ifndef SM_MEDIA_TMP
#define SM_MEDIA_TMP "/tmp"
#endif

struct sm_video_api {
    __typeof__(SLVideo_CreateStream) *create;
    __typeof__(SLVideo_BeginFrame) *begin;
    __typeof__(SLVideo_WriteFrameData) *write;
    __typeof__(SLVideo_SubmitFrame) *submit;
    __typeof__(SLVideo_SetStreamTargetFramerate) *framerate;
    __typeof__(SLVideo_SetStreamTargetBitrate) *bitrate;
    __typeof__(SLVideo_GetStreamResolution) *resolution;
    __typeof__(SLVideo_FreeStream) *free;
};
struct sm_video_result { unsigned expected,submitted,width,height; int okay; };
static uint32_t sm_read32(FILE *file)
{
    unsigned char data[4];
    if(fread(data,1,4,file)!=4)return UINT32_MAX;
    return (uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
}
static struct sm_video_result sm_play(CSLVideoContext *context,const struct sm_video_api *api,const char *path)
{
    struct sm_video_result result={0};
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd<0)return result;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_size<24||st.st_size>9*1024*1024){close(fd);return result;}
    FILE *file=fdopen(fd,"rb");if(!file){close(fd);return result;}
    char magic[8];
    if(fread(magic,1,8,file)!=8||memcmp(magic,"SLAVH264",8)){fclose(file);return result;}
    result.expected=sm_read32(file);unsigned fps=sm_read32(file),width=sm_read32(file),height=sm_read32(file);
    if(!result.expected||result.expected>900||fps!=30||!width||width>1920||!height||height>1080){fclose(file);return result;}
    CSLVideoStream *stream=api->create(context,k_ESLVideoFormatH264,1);
    if(!stream){fclose(file);return result;}
    api->framerate(stream,fps,1);api->bitrate(stream,width>1280?8000:width>640?5000:2000);
    unsigned total=0;
    while(result.submitted<result.expected&&!stopping){
        unsigned length=sm_read32(file);
        if(!length||length>1048576||length>8388608-total)break;
        void *data=malloc(length);if(!data)break;
        int okay=fread(data,1,length,file)==length;
        if(okay)okay=api->begin(stream,(int)length)==0&&api->write(stream,data,(int)length)==0&&api->submit(stream)==0;
        free(data);if(!okay)break;
        total+=length;result.submitted++;usleep(1000000/fps);
    }
    int complete=result.submitted==result.expected&&fgetc(file)==EOF;
    usleep(200000);int decoded_width=0,decoded_height=0;
    api->resolution(stream,&decoded_width,&decoded_height);
    if(decoded_width>0)result.width=(unsigned)decoded_width;
    if(decoded_height>0)result.height=(unsigned)decoded_height;
    result.okay=complete&&!stopping&&result.width==width&&result.height==height;
    api->free(stream);fclose(file);
    return result;
}
static int sm_nonce(const char *data,size_t bytes)
{
    if(bytes!=17||data[16]!='\n')return 0;
    for(unsigned i=0;i<16;i++)if(!((data[i]>='0'&&data[i]<='9')||(data[i]>='a'&&data[i]<='f')))return 0;
    return 1;
}
static void sm_ready(const char *directory)
{
    char path[512];if(snprintf(path,sizeof(path),"%s/ready",directory)>=(int)sizeof(path))return;
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return;
    char data[128];int bytes=snprintf(data,sizeof(data),"{\"protocol\":1,\"audio_protocol\":5,\"display_protocol\":2,\"overlay_buffers\":3,\"pid\":%ld}\n",(long)getpid());
    if(write(fd,data,(size_t)bytes)!=bytes)fprintf(stderr,"media: ready marker write failed\n");
    close(fd);
}
static void sm_request(const char *directory,CSLVideoContext *context,CSLVideoOverlay *overlay,
                       const struct sm_video_api *api,__typeof__(SLVideo_HideOverlay) *hide,__typeof__(SLVideo_ShowOverlay) *show)
{
    char request[512];
    if(snprintf(request,sizeof(request),"%s/request",directory)>=(int)sizeof(request))return;
    int fd=open(request,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;
    struct stat st;char data[18];ssize_t bytes=read(fd,data,sizeof(data));
    int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&077)&&bytes>=0&&sm_nonce(data,(size_t)bytes);
    close(fd);unlink(request);
    if(!valid){fprintf(stderr,"media: invalid private playback request\n");return;}
    char busy[512];if(snprintf(busy,sizeof(busy),"%s/video-busy",directory)>=(int)sizeof(busy))return;
    fd=open(busy,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return;close(fd);
    data[16]=0;char clip[128];snprintf(clip,sizeof(clip),SM_MEDIA_TMP "/media-preview-%s.slav",data);
    fprintf(stderr,"media: begin request %s using existing display context\n",data);
    hide(overlay);
    struct sm_video_result result=sm_play(context,api,clip);
    show(overlay);
    unlink(busy);
    fprintf(stderr,"media: end request %s frames=%u/%u resolution=%ux%u okay=%d\n",
            data,result.submitted,result.expected,result.width,result.height,result.okay);
    char path[512];if(snprintf(path,sizeof(path),"%s/result",directory)>=(int)sizeof(path))return;
    fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return;
    FILE *out=fdopen(fd,"w");if(!out){close(fd);return;}
    fprintf(out,"{\"request\":\"%s\",\"okay\":%s,\"submitted_frames\":%u,\"expected_frames\":%u,\"reported_width\":%u,\"reported_height\":%u}\n",
            data,result.okay?"true":"false",result.submitted,result.expected,result.width,result.height);
    fclose(out);
}
