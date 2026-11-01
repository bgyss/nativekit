/*
 * rr_metal_ext.h: the Metal backend's device, frame and target, for a
 * console's own drawing on the GPU (its compositor, its own pictures):
 * Objective-C, compiled with -fobjc-arc.
 */
#ifndef NK_RR_METAL_EXT_H
#define NK_RR_METAL_EXT_H

#import <Metal/Metal.h>

id<MTLDevice> rr_metal_device(void);
id<MTLCommandQueue> rr_metal_queue(void);
int rr_metal_frame(void);                         /* this frame in flight's index, 0-2 */
int rr_metal_encoding(void);                      /* 1 between rr_begin and rr_present */
/* the target's render pass ended for a compute pass; rr_metal_compute_end
   ends it and goes on with the target's pass (its contents kept) */
id<MTLComputeCommandEncoder> rr_metal_compute_begin(void);
void rr_metal_compute_end(id<MTLComputeCommandEncoder> ce);
id<MTLRenderCommandEncoder> rr_metal_encoder(void);
/* a quad over target rectangle d in this frame's vertex buffer: its offset, -1 when full */
long rr_metal_quad(const int d[4]);
id<MTLBuffer> rr_metal_vertices(void);
id<MTLTexture> rr_metal_picture_texture(int w, int h);   /* this frame's picture texture */
void rr_metal_draw_picture(id<MTLTexture> t, int w, int h, const int d[4]);   /* one to one or sharp bilinear */
/* a pipeline of the shared vertex function (bvs) and fragment function fs from lib */
id<MTLRenderPipelineState> rr_metal_pipe(NSString *fs, id<MTLLibrary> lib, int blend);
extern NSString *const rr_metal_vertex_src;     /* BO and bvs, for a console's library */
void rr_metal_wait_idle(void);                    /* every frame in flight done */

#endif
