/* SPDX-License-Identifier: MIT -- never render into the displayed overlay. */
#ifndef STEAMLINK_SURFACES_H
#define STEAMLINK_SURFACES_H
#define SP_COUNT 3U
struct sp_surface {CSLVideoOverlay *overlay;uint32_t *pixels;unsigned pitch;};
struct sp_api {
    __typeof__(SLVideo_CreateOverlay) *create;
    __typeof__(SLVideo_GetOverlayPixels) *pixels;
    __typeof__(SLVideo_SetOverlayDisplayFullscreen) *fullscreen;
    __typeof__(SLVideo_ShowOverlay) *show;
    __typeof__(SLVideo_HideOverlay) *hide;
    __typeof__(SLVideo_FreeOverlay) *free;
};
struct sp_pool {struct sp_api api;struct sp_surface surfaces[SP_COUNT];unsigned front,width,height;};
static inline void sp_destroy(struct sp_pool *p)
{
    if(p->front<SP_COUNT)p->api.hide(p->surfaces[p->front].overlay);
    for(unsigned i=0;i<SP_COUNT;i++)if(p->surfaces[i].overlay){p->api.free(p->surfaces[i].overlay);p->surfaces[i].overlay=NULL;}
    p->front=SP_COUNT;
}
static inline int sp_init(struct sp_pool *p,CSLVideoContext *context,struct sp_api api,unsigned width,unsigned height)
{
    *p=(struct sp_pool){.api=api,.front=SP_COUNT,.width=width,.height=height};
    if(!width||width>1920||!height||height>1080)return 0;
    for(unsigned i=0;i<SP_COUNT;i++){
        struct sp_surface *s=&p->surfaces[i];
        s->overlay=api.create(context,(int)width,(int)height);if(!s->overlay){sp_destroy(p);return 0;}
        int pitch=0;api.pixels(s->overlay,&s->pixels,&pitch);
        if(!s->pixels||pitch<(int)width*4||pitch>16384||(pitch&3)){sp_destroy(p);return 0;}
        s->pitch=(unsigned)pitch;api.fullscreen(s->overlay);
    }
    return 1;
}
static inline struct sp_surface *sp_back(struct sp_pool *p)
{return &p->surfaces[p->front<SP_COUNT?(p->front+1)%SP_COUNT:0];}
static inline void sp_present(struct sp_pool *p)
{
    unsigned next=p->front<SP_COUNT?(p->front+1)%SP_COUNT:0;
    p->api.show(p->surfaces[next].overlay);p->front=next;
}
#endif
