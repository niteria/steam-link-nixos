/* SPDX-License-Identifier: MIT */
#ifndef STEAMLINK_CONSOLE_PIXELS_H
#define STEAMLINK_CONSOLE_PIXELS_H
#include <stddef.h>
#include <stdint.h>
struct sfc_geometry {
    uint32_t width, height, pitch, xoffset, yoffset;
    uint32_t red, green, blue;
};
static inline int sfc_valid(const struct sfc_geometry *g, size_t source_bytes,
                     uint32_t destination_pitch, size_t destination_bytes)
{
    if (!g || !g->width || g->width > 1920 || !g->height || g->height > 1080 ||
        !source_bytes || source_bytes > 16U*1024*1024 || (g->pitch & 3) ||
        (destination_pitch & 3) || g->red > 24 || g->green > 24 || g->blue > 24 ||
        g->red == g->green || g->red == g->blue || g->green == g->blue)
        return 0;
    uint64_t right = ((uint64_t)g->xoffset + g->width) * 4;
    uint64_t end = ((uint64_t)g->yoffset + g->height - 1) * g->pitch + right;
    uint64_t destination_end = (uint64_t)(g->height - 1)*destination_pitch + (uint64_t)g->width*4;
    return right <= g->pitch && end <= source_bytes &&
           destination_pitch >= g->width*4 && destination_pitch <= g->width*4+4096 &&
           destination_end <= destination_bytes;
}
static inline int sfc_copy(uint32_t *destination, size_t destination_bytes, uint32_t destination_pitch,
                    const volatile uint32_t *source, size_t source_bytes, const struct sfc_geometry *g)
{
    if (!destination || !source || ((uintptr_t)source & 3) || ((uintptr_t)destination & 3) ||
        !sfc_valid(g, source_bytes, destination_pitch, destination_bytes)) return 0;
    for (uint32_t y=0; y<g->height; y++) {
        const volatile uint32_t *row = source + (size_t)(y+g->yoffset)*(g->pitch/4) + g->xoffset;
        uint32_t *out = destination + (size_t)y*(destination_pitch/4);
        for (uint32_t x=0; x<g->width; x++) {
            uint32_t pixel = row[x];
            /* fbcon does not promise opaque alpha. Valve's overlay does. */
            out[x] = 0xff000000U | (((pixel>>g->red)&255)<<16) |
                     (((pixel>>g->green)&255)<<8) | ((pixel>>g->blue)&255);
        }
    }
    return 1;
}
#endif
