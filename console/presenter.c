/* SPDX-License-Identifier: MIT
 * Present the actual Linux fbcon buffer through Valve's HDMI overlay API.
 * The kernel owns the VT, fonts, input and /dev/fb0; this process owns scanout. */
#define _DEFAULT_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "SLVideo.h"
#include "pixels.h"
static volatile sig_atomic_t stopping;
static void stop(int signal) { (void)signal; stopping=1; }
#include "media.h"
#include "audio.h"
static int geometry(int fd, size_t bytes, struct sfc_geometry *g)
{
    struct fb_fix_screeninfo fixed; struct fb_var_screeninfo variable;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fixed) || ioctl(fd, FBIOGET_VSCREENINFO, &variable)) return 0;
    if (variable.bits_per_pixel!=32 || variable.red.length!=8 || variable.green.length!=8 ||
        variable.blue.length!=8 || variable.red.msb_right || variable.green.msb_right || variable.blue.msb_right ||
        fixed.type!=FB_TYPE_PACKED_PIXELS || fixed.visual!=FB_VISUAL_TRUECOLOR || fixed.smem_len!=bytes) return 0;
    *g=(struct sfc_geometry){variable.xres,variable.yres,fixed.line_length,variable.xoffset,variable.yoffset,
                             variable.red.offset,variable.green.offset,variable.blue.offset};
    return sfc_valid(g, bytes, g->width*4, (size_t)g->width*g->height*4);
}
#define API(variable, name) __typeof__(name) *variable; do { \
    void *symbol=dlsym(library,#name); if(!symbol){fprintf(stderr,"console: missing %s\n",#name);return 2;} \
    memcpy(&variable,&symbol,sizeof(variable)); } while(0)
int main(int argc, char **argv)
{
    setvbuf(stderr,NULL,_IONBF,0);
    int snapshot=argc==3&&!strcmp(argv[1],"--snapshot");
    if(argc!=2&&!snapshot){fprintf(stderr,"usage: fb-console FBDEV | --snapshot FBDEV\n");return 2;}
    const char *device=argv[snapshot?2:1];
    int fd=open(device,O_RDONLY|O_CLOEXEC);if(fd<0){perror("console: framebuffer open");return 2;}
    struct fb_fix_screeninfo fixed;
    if(ioctl(fd,FBIOGET_FSCREENINFO,&fixed)||!fixed.smem_len||fixed.smem_len>16*1024*1024){close(fd);return 2;}
    struct sfc_geometry g;
    if(!geometry(fd,fixed.smem_len,&g)){fprintf(stderr,"console: unsupported framebuffer geometry/format\n");close(fd);return 2;}
    const volatile uint32_t *source=mmap(NULL,fixed.smem_len,PROT_READ,MAP_SHARED,fd,0);
    if(source==MAP_FAILED){perror("console: framebuffer mmap");close(fd);return 2;}
    if(snapshot){
        uint32_t *row=malloc((size_t)g.width*4);if(!row)return 2;
        printf("P6\n%u %u\n255\n",g.width,g.height);
        struct sfc_geometry line=g;line.height=1;
        for(uint32_t y=0;y<g.height;y++){
            line.yoffset=g.yoffset+y;
            if(!sfc_copy(row,(size_t)g.width*4,g.width*4,source,fixed.smem_len,&line))return 2;
            for(uint32_t x=0;x<g.width;x++){
                unsigned char rgb[3]={row[x]>>16,row[x]>>8,row[x]};
                if(fwrite(rgb,1,3,stdout)!=3)return 2;
            }
        }
        free(row);munmap((void *)source,fixed.smem_len);close(fd);return fflush(stdout)?2:0;
    }
    struct utsname kernel;
    if(uname(&kernel)||strcmp(kernel.release,"7.2.6")||access("/sys/module/steamlink_pe",F_OK)){
        fprintf(stderr,"console: requires the explicit PE/HDMI runtime\n");return 3;
    }
    void *library=dlopen("/usr/lib/libSLVideo.so.1",RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"console: video library: %s\n",dlerror());return 2;}
    API(create,SLVideo_CreateContext);API(free_context,SLVideo_FreeContext);
    API(create_overlay,SLVideo_CreateOverlay);API(get_pixels,SLVideo_GetOverlayPixels);
    API(fullscreen,SLVideo_SetOverlayDisplayFullscreen);API(show,SLVideo_ShowOverlay);
    API(hide,SLVideo_HideOverlay);API(free_overlay,SLVideo_FreeOverlay);
    const char *control=getenv("STEAMLINK_MEDIA_CONTROL");struct sm_video_api video={0};struct sm_audio_owner audio={0};
    if(control){
        API(stream_create,SLVideo_CreateStream);API(begin,SLVideo_BeginFrame);API(write_data,SLVideo_WriteFrameData);
        API(submit,SLVideo_SubmitFrame);API(framerate,SLVideo_SetStreamTargetFramerate);API(bitrate,SLVideo_SetStreamTargetBitrate);
        API(resolution,SLVideo_GetStreamResolution);API(free_stream,SLVideo_FreeStream);
        video=(struct sm_video_api){stream_create,begin,write_data,submit,framerate,bitrate,resolution,free_stream};
    }
    fprintf(stderr,"console: creating video context for %ux%u fbcon\n",g.width,g.height);
    CSLVideoContext *context=create();if(!context)return 2;
    fprintf(stderr,"console: creating triple-buffered overlays\n");
    struct sp_pool pool;
    if(!sp_init(&pool,context,(struct sp_api){create_overlay,get_pixels,fullscreen,show,hide,free_overlay},g.width,g.height)){free_context(context);return 2;}
    signal(SIGINT,stop);signal(SIGTERM,stop);
    struct sm_audio_visual audio_visual={&pool};
    uint32_t width=g.width,height=g.height;unsigned long frames=0;int result=0;
    while(!stopping){
        struct sp_surface *back=sp_back(&pool);
        if(!geometry(fd,fixed.smem_len,&g)||g.width!=width||g.height!=height||
            !sfc_copy(back->pixels,(size_t)back->pitch*height,back->pitch,source,fixed.smem_len,&g)){result=2;break;}
        sp_present(&pool);
        if(!frames++){
            fprintf(stderr,"console: framebuffer presented; triple-buffered at 30Hz\n");
            const char *ready=getenv("STEAMLINK_FB_READY");
            if(ready){int marker=open(ready,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(marker<0){result=2;break;}close(marker);}
            if(control)sm_ready(control);
        }
        if(control)sm_request(control,context,pool.surfaces[pool.front].overlay,&video,hide,show);
        if(control)sm_audio_request(control,&audio,&audio_visual);
        usleep(33333);
    }
    fprintf(stderr,"console: stopping audio context\n");
    if(audio.context)audio.api.free_context(audio.context);
    fprintf(stderr,"console: audio context stopped\n");
    if(audio.library)dlclose(audio.library);
    sp_destroy(&pool);free_context(context);dlclose(library);
    munmap((void *)source,fixed.smem_len);close(fd);
    fprintf(stderr,"console: stopped after %lu frames\n",frames);return result;
}
