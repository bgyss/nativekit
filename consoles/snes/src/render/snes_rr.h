/*
 * snes_rr.h: the SNES's calls on the renderer backends (rr.h is the
 * generic part): its compositor and Mode 7.
 */
#ifndef NSHVC_SNES_RR_H
#define NSHVC_SNES_RR_H

#include "rr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the console's picture, w x h, drawn by the backend's compositor from the
   frame's description (ppu.c ppu_compose, render/compose_body.h: words of
   it), then shown over target rectangle d as rr_image shows a picture;
   0 when the backend has none (the caller draws the picture and calls
   rr_image) */
int rr_compose(const uint32_t *desc, size_t words, int w, int h, const int d[4]);
/* an interlaced frame's woven picture, ww x wh, by the compositor: desc
   holds the finished frame's description and the one before's, each row
   parity's rows from the one at word base0 (even rows) or base1 (odd)
   (ppu.h ppu_woven) */
int rr_compose_woven(const uint32_t *desc, size_t words, int ww, int wh, uint32_t base0, uint32_t base1,
                     const int d[4]);

/* A frame with Mode 7 or a layer's canvas presented over the target's
   rectangle d (rr_mode7.h):
   its record m and the samples s rr_m7_tables made at d's size. Exactly
   rr_ref_mode7's drawing, but that the blends round as the GPU's (within 1
   a channel, as rr_image's). */
struct RrMode7;
struct RrM7Samples;
void rr_mode7(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s);
/* a frame presenting Mode 7 or a canvas: its picture and records drawn by
   the backend's compositor from its description (ppu.c ppu_compose_m7),
   then presented as rr_mode7 presents m (m's tables, not its records or
   picture); 0 when the backend has no compositor */
int rr_compose_mode7(const uint32_t *desc, size_t words, const struct RrMode7 *m, const int d[4],
                     const struct RrM7Samples *s);

/* the GPU backends' open hooks (rr_set_open_hook), set by nshvc_main:
   the compositor's and Mode 7's programs made when the backend opens */
int snes_metal_open(void);
int snes_gl_open(void);

#ifdef __cplusplus
}
#endif
#endif
