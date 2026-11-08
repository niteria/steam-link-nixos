/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "pixels.h"
int main(void)
{
    uint32_t source[24]={0},out[10];
    struct sfc_geometry g={3,2,24,1,1,0,8,16};
    source[7]=0x00563312;source[13]=0x000000ff;
    for(unsigned i=0;i<10;i++)out[i]=0xdeadbeef;
    assert(sfc_copy(out,sizeof(out),20,source,sizeof(source),&g));
    assert(out[0]==0xff123356&&out[5]==0xffff0000);
    assert(out[3]==0xdeadbeef&&out[4]==0xdeadbeef&&out[8]==0xdeadbeef&&out[9]==0xdeadbeef);
    assert(!sfc_copy(out,sizeof(out),20,source,63,&g));
    assert(!sfc_copy(out,24,20,source,sizeof(source),&g));
    struct sfc_geometry bad=g;bad.xoffset=0xffffffff;assert(!sfc_valid(&bad,sizeof(source),20,sizeof(out)));
    bad=g;bad.yoffset=0xffffffff;assert(!sfc_valid(&bad,sizeof(source),20,sizeof(out)));
    bad=g;bad.pitch=25;assert(!sfc_valid(&bad,sizeof(source),20,sizeof(out)));
    bad=g;bad.red=bad.blue;assert(!sfc_valid(&bad,sizeof(source),20,sizeof(out)));
    bad=g;bad.height=0;assert(!sfc_valid(&bad,sizeof(source),20,sizeof(out)));
    bad=g;bad.width=1921;assert(!sfc_valid(&bad,sizeof(source),8000,sizeof(out)));
    for(unsigned i=1;i<=10000;i++){
        unsigned w=i%32+1,h=(i/32)%32+1,x=i%5,y=(i/5)%5,pitch=(w+x+3)*4,dp=(w+2)*4;
        size_t bytes=(size_t)(h+y)*pitch,db=(size_t)h*dp;
        uint32_t *src=malloc(bytes),*dst=malloc(db);assert(src&&dst);
        for(size_t j=0;j<bytes/4;j++)src[j]=0x00112233;
        for(size_t j=0;j<db/4;j++)dst[j]=0xdeadbeef;
        struct sfc_geometry test={w,h,pitch,x,y,0,8,16};
        assert(sfc_copy(dst,db,dp,src,bytes,&test));
        for(unsigned row=0;row<h;row++)for(unsigned col=0;col<w+2;col++)
            assert(dst[row*(dp/4)+col]==(col<w?0xff332211:0xdeadbeef));
        free(src);free(dst);
    }
    puts("PASS: 10000 padded/panned fbcon copies, channel order, opaque alpha and bounds refusals");
}
