/* SPDX-License-Identifier: MIT -- bounded EDID/CTA LPCM capability parser. */
#include <errno.h>
struct sm_sink { int valid,basic_audio,lpcm_stereo_48k16,extensions,error,io_errno; uint32_t identity;char name[14]; };
static inline struct sm_sink sm_parse_edid(const unsigned char *data,size_t bytes)
{
    struct sm_sink sink={0};
    static const unsigned char magic[8]={0,255,255,255,255,255,255,0};
    if((bytes!=128&&bytes!=256)||memcmp(data,magic,8)){sink.error=4;return sink;}
    for(size_t block=0;block<bytes/128;block++){
        unsigned sum=0;for(unsigned i=0;i<128;i++)sum+=data[block*128+i];
        if(sum&255){sink.error=5;return sink;}
    }
    sink.valid=1;sink.extensions=data[126];
    sink.identity=2166136261U;for(size_t i=0;i<bytes;i++)sink.identity=(sink.identity^data[i])*16777619U;
    for(unsigned i=54;i+18<=126;i+=18)if(!data[i]&&!data[i+1]&&!data[i+2]&&data[i+3]==0xfc){
        for(unsigned j=0;j<13&&data[i+5+j]>=32&&data[i+5+j]<127;j++)sink.name[j]=data[i+5+j]=='"'||data[i+5+j]=='\\'?'?':(char)data[i+5+j];
    }
    if(!sink.extensions||bytes<256||data[128]!=2)return sink;
    const unsigned char *cta=data+128;
    sink.basic_audio=!!(cta[3]&0x40);sink.lpcm_stereo_48k16=sink.basic_audio;
    unsigned end=cta[2];if(!end)return sink;
    if(end<4||end>127){sink.valid=0;sink.lpcm_stereo_48k16=0;sink.error=6;return sink;}
    for(unsigned i=4;i<end;){
        unsigned tag=cta[i]>>5,length=cta[i]&31;i++;
        if(length>end-i){sink.valid=0;sink.lpcm_stereo_48k16=0;sink.error=6;return sink;}
        if(tag==1)for(unsigned j=0;j+3<=length;j+=3){
            const unsigned char *sad=cta+i+j;
            if(((sad[0]>>3)&15)==1&&(sad[0]&7)>=1&&(sad[1]&4)&&(sad[2]&1))sink.lpcm_stereo_48k16=1;
        }
        i+=length;
    }
    return sink;
}
/* Only EDID offset writes followed by reads: no EEPROM data writes, bus retune
 * or segment programming. Extra extension blocks are reported, not guessed. */
static inline struct sm_sink sm_read_sink(void)
{
    unsigned char edid[256]={0},offset=0;
    struct {uint32_t master,address,type,read_count,read_buffer,write_count,write_buffer;} request={0};
    struct sm_sink empty={0};
    if(sizeof(void *)!=4){empty.error=1;empty.io_errno=EOVERFLOW;return empty;}
    int fd=open("/dev/twsi0",O_RDWR|O_CLOEXEC);if(fd<0){empty.error=1;empty.io_errno=errno;return empty;}
    request.address=0x50;request.read_count=128;request.read_buffer=(uint32_t)(uintptr_t)edid;
    request.write_count=1;request.write_buffer=(uint32_t)(uintptr_t)&offset;
    if(ioctl(fd,0x1619,&request)){empty.error=2;empty.io_errno=errno;close(fd);return empty;}
    size_t bytes=128;
    if(edid[126]){offset=128;request.read_buffer=(uint32_t)(uintptr_t)(edid+128);
        if(ioctl(fd,0x1619,&request)){empty.error=3;empty.io_errno=errno;close(fd);return empty;}bytes=256;}
    close(fd);struct sm_sink sink=sm_parse_edid(edid,bytes);
    if(!sink.valid){fprintf(stderr,"EDID validation error%d header",sink.error);for(unsigned i=0;i<8;i++)fprintf(stderr," %02x",edid[i]);fputc('\n',stderr);}
    return sink;
}
