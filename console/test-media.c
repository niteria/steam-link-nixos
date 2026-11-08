/* SPDX-License-Identifier: MIT */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "SLVideo.h"
static volatile sig_atomic_t stopping;
#define SM_MEDIA_TMP "."
#include "media.h"
static int display,overlay,stream,hidden,created,freed,submitted;
static CSLVideoStream *create(CSLVideoContext *context,ESLVideoFormat format,int low)
{assert(context==(void *)&display&&format==k_ESLVideoFormatH264&&low&&hidden);created++;return (void *)&stream;}
static int begin(CSLVideoStream *s,int length){assert(s==(void *)&stream&&hidden&&length==8);return 0;}
static int write_data(CSLVideoStream *s,void *data,int length){assert(s==(void *)&stream&&data&&length==8);return 0;}
static int submit(CSLVideoStream *s){assert(s==(void *)&stream&&hidden);submitted++;return 0;}
static void framerate(CSLVideoStream *s,unsigned n,unsigned d){assert(s==(void *)&stream&&n==30&&d==1);}
static void bitrate(CSLVideoStream *s,int value){assert(s==(void *)&stream&&value==5000);}
static void resolution(CSLVideoStream *s,int *w,int *h){assert(s==(void *)&stream);*w=1280;*h=720;}
static void free_stream(CSLVideoStream *s){assert(s==(void *)&stream&&hidden);freed++;}
static void hide(CSLVideoOverlay *o){assert(o==(void *)&overlay&&!hidden);hidden=1;}
static void show(CSLVideoOverlay *o){assert(o==(void *)&overlay&&hidden&&freed==created);hidden=0;}
static void number(FILE *file,unsigned n){unsigned char b[4]={n,n>>8,n>>16,n>>24};assert(fwrite(b,1,4,file)==4);}
int main(void)
{
    umask(0077);char directory[]="./media-test-XXXXXX";assert(mkdtemp(directory));
    char nonce[17];snprintf(nonce,sizeof(nonce),"%016lx",(unsigned long)getpid());
    char clip[128],request[128],result[128],ready[128];
    snprintf(clip,sizeof(clip),"./media-preview-%s.slav",nonce);
    snprintf(request,sizeof(request),"%s/request",directory);snprintf(result,sizeof(result),"%s/result",directory);
    snprintf(ready,sizeof(ready),"%s/ready",directory);sm_ready(directory);assert(!access(ready,R_OK));
    struct sm_video_api api={create,begin,write_data,submit,framerate,bitrate,resolution,free_stream};
    for(int attempt=0;attempt<3;attempt++){
        FILE *file=fopen(clip,"wb");assert(file);assert(fwrite("SLAVH264",1,8,file)==8);
        number(file,2);number(file,30);number(file,1280);number(file,720);
        for(unsigned i=0;i<(attempt==2?1U:2U);i++){number(file,8);assert(fwrite("12345678",1,8,file)==8);}fclose(file);
        file=fopen(request,"wb");assert(file);fprintf(file,"%s\n",nonce);fclose(file);
        sm_request(directory,(void *)&display,(void *)&overlay,&api,hide,show);
        assert(!hidden&&created==attempt+1&&freed==created);
        file=fopen(result,"rb");assert(file);char data[512];size_t bytes=fread(data,1,sizeof(data)-1,file);data[bytes]=0;fclose(file);
        assert(strstr(data,attempt==2?"\"okay\":false":"\"okay\":true"));unlink(result);
    }
    assert(submitted==5);
    assert(!sm_nonce("../../etc/passwd\n",17));assert(!sm_nonce("0000000000000000x",17));
    unlink(clip);unlink(ready);assert(!rmdir(directory));
    puts("PASS: repeated playback shares one context, overlay restored on success/truncation, private request validation");
}
