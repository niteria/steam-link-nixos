/* SPDX-License-Identifier: MIT -- same pool helper as production, no display. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "SLVideo.h"
#include "surfaces.h"
struct fake {uint32_t pixels[16];int freed;};
static struct fake surfaces[SP_COUNT];static unsigned created,freed,visible=SP_COUNT,fail_at=SP_COUNT;
static CSLVideoOverlay *create(CSLVideoContext *c,int w,int h)
{(void)c;assert(w==4&&h==4);if(created==fail_at)return NULL;return (void *)&surfaces[created++];}
static void pixels(CSLVideoOverlay *o,uint32_t **p,int *pitch){*p=((struct fake *)o)->pixels;*pitch=16;}
static void full(CSLVideoOverlay *o){assert(o);}
static void show(CSLVideoOverlay *o){visible=(unsigned)((struct fake *)o-surfaces);}
static void hide(CSLVideoOverlay *o){assert(o==(void *)&surfaces[visible]);visible=SP_COUNT;}
static void release(CSLVideoOverlay *o){struct fake *s=(void *)o;assert(!s->freed);s->freed=1;freed++;}
int main(void)
{
    struct sp_pool p;struct sp_api api={create,pixels,full,show,hide,release};
    assert(sp_init(&p,NULL,api,4,4));
    for(unsigned n=0;n<10000;n++){
        struct sp_surface *back=sp_back(&p);assert(back->overlay!=(visible<SP_COUNT?(void *)&surfaces[visible]:NULL));
        uint32_t old=visible<SP_COUNT?surfaces[visible].pixels[0]:0;
        for(unsigned k=0;k<16;k++)back->pixels[k]=n;
        if(visible<SP_COUNT)assert(surfaces[visible].pixels[0]==old);
        sp_present(&p);assert(surfaces[visible].pixels[0]==n);
    }
    sp_destroy(&p);assert(freed==3&&visible==SP_COUNT);
    for(unsigned i=0;i<SP_COUNT;i++)surfaces[i].freed=0;
    created=freed=0;fail_at=2;assert(!sp_init(&p,NULL,api,4,4));assert(freed==2&&visible==SP_COUNT);
    puts("PASS:10000 alternating presentations never modify the front surface; partial allocation cleanup");
}
