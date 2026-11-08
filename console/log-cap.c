// SPDX-License-Identifier: MIT
// Bound a live process's regular-file log without stopping the process.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop(int signal){(void)signal;stopping=1;}
static int copy_bytes(int source,int destination,off_t offset,off_t length)
{
    char buffer[16384];
    while(length){
        size_t size=length>(off_t)sizeof(buffer)?sizeof(buffer):(size_t)length;
        ssize_t got=pread(source,buffer,size,offset);
        if(got<0&&errno==EINTR)continue;
        if(got<=0){errno=EIO;return -1;}
        size_t sent=0;
        while(sent<(size_t)got){
            ssize_t n=write(destination,buffer+sent,got-sent);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)return -1;
            sent+=n;
        }
        offset+=got;length-=got;
    }
    return fsync(destination);
}
static int cap_log(pid_t pid,const char *path,off_t limit,off_t *before)
{
    int result=-1,file=-1,pidfd=-1,writer=-1,archive=-1,temporary_created=0;
    char temporary[PATH_MAX]={0},previous[PATH_MAX],startup[PATH_MAX];
    struct stat st,owner;
    file=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW);if(file<0)goto done;
    if(fstat(file,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()){errno=EINVAL;goto done;}
    if(flock(file,LOCK_EX|LOCK_NB))goto done;
    pidfd=syscall(SYS_pidfd_open,pid,0);if(pidfd<0)goto done;
    writer=syscall(SYS_pidfd_getfd,pidfd,STDOUT_FILENO,0);if(writer<0)goto done;
    if(fstat(writer,&owner)||owner.st_dev!=st.st_dev||owner.st_ino!=st.st_ino){errno=EINVAL;goto done;}
    int flags=fcntl(writer,F_GETFL);
    if(flags<0||(flags&O_ACCMODE)==O_RDONLY){errno=EINVAL;goto done;}
    // pidfd_getfd duplicates the SAME open-file description. Updating this flag
    // prevents the original writer's old offset from making sparse holes after truncation.
    if(!(flags&O_APPEND)&&fcntl(writer,F_SETFL,flags|O_APPEND))goto done;
    *before=st.st_size;
    if(st.st_size<=limit){result=0;goto done;}
    if(snprintf(temporary,sizeof(temporary),"%s.cap-%ld.tmp",path,(long)getpid())>=(int)sizeof(temporary)
       ||snprintf(previous,sizeof(previous),"%s.previous",path)>=(int)sizeof(previous)
       ||snprintf(startup,sizeof(startup),"%s.startup",path)>=(int)sizeof(startup)){errno=ENAMETOOLONG;goto done;}
    archive=open(startup,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(archive>=0){
        if(copy_bytes(file,archive,0,st.st_size<65536?st.st_size:65536)){unlink(startup);goto done;}
        close(archive);archive=-1;
    }else if(errno!=EEXIST)goto done;
    if(!lstat(previous,&owner)&&(!S_ISREG(owner.st_mode)||owner.st_uid!=geteuid())){errno=EINVAL;goto done;}
    archive=open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(archive<0)goto done;
    temporary_created=1;
    if(copy_bytes(file,archive,st.st_size-limit,limit))goto done;
    close(archive);archive=-1;
    if(rename(temporary,previous))goto done;
    temporary_created=0;
    // Copy-truncate is best-effort diagnostic retention, not an atomic event journal.
    if(ftruncate(file,0))goto done;
    result=1;
done:
    {int error=errno;
    if(archive>=0)close(archive);
    if(temporary_created)unlink(temporary);
    if(writer>=0)close(writer);
    if(pidfd>=0)close(pidfd);
    if(file>=0)close(file);
    errno=error;}
    return result;
}
int main(int argc,char **argv)
{
    if(argc!=4&&argc!=5)return 2;
    int watch=argc==5&&!strcmp(argv[4],"--watch");if(argc==5&&!watch)return 2;
    char *end=NULL;long pid=strtol(argv[1],&end,10);if(!end||*end||pid<=1||pid>INT_MAX)return 2;
    long limit=strtol(argv[3],&end,10);if(!end||*end||limit<65536||limit>16*1024*1024)return 2;
    signal(SIGINT,stop);signal(SIGTERM,stop);setvbuf(stdout,NULL,_IOLBF,0);
    do {
        off_t before=0;int result=cap_log(pid,argv[2],limit,&before);
        if(result<0){perror("log-cap");return 1;}
        if(result||!watch)printf("{\"rotated\":%s,\"before_bytes\":%lld,\"tail_limit_bytes\":%ld,\"writer_append\":true}\n",
                                result?"true":"false",(long long)before,limit);
        if(watch)for(unsigned i=0;i<5&&!stopping;i++)sleep(1);
    }while(watch&&!stopping);
    return 0;
}
