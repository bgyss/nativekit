/*
 * rr_metal.m: the renderer backend on Metal (macOS, iOS): the frame is
 * drawn into an offscreen target the size of the
 * window's drawable, then copied into the window's CAMetalLayer. Pictures
 * go on with the sharp bilinear shader (rr_size.h's weight, in the shader:
 * fwidth gives the texels to one target pixel); a console draws its own on
 * the GPU through rr_metal_ext.h. The shader source is
 * compiled at start-up, so the build needs no Metal toolchain step.
 */
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rr.h"
#include "rr_size.h"
#include "rr_metal_ext.h"
#include "host.h"

CAMetalLayer *host_metal_layer(void);
void host_metal_layer_fit(int *w, int *h);
/* without a window host (HOST=null) there is no layer: frames are drawn
   offscreen and read back, never shown */
__attribute__((weak)) CAMetalLayer *host_metal_layer(void) { return nil; }
__attribute__((weak)) void host_metal_layer_fit(int *w, int *h) { *w = *h = 0; }

/* the vertex function every pipeline shares, and its output */
NSString *const rr_metal_vertex_src = @
"struct BO { float4 pos [[position]]; float2 uv; };\n"
"vertex BO bvs(uint i [[vertex_id]], const device float4 *v [[buffer(0)]]) { BO o; o.pos = float4(v[i].xy, 0, 1); o.uv = v[i].zw; return o; }\n";

/* the backend's fragment functions */
static const char k_frag_src[] =
/* one to one */
"fragment float4 bfs(BO in [[stage_in]], texture2d<float> t [[texture(0)]]) {\n"
"  constexpr sampler s(filter::nearest, address::clamp_to_edge);\n"
"  return float4(t.sample(s, in.uv).rgb, 1); }\n"
/* sharp bilinear: whole texels at any scale, blended only across the one
   target pixel a texel edge falls in (rr_size.h rr_ref_image): the sample
   point from the target pixel's own place and the texels a target pixel
   (an interpolated coordinate and fwidth fall to either side of a texel's
   edge), the four texels blended as rr_mix does, across then down, each
   rounded (the sampler's own blend weighs in fewer bits): 2 off a channel
   at a hard edge otherwise */
"struct SU { float d0, d1, dx, dy; };\n"
"fragment float4 sfs(BO in [[stage_in]], constant SU &U [[buffer(0)]], texture2d<float> t [[texture(0)]]) {\n"
"  float2 ts = float2(t.get_width(), t.get_height()), dd = max(float2(U.dx, U.dy), float2(1e-5));\n"
"  float2 px = (in.pos.xy - float2(U.d0, U.d1)) * float2(U.dx, U.dy) - 0.5;\n"
"  float2 i = floor(px), f = px - i;\n"
"  f = clamp((f - 0.5) / dd + 0.5, 0.0, 1.0);\n"
"  int2 hi = int2(ts) - 1, a = clamp(int2(i), int2(0), hi), b = clamp(int2(i) + 1, int2(0), hi);\n"
"  float3 c00 = round(t.read(uint2(a.x, a.y)).rgb * 255.0), c01 = round(t.read(uint2(b.x, a.y)).rgb * 255.0);\n"
"  float3 c10 = round(t.read(uint2(a.x, b.y)).rgb * 255.0), c11 = round(t.read(uint2(b.x, b.y)).rgb * 255.0);\n"
"  float2 w = floor(f * 256.0 + 0.5);\n"
"  float3 top = c00 + floor(((c01 - c00) * w.x + 128.0) / 256.0), bot = c10 + floor(((c11 - c10) * w.x + 128.0) / 256.0);\n"
"  return float4((top + floor(((bot - top) * w.y + 128.0) / 256.0)) / 255.0, 1); }\n"
/* an overlay: bilinear, its alpha kept for the blend */
"fragment float4 ofs(BO in [[stage_in]], texture2d<float> t [[texture(0)]]) {\n"
"  constexpr sampler s(filter::linear, address::clamp_to_edge);\n"
"  return t.sample(s, in.uv); }\n";

static id<MTLDevice> s_dev;
static id<MTLCommandQueue> s_q;
static id<MTLRenderPipelineState> s_copy, s_sharp;
static id<MTLTexture> s_col;            /* the target */
static id<MTLTexture> s_pic[3];          /* each frame's picture (frames in flight) */
static int s_pi;
static id<MTLBuffer> s_vb[3];
static size_t s_vused;
static id<MTLCommandBuffer> s_cmd;
static id<MTLRenderCommandEncoder> s_enc;
static dispatch_semaphore_t s_inflight;
static int s_rw, s_rh;
static uint32_t s_clear;
static id<MTLRenderPipelineState> s_over;      /* overlays: blended */
static id<MTLTexture> s_otex[3][RR_OVERLAYS];  /* each frame in flight's overlays' images */
static int s_on;                                /* overlays this frame */
#define VB_SIZE (1u << 16)
static int (*s_open_hook)(void);

id<MTLRenderPipelineState> rr_metal_pipe(NSString *fs, id<MTLLibrary> lib, int blend)
{
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    NSError *err = nil;
    id<MTLRenderPipelineState> p;
    d.vertexFunction = [lib newFunctionWithName:@"bvs"];
    d.fragmentFunction = [lib newFunctionWithName:fs];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (blend) {
        d.colorAttachments[0].blendingEnabled = YES;
        d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorZero;
        d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    }
    p = [s_dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p)
        fprintf(stderr, "rr: metal pipeline %s: %s\n", [fs UTF8String], [[err description] UTF8String]);
    return p;
}

static id<MTLRenderPipelineState> make_pipe(NSString *fs, id<MTLLibrary> lib) { return rr_metal_pipe(fs, lib, 0); }

static void targets(int w, int h)
{
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                   width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    s_col = [s_dev newTextureWithDescriptor:td];
    s_rw = w;
    s_rh = h;
}

int rr_open(void)
{
    @autoreleasepool {
        NSError *err = nil;
        id<MTLLibrary> lib;
        CAMetalLayer *ml;
        int i, w, h;
        s_dev = MTLCreateSystemDefaultDevice();
        if (!s_dev)
            return 0;
        s_q = [s_dev newCommandQueue];
        lib = [s_dev newLibraryWithSource:[NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\n%@%s",
                                                                    rr_metal_vertex_src, k_frag_src]
                                options:nil error:&err];
        if (!lib) {
            fprintf(stderr, "rr: metal shader: %s\n", [[err description] UTF8String]);
            return 0;
        }
        s_copy = make_pipe(@"bfs", lib);
        s_sharp = make_pipe(@"sfs", lib);
        s_over = rr_metal_pipe(@"ofs", lib, 1);
        if (!s_copy || !s_sharp || !s_over)
            return 0;
        for (i = 0; i < 3; i++)
            s_vb[i] = [s_dev newBufferWithLength:VB_SIZE options:MTLResourceStorageModeShared];
        s_inflight = dispatch_semaphore_create(3);
        ml = host_metal_layer();
        if (ml) {
            [ml setDevice:s_dev];
            [ml setMaximumDrawableCount:3];
            host_metal_layer_fit(&w, &h);
        }
        if (!ml || w < 1 || h < 1)
            rr_headless_size(&w, &h);
        targets(w, h);
        return !s_open_hook || s_open_hook();
    }
}

void rr_set_open_hook(int (*fn)(void)) { s_open_hook = fn; }

void rr_close(void) {}

const char *rr_name(void) { return "metal"; }

void rr_target(int *w, int *h)
{
    CAMetalLayer *ml = host_metal_layer();
    if (ml && !s_cmd) {                    /* the next frame at the window's size */
        int fw, fh;
        host_metal_layer_fit(&fw, &fh);
        if (fw > 0 && fh > 0 && (fw != s_rw || fh != s_rh))
            targets(fw, fh);
    }
    *w = s_rw;
    *h = s_rh;
}

/* a quad over target rectangle d, uv 0..1, into this frame's vertex buffer:
   its offset, or -1 */
static long quad(const int d[4])
{
    static const int ix[6] = { 0, 1, 2, 2, 1, 3 };
    float c[4][4] = { { (float)d[0], (float)d[1], 0, 0 }, { (float)d[2], (float)d[1], 1, 0 },
                      { (float)d[0], (float)d[3], 0, 1 }, { (float)d[2], (float)d[3], 1, 1 } };
    float *q;
    size_t o = (s_vused + 15) & ~(size_t)15;
    int i;
    if (o + 6 * 16 > VB_SIZE)
        return -1;
    s_vused = o + 6 * 16;
    q = (float *)((char *)[s_vb[s_pi] contents] + o);
    for (i = 0; i < 6; i++) {
        q[4 * i + 0] = c[ix[i]][0] / s_rw * 2 - 1;
        q[4 * i + 1] = 1 - c[ix[i]][1] / s_rh * 2;
        q[4 * i + 2] = c[ix[i]][2];
        q[4 * i + 3] = c[ix[i]][3];
    }
    return (long)o;
}

void rr_begin(uint32_t argb)
{
    @autoreleasepool {
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        int w, h;
        rr_target(&w, &h);
        dispatch_semaphore_wait(s_inflight, DISPATCH_TIME_FOREVER);
        s_cmd = [s_q commandBuffer];
        {
            dispatch_semaphore_t sem = s_inflight;
            [s_cmd addCompletedHandler:^(id<MTLCommandBuffer> b) { (void)b; dispatch_semaphore_signal(sem); }];
        }
        s_pi = (s_pi + 1) % 3;
        s_vused = 0;
        s_on = 0;
        s_clear = argb;
        rp.colorAttachments[0].texture = s_col;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(((argb >> 16) & 0xFF) / 255.0, ((argb >> 8) & 0xFF) / 255.0,
                                                              (argb & 0xFF) / 255.0, 1);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        s_enc = [s_cmd renderCommandEncoderWithDescriptor:rp];
    }
}

/* this frame's picture, w x h */
static id<MTLTexture> pic_tex(int w, int h)
{
    id<MTLTexture> t = s_pic[s_pi];
    if (!t || (int)t.width != w || (int)t.height != h) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                       width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        t = s_pic[s_pi] = [s_dev newTextureWithDescriptor:td];
    }
    return t;
}

static void draw_pic(id<MTLTexture> t, int w, int h, const int d[4]);

void rr_image(const uint32_t *argb, int w, int h, const int d[4])
{
    @autoreleasepool {
        id<MTLTexture> t;
        if (!s_enc || w <= 0 || h <= 0 || d[2] <= d[0] || d[3] <= d[1])
            return;
        t = pic_tex(w, h);
        /* 0xAARRGGBB little-endian is BGRA in memory */
        [t replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h) mipmapLevel:0
               withBytes:argb bytesPerRow:(NSUInteger)w * 4];
        draw_pic(t, w, h, d);
    }
}

/* picture t (w x h) over target rectangle d: one to one, or sharp bilinear */
static void draw_pic(id<MTLTexture> t, int w, int h, const int d[4])
{
    {
        long off;
        if ((off = quad(d)) < 0)
            return;
        [s_enc setRenderPipelineState:d[2] - d[0] == w && d[3] - d[1] == h ? s_copy : s_sharp];
        [s_enc setVertexBuffer:s_vb[s_pi] offset:(NSUInteger)off atIndex:0];
        {
            /* where the target's pixels sample: its rectangle's corner, and
               texels a target pixel (rr_ref_image's dx, dy) */
            float su[4] = { (float)d[0], (float)d[1], (float)w / (float)(d[2] - d[0]), (float)h / (float)(d[3] - d[1]) };
            [s_enc setFragmentBytes:su length:sizeof su atIndex:0];
        }
        [s_enc setFragmentTexture:t atIndex:0];
        [s_enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
}

void rr_overlay(const uint32_t *argb, int w, int h, const float q[6])
{
    @autoreleasepool {
        static const int ix[6] = { 0, 1, 2, 2, 1, 3 };
        float c[4][4] = { { q[0], q[1], 0, 0 }, { q[2], q[3], 1, 0 }, { q[4], q[5], 0, 1 },
                          { q[2] + q[4] - q[0], q[3] + q[5] - q[1], 1, 1 } };
        id<MTLTexture> t;
        size_t o = (s_vused + 15) & ~(size_t)15;
        float *v;
        int i;
        if (!s_enc || w <= 0 || h <= 0 || s_on == RR_OVERLAYS || o + 6 * 16 > VB_SIZE)
            return;
        t = s_otex[s_pi][s_on];
        if (!t || (int)t.width != w || (int)t.height != h) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                           width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
            td.usage = MTLTextureUsageShaderRead;
            t = s_otex[s_pi][s_on] = [s_dev newTextureWithDescriptor:td];
        }
        s_on++;
        [t replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h) mipmapLevel:0
               withBytes:argb bytesPerRow:(NSUInteger)w * 4];
        s_vused = o + 6 * 16;
        v = (float *)((char *)[s_vb[s_pi] contents] + o);
        for (i = 0; i < 6; i++) {
            v[4 * i + 0] = c[ix[i]][0] / s_rw * 2 - 1;
            v[4 * i + 1] = 1 - c[ix[i]][1] / s_rh * 2;
            v[4 * i + 2] = c[ix[i]][2];
            v[4 * i + 3] = c[ix[i]][3];
        }
        [s_enc setRenderPipelineState:s_over];
        [s_enc setVertexBuffer:s_vb[s_pi] offset:o atIndex:0];
        [s_enc setFragmentTexture:t atIndex:0];
        [s_enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
}

void rr_present(void)
{
    @autoreleasepool {
        CAMetalLayer *ml = host_window_visible() ? host_metal_layer() : nil;
        if (!s_enc)
            return;
        [s_enc endEncoding];
        s_enc = nil;
        if (ml) {
            id<CAMetalDrawable> dr = [ml nextDrawable];
            if (dr && (int)[dr texture].width == s_rw && (int)[dr texture].height == s_rh) {
                id<MTLBlitCommandEncoder> b = [s_cmd blitCommandEncoder];
                [b copyFromTexture:s_col sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake((NSUInteger)s_rw, (NSUInteger)s_rh, 1)
                         toTexture:[dr texture] destinationSlice:0 destinationLevel:0
                 destinationOrigin:MTLOriginMake(0, 0, 0)];
                [b endEncoding];
                [s_cmd presentDrawable:dr];
            }
        }
        [s_cmd commit];
        s_cmd = nil;
    }
}

int rr_read(uint32_t *argb)
{
    @autoreleasepool {
        id<MTLCommandBuffer> c = [s_q commandBuffer];
        [c commit];
        [c waitUntilCompleted];                /* everything before it is done */
        [s_col getBytes:argb bytesPerRow:(NSUInteger)s_rw * 4
             fromRegion:MTLRegionMake2D(0, 0, (NSUInteger)s_rw, (NSUInteger)s_rh) mipmapLevel:0];
        return 1;
    }
}

/* rr_metal_ext.h: the device, the frame and the target, for a console's
   own drawing */
id<MTLDevice> rr_metal_device(void) { return s_dev; }

id<MTLCommandQueue> rr_metal_queue(void) { return s_q; }

int rr_metal_frame(void) { return s_pi; }

int rr_metal_encoding(void) { return s_enc != nil; }

id<MTLRenderCommandEncoder> rr_metal_encoder(void) { return s_enc; }

/* the target's pass ended, for a compute pass of the console's own */
id<MTLComputeCommandEncoder> rr_metal_compute_begin(void)
{
    [s_enc endEncoding];
    s_enc = nil;
    return [s_cmd computeCommandEncoder];
}

/* the compute pass ended, then the target's pass goes on */
void rr_metal_compute_end(id<MTLComputeCommandEncoder> ce)
{
    MTLRenderPassDescriptor *rp;
    [ce endEncoding];
    rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = s_col;
    rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    s_enc = [s_cmd renderCommandEncoderWithDescriptor:rp];
}

long rr_metal_quad(const int d[4]) { return quad(d); }

id<MTLBuffer> rr_metal_vertices(void) { return s_vb[s_pi]; }

id<MTLTexture> rr_metal_picture_texture(int w, int h) { return pic_tex(w, h); }

void rr_metal_draw_picture(id<MTLTexture> t, int w, int h, const int d[4]) { draw_pic(t, w, h, d); }

/* an empty command buffer committed and waited on: everything before it done */
void rr_metal_wait_idle(void)
{
    id<MTLCommandBuffer> c = [s_q commandBuffer];
    [c commit];
    [c waitUntilCompleted];
}
