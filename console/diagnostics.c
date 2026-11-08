/* SPDX-License-Identifier: MIT -- local diagnostics require no network/auth. */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "edid.h"
static const char *control="/run/steamlink-console/root/run/media-player";
static volatile sig_atomic_t cancelled;
static void stop(int number){(void)number;cancelled=1;}
static uint64_t now(void){struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return 0;return (uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;}
static void fail(const char *text){fprintf(stderr,"diagnostics: %s\n",text);exit(1);}
static void print_result(const char *data){size_t bytes=strlen(data);if(!bytes)return;fputs(data,stdout);if(data[bytes-1]!='\n')fputc('\n',stdout);fflush(stdout);}
static int read_private(int dir,const char *name,char *data,size_t bytes)
{
    int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_size<1||st.st_size>=(off_t)bytes){close(fd);return 0;}
    ssize_t count=read(fd,data,bytes-1);close(fd);if(count<1)return 0;data[count]=0;return 1;
}
static int request(const char *mode,char *result,size_t bytes)
{
    int directory=open(control,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(directory<0){fprintf(stderr,"diagnostics: media display is not ready\n");return 1;}
    struct stat st;
    if(fstat(directory,&st)||st.st_uid!=geteuid()||(st.st_mode&077)){close(directory);fail("private control directory required");}
    char ready[512];
    if(!read_private(directory,"ready",ready,sizeof(ready))||!strstr(ready,"\"audio_protocol\":5")){close(directory);fprintf(stderr,"diagnostics: revision5 audio-capable media display required\n");return 1;}
    int lock=openat(directory,"diagnostic.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);close(directory);fprintf(stderr,"diagnostics: another diagnostic is running\n");return 1;}
    if(!faccessat(directory,"audio-busy",F_OK,0)||!faccessat(directory,"video-busy",F_OK,0)||!faccessat(directory,"audio-request",F_OK,0)){
        close(lock);close(directory);fprintf(stderr,"diagnostics: media display is busy\n");return 1;
    }
    unsigned char random[8];int source=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(source<0||read(source,random,8)!=8)fail("random request ID");
    close(source);
    char nonce[17];for(unsigned i=0;i<8;i++)snprintf(nonce+i*2,3,"%02x",random[i]);
    unlinkat(directory,"audio-result",0);
    int fd=openat(directory,"audio-request.pending",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)fail("request staging file exists");
    char data[40];int length=snprintf(data,sizeof(data),"%s %s\n",nonce,mode);
    if(write(fd,data,(size_t)length)!=length||close(fd)||renameat(directory,"audio-request.pending",directory,"audio-request"))fail("publish request");
    uint64_t deadline=now()+80000;int outcome=1;
    while(!cancelled&&now()<deadline){
        if(read_private(directory,"audio-result",result,bytes)){
            char expected[64];snprintf(expected,sizeof(expected),"\"request\":\"%s\"",nonce);
            if(!strstr(result,expected))fail("different request result");
            outcome=strstr(result,"\"okay\":true")?0:2;break;
        }
        usleep(100000);
    }
    if(outcome==1)fprintf(stderr,"diagnostics: request interrupted or timed out; display may still be finishing its bounded test\n");
    close(lock);close(directory);return outcome;
}
static int perform(const char *mode,int yes)
{
    char sink[4096]={0};int code=request("sink",sink,sizeof(sink));
    print_result(sink);if(code||!strcmp(mode,"sink"))return code;
    if(!strstr(sink,"\"lpcm_stereo_48k16\":true")&&!strstr(sink,"\"sink_capability_source\":\"sdk-at-test-start\"")){
        fprintf(stderr,"diagnostics: connect an HDMI sink advertising stereo48kHz16-bit PCM, then retry; no audio was started\n");return 3;
    }
    if(strstr(sink,"\"sink_capability_source\":\"sdk-at-test-start\""))
        fprintf(stderr,"diagnostics: fresh HDMI EDID is unqualified; the explicit test will check SDK stereo configuration\n");
    if(!yes){
        if(!isatty(STDIN_FILENO)){fprintf(stderr,"diagnostics: start this listening test from an interactive terminal\n");return 1;}
        fprintf(stderr,"Headphones connected to monitor audio out; choose a comfortable low volume.\n");
        fprintf(stderr,!strcmp(mode,"sync")?"Ten flashes/clicks, one per second, after a three-second countdown.\n":"Quiet left/right/stereo tones, then speech; about sixteen seconds including countdown.\n");
        fprintf(stderr,"Press Enter when ready, or type q to cancel: ");char answer[32];
        if(!fgets(answer,sizeof(answer),stdin)||strcmp(answer,"\n"))return 0;
    }
    char result[4096]={0};code=request(mode,result,sizeof(result));print_result(result);return code;
}
int main(int argc,char **argv)
{
    umask(0077);signal(SIGINT,stop);signal(SIGTERM,stop);const char *mode=NULL;int yes=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--sink-direct")){
            struct sm_sink sink=sm_read_sink();
            printf("{\"sink_valid\":%s,\"sink_name\":\"%s\",\"sink_id\":%u,\"lpcm_stereo_48k16\":%s,\"read_error\":%d,\"io_errno\":%d,\"audio_started\":false}\n",
                sink.valid?"true":"false",sink.name,sink.identity,sink.lpcm_stereo_48k16?"true":"false",sink.error,sink.io_errno);
            return sink.valid?0:1;
        }
        if(!strcmp(argv[i],"--control")&&i+1<argc)control=argv[++i];
        else if(!strcmp(argv[i],"--yes"))yes=1;
        else if(!strcmp(argv[i],"--help")){puts("steamlink-diagnostics sink|channels|sync|menu [--yes] [--control PRIVATE_DIR] | --sink-direct");return 0;}
        else if(!mode)mode=argv[i];else fail("invalid option");
    }
    if(!mode||(!strchr("scm",mode[0])))fail("choose sink, channels, sync or menu");
    if(!strcmp(mode,"menu")){
        while(!cancelled){
            puts("\nSteam Link local diagnostics (no network required)\n1 Audio readiness/configuration (fresh EDID unqualified)\n2 Headphone left/right/stereo/speech check\n3 Flash/click A/V sync check\n0 Return to console (Ctrl+Alt+F1)\n");
            char choice[32];if(!fgets(choice,sizeof(choice),stdin))break;
            if(choice[0]=='0'){int tty=open("/dev/tty1",O_RDWR|O_NOCTTY);if(tty>=0){ioctl(tty,VT_ACTIVATE,1);close(tty);}continue;}
            const char *selected=choice[0]=='1'?"sink":choice[0]=='2'?"channels":choice[0]=='3'?"sync":NULL;
            if(selected)(void)perform(selected,0);
        }
        return 0;
    }
    if(strcmp(mode,"sink")&&strcmp(mode,"channels")&&strcmp(mode,"sync"))fail("invalid diagnostic mode");
    return perform(mode,yes);
}
