/*
 * snes_metal.m: the SNES's picture on the Metal backend (the kit's
 * rr_metal.m, through rr_metal_ext.h): its compositor (compose_body.h) in
 * compute kernels, and a frame with Mode 7 with rr_mode7.h's arithmetic in
 * a fragment shader. The shader source is compiled at start-up, in the
 * backend's open hook, so the build needs no Metal toolchain step.
 */
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>

#include "snes_rr.h"
#include "rr_mode7.h"
#include "rr_metal_ext.h"

#include "rr_compose_src.h"

/* the compositor (compose_body.h): its functions members of a structure
   that holds the frame's description, a thread a picture pixel */
static const char k_compose_pre[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "#define CP_FN\n"
    "#define CP_VAR\n"
    "#define U32 uint\n"
    "#define I32 int\n"
    "#define U_(x) uint(x)\n"
    "#define I_(x) int(x)\n"
    "#define FD_(i) fd[cp_fdb + (i)]\n"
    "struct CP {\n"
    "device const uint *fd;\n";
static const char k_compose_post[] =
    "};\n"
    /* wp: the woven picture's width (0: none) and its descriptions' first words */
    "kernel void cpk(device const uint *fd [[buffer(0)]], texture2d<float, access::write> o [[texture(0)]],\n"
    "                constant uint4 &wp [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {\n"
    "  CP c; uint p;\n"
    "  c.fd = fd;\n"
    "  c.cp_fdb = 0u;\n"
    "  if (g.x >= o.get_width() || g.y >= o.get_height()) return;\n"
    "  p = wp.x == 0u ? c.cp_pixel(g.x, g.y) : c.cp_woven(g.x, g.y, wp.x, wp.y, wp.z);\n"
    "  o.write(float4(float((p >> 16) & 255u), float((p >> 8) & 255u), float(p & 255u), 255.0) / 255.0, g); }\n"
    /* a frame presenting Mode 7 or a canvas: its picture and records */
    "kernel void cprk(device const uint *fd [[buffer(0)]], device uint4 *rec [[buffer(1)]], device uint *pic [[buffer(2)]],\n"
    "                 uint2 g [[thread_position_in_grid]]) {\n"
    "  CP c; uint i;\n"
    "  c.fd = fd;\n"
    "  c.cp_fdb = 0u;\n"
    "  if (g.x >= fd[CP_PW] || g.y >= fd[CP_PH]) return;\n"
    "  i = g.y * fd[CP_PW] + g.x;\n"
    "  pic[i] = c.cp_pixel(g.x, g.y);\n"
    "  c.cp_record(g.x, g.y);\n"
    "  rec[i] = uint4(c.cp_r0, c.cp_r1, c.cp_r2, c.cp_r3); }\n";

/* the Mode 7 library's own source: rr_metal_vertex_src (BO, bvs) goes
   before it */
static const char k_m7_src[] =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
/* a frame with Mode 7 presented (rr_mode7.h, the same arithmetic) */
"struct M7U { int pw, ph, d0, d1, step_u, step_v, du[28], dv[28], cpp, left, cvk, cvlen, cvw, cvh; };\n"
"static uint m7_direct(uint c) { return (c & 7u) << 2 | ((c & 0x38u) >> 1) << 5 | ((c & 0xC0u) >> 3) << 10; }\n"
"static uint m7_finish(uint mc, uint ml, uint sc, uint sl, uint fl, uint adsub, uint wsel, uint fixed, uint bright) {\n"
"  bool clip = (fl & 0x80u) != 0, prevent = (fl & 0x100u) != 0;\n"
"  uint bit = ml == 5 ? 5 : ml == 6 ? 8 : ml, c = clip ? 0 : mc, r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;\n"
"  if (bit < 8 && (adsub >> bit & 1) && !prevent) {\n"
"    uint o; bool halve = (adsub & 0x40) != 0 && !clip;\n"
"    if (wsel & 2) { if (sl == 5) { o = fixed; halve = false; } else o = sc; } else o = fixed;\n"
"    if (adsub & 0x80) {\n"
"      int rr = int(r) - int(o & 31), gg = int(g) - int((o >> 5) & 31), bb = int(b) - int((o >> 10) & 31);\n"
"      r = rr < 0 ? 0 : uint(rr); g = gg < 0 ? 0 : uint(gg); b = bb < 0 ? 0 : uint(bb);\n"
"      if (halve) { r >>= 1; g >>= 1; b >>= 1; }\n"
"    } else {\n"
"      r += o & 31; g += (o >> 5) & 31; b += (o >> 10) & 31;\n"
"      if (halve) { r >>= 1; g >>= 1; b >>= 1; }\n"
"      r = min(r, 31u); g = min(g, 31u); b = min(b, 31u);\n"
"    }\n"
"  }\n"
"  if (bright != 15) { r = ((r * bright + 7u) * 4370u) >> 16; g = ((g * bright + 7u) * 4370u) >> 16; b = ((b * bright + 7u) * 4370u) >> 16; }\n"
"  return 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2); }\n"
"static uint m7_ptexel(texture2d_array<uint> plane, uint layer, const device ushort *v, uint sel, int px, int py) {\n"
"  if ((px | py) & ~0x3FF) {\n"
"    if ((sel & 0xC0) == 0x80) return 0;\n"
"    if ((sel & 0xC0) == 0xC0) return uint(v[uint(py & 7) * 8 + uint(px & 7)]) >> 8;\n"
"  }\n"
"  return plane.read(uint2(px & 1023, py & 1023), layer).r; }\n"
"static uint m7_mix(uint a, uint b, float t) {\n"
"  uint o = 0xFF000000u;\n"
"  for (uint s = 0; s < 24; s += 8) { float ca = float((a >> s) & 0xFF), cb = float((b >> s) & 0xFF); o |= uint(int(ca + (cb - ca) * t + 0.5)) << s; }\n"
"  return o; }\n"
"static int m7_fdiv(int a, int b) {\n"
"  int q = int(floor(float(a) / float(b)));\n"
"  if (q * b > a) q--; else if ((q + 1) * b <= a) q++;\n"
"  return q; }\n"
/* the next texel's part of the footprint past its edge (rr_mode7.h rr_m7_edge) */
"static inline __attribute__((always_inline)) int m7_edge(int f, int sa, int sb) {\n"
"  int A = max(sa, sb), B = min(sa, sb), T = 2 * (128 - f), cov;\n"
"  if (A == 0) return T < 0 ? 256 : T > 0 ? 0 : 128;\n"
"  if (T <= -(A + B)) cov = 0;\n"
"  else if (B > 0 && T <= -(A - B)) cov = m7_fdiv(256 * (T + A + B) * (T + A + B), 8 * A * B);\n"
"  else if (T < A - B || B == 0) cov = 128 + m7_fdiv(128 * T, A);\n"
"  else if (T < A + B) cov = 256 - m7_fdiv(256 * (A + B - T) * (A + B - T), 8 * A * B);\n"
"  else cov = 256;\n"
"  return 256 - clamp(cov, 0, 256); }\n"
/* a picture pixel's colour with Mode 7's pixel byte pix (rr_m7_compose) */
"static inline __attribute__((always_inline)) uint m7_compose(uint4 p, const device int *g, const device ushort *pl, uint pix) {\n"
"  uint fl = p.z & 0xFFFFu, pix1 = (fl & 0x200u) ? (p.w & 0xFF) : pix, pix2 = (fl & 0x400u) ? ((p.w >> 8) & 0xFF) : pix;\n"
"  uint mc = p.x & 0xFFFF, sc = p.x >> 16, mz = p.y & 0xFF, sz = (p.y >> 8) & 0xFF, ml = (p.y >> 16) & 0xFF, sl = p.y >> 24;\n"
"  if (pix1) {\n"
"    uint col = ((uint(g[4]) >> 8) & 1) ? m7_direct(pix1) : uint(pl[pix1]);\n"
"    if ((fl & 0x08u) && 3 > mz) { mc = col; mz = 3; ml = 0; }\n"
"    if ((fl & 0x10u) && 3 > sz) { sc = col; sz = 3; sl = 0; }\n"
"  }\n"
"  if (pix2 & 0x7F) {\n"
"    uint col = pl[pix2 & 0x7F], z = (pix2 & 0x80) ? 5 : 1;\n"
"    if ((fl & 0x20u) && z > mz) { mc = col; mz = z; ml = 1; }\n"
"    if ((fl & 0x40u) && z > sz) { sc = col; sz = z; sl = 1; }\n"
"  }\n"
"  if (fl & 4u) { uint t = mc; mc = sl == 5 ? uint(pl[0]) : sc; sc = t; t = ml; ml = sl; sl = t; }\n"
"  return m7_finish(mc, ml, sc, sl, fl, (uint(g[4]) >> 16) & 0xFF, uint(g[4]) >> 24, uint(g[5]) & 0xFFFF, (uint(g[5]) >> 16) & 0xFF); }\n"
/* a canvas pixel's colour with the canvas's texel pix (rr_cv_compose) */
"static uint cv_direct(uint c, uint pal) {\n"
"  return ((c & 7u) << 2 | (pal & 1u) << 1) | ((c & 0x38u) >> 1 | (pal & 2u)) << 5 | ((c & 0xC0u) >> 3 | (pal & 4u)) << 10; }\n"
"static uint cv_compose(uint4 p, const device int *g, const device ushort *pl, uint pix) {\n"
"  uint fl = p.z & 0xFFFFu, w3 = p.w, bpp = uint(g[7]) & 0xFF, mode = (uint(g[7]) >> 8) & 7;\n"
"  uint pal = (w3 >> 22) & 7, z = (w3 >> 25) & 15, n = (w3 >> 29) & 3;\n"
"  uint mc = p.x & 0xFFFF, sc = p.x >> 16, mz = p.y & 0xFF, sz = (p.y >> 8) & 0xFF, ml = (p.y >> 16) & 0xFF, sl = p.y >> 24;\n"
"  pix &= (1u << bpp) - 1u;\n"
"  if (pix) {\n"
"    uint col = bpp == 8 ? (((uint(g[4]) >> 8) & 1) ? cv_direct(pix, pal) : uint(pl[pix])) :\n"
"               bpp == 4 ? uint(pl[pal * 16 + pix]) : uint(pl[(mode == 0 ? n * 32 : 0) + pal * 4 + pix]);\n"
"    if ((fl & 0x08u) && z > mz) { mc = col; mz = z; ml = n; }\n"
"    if ((fl & 0x10u) && z > sz) { sc = col; sz = z; sl = n; }\n"
"  }\n"
"  return m7_finish(mc, ml, sc, sl, fl, (uint(g[4]) >> 16) & 0xFF, uint(g[4]) >> 24, uint(g[5]) & 0xFFFF, (uint(g[5]) >> 16) & 0xFF); }\n"
/* canvas pixel (i, r) over the output pixel centred at u, v: the canvas's
   texel under the centre (rr_cv_q) */
"static uint cv_q(constant M7U &U, uint4 p, const device int *g, const device ushort *pl, const device uchar *cv, int i, int r, int u, int v) {\n"
"  uint w3 = p.w, k = uint(U.cvk);\n"
"  int h = i / U.cpp - U.left, fu = clamp(u - 1024 * h + 512, 0, 1023), fv = clamp(v - 1024 * r + 512, 0, 1023);\n"
"  uint sx = (uint(fu) * k) >> 10, sy = (uint(fv) * k) >> 10;\n"
"  if ((w3 >> 20) & 1) sx = k - 1 - sx;\n"
"  if ((w3 >> 21) & 1) sy = k - 1 - sy;\n"
"  if ((w3 >> 31) != 0u) {\n"
"    uint ex = uint(i / U.cpp) * k + sx, ey = uint(r) * k + sy;\n"
"    return cv_compose(p, g, pl, ex < uint(U.cvw) && ey < uint(U.cvh) ? uint(cv[ey * uint(U.cvw) + ex]) : 0u); }\n"
"  uint at = (w3 & 0x3FFF) * 64u * k * k + (((w3 >> 17) & 7) * k + sy) * 8u * k + ((w3 >> 14) & 7) * k + sx;\n"
"  return cv_compose(p, g, pl, at < uint(U.cvlen) ? uint(cv[at]) : 0u); }\n"
/* picture pixel (i, r) over the output pixel centred at u, v: its Mode 7
   plane filtered by the footprint (rr_m7_q); one loop over the texels
   it takes, each composed once (a byte as the last one's: its colour) */
"static inline __attribute__((always_inline)) uint m7_q(constant M7U &U, const device uint4 *P, const device int *S, const device ushort *pal,\n"
"                 const device ushort *vram, const device uint *pic, texture2d_array<uint> plane, const device uchar *cv, int i, int r, int u, int v) {\n"
"  i = clamp(i, 0, U.pw - 1); r = clamp(r, 0, U.ph - 1);\n"
"  uint4 p = P[r * U.pw + i]; uint fl = p.z & 0xFFFFu;\n"
"  if (fl & 1u) return pic[r * U.pw + i];\n"
"  if (fl & 2u) return 0xFF000000u;\n"
"  const device int *g = S + (p.z >> 16) * 8;\n"
"  const device ushort *pl = pal + uint(g[6]) * 256u;\n"
"  if (fl & 0x800u) return cv_q(U, p, g, pl, cv, i, r, u, v);\n"
"  bool look = (fl & 0x78u) && (fl & 0x600u) != 0x600u;\n"
"  uint sel = uint(g[4]) & 0xFF;\n"
"  const device ushort *vr = vram + (uint(g[5]) >> 24) * 0x8000u;\n"
"  int a = int(short(g[2] & 0xFFFF)), b = g[2] >> 16, c = int(short(g[3] & 0xFFFF)), d = g[3] >> 16;\n"
"  int uu = u, ee = v - 1024 * r, sx = 1, sy = 1;\n"
"  if (sel & 1) { uu = 255 * 1024 - uu; sx = -1; }\n"
"  if (sel & 2) { ee = -ee; sy = -1; }\n"
"  int hx = g[0] + a * (uu >> 10) + b * (ee >> 10), lx = a * (uu & 1023) + b * (ee & 1023);\n"
"  int hy = g[1] + c * (uu >> 10) + d * (ee >> 10), ly = c * (uu & 1023) + d * (ee & 1023);\n"
"  int ax = (abs(a) * U.step_u) >> 10, bx = (abs(b) * U.step_v) >> 10, ay = (abs(c) * U.step_u) >> 10, by = (abs(d) * U.step_v) >> 10;\n"
"  int w = max(ax + bx, ay + by);\n"
"  int n, base, ix = 0, iy = 0, ex = 0, ey = 0;\n"
"  float fx = 0, fy = 0;\n"
"  if (!look) { n = 1; base = -2; }\n"
"  else if (w <= 256) {\n"
"    int qx = hx + (lx >> 10) - 128, qy = hy + (ly >> 10) - 128;\n"
"    ix = qx >> 8; iy = qy >> 8; ex = m7_edge(qx & 255, ax, bx); ey = m7_edge(qy & 255, ay, by);\n"
"    if (ex == 256) { ix++; ex = 0; }\n"
"    if (ey == 256) { iy++; ey = 0; }\n"
"    fx = float(ex) / 256.0; fy = float(ey) / 256.0;\n"
"    n = (ex ? 2 : 1) * (ey ? 2 : 1); base = -1;\n"
"  } else {\n"
"    n = w <= 512 ? 4 : w <= 1024 ? 8 : 16; base = n == 4 ? 0 : n == 8 ? 4 : 12;\n"
"  }\n"
"  uint last = 0x100u, col = 0, cur = 0, top = 0, sr = 0, sg = 0, sb = 0;\n"
"  for (int k = 0; k < n; k++) {\n"
"    int px, py, ci = 0, cj = 0;\n"
"    if (base < 0) { ci = ex ? (k & 1) : 0; cj = ex ? (k >> 1) : k; px = ix + ci; py = iy + cj; }\n"
"    else {\n"
"      int eu = sx * U.du[base + k], ev = sy * U.dv[base + k];\n"
"      px = (hx + ((lx + a * eu + b * ev) >> 10)) >> 8; py = (hy + ((ly + c * eu + d * ev) >> 10)) >> 8;\n"
"    }\n"
"    uint pix = base == -2 ? 0u : m7_ptexel(plane, uint(g[5]) >> 24, vr, sel, px, py);\n"
"    if (pix != last) { col = m7_compose(p, g, pl, pix); last = pix; }\n"
"    if (base >= 0) { sb += col & 0xFF; sg += (col >> 8) & 0xFF; sr += (col >> 16) & 0xFF; continue; }\n"
"    cur = ci ? m7_mix(cur, col, fx) : col;\n"
"    if (!ex || ci) top = cj ? m7_mix(top, cur, fy) : cur;\n"
"  }\n"
"  if (base < 0) return top;\n"
"  uint sh = n == 4 ? 2 : n == 8 ? 3 : 4, h = uint(n) / 2;\n"
"  return 0xFF000000u | ((sr + h) >> sh) << 16 | ((sg + h) >> sh) << 8 | ((sb + h) >> sh); }\n"
/* picture pixels (i, r) and (i + 1, r) (two) over the output pixel centred
   at u, v: their Mode 7 plane filtered by the footprint (rr_m7_q). Both
   are looked up at the same position, so on one line state their texels
   are the same: the plane is read once and its texels composed for each
   (once where their records are the same), each texel's byte composed
   once (a byte as the last one's: its colour). */
"static inline __attribute__((always_inline)) uint2 m7_row(constant M7U &U, const device uint4 *P, const device int *S, const device ushort *pal,\n"
"                 const device ushort *vram, const device uint *pic, texture2d_array<uint> plane, const device uchar *cv, int i, bool two, int r, int u, int v) {\n"
"  r = clamp(r, 0, U.ph - 1);\n"
"  int i0 = clamp(i, 0, U.pw - 1), i1 = clamp(i + 1, 0, U.pw - 1);\n"
"  uint4 p0 = P[r * U.pw + i0], p1 = two ? P[r * U.pw + i1] : p0;\n"
"  uint fl0 = p0.z & 0xFFFFu, fl1 = p1.z & 0xFFFFu;\n"
/* a canvas pixel in the pair: each pixel on its own */
"  if ((fl0 | fl1) & 0x800u) {\n"
"    uint a = m7_q(U, P, S, pal, vram, pic, plane, cv, i, r, u, v);\n"
"    return uint2(a, two ? m7_q(U, P, S, pal, vram, pic, plane, cv, i + 1, r, u, v) : a); }\n"
/* the same record, the same colour: but a picture pixel's (not on such a
   line) is its own */
"  bool same = all(p0 == p1) && !(fl0 & 1u);\n"
/* a picture pixel not on a Mode 7 line, or a hires line's first half pixel */
"  uint k0 = (fl0 & 1u) ? pic[r * U.pw + i0] : 0xFF000000u, k1 = (fl1 & 1u) ? pic[r * U.pw + i1] : 0xFF000000u;\n"
"  bool m0 = !(fl0 & 3u), m1 = two && !(fl1 & 3u) && !same;\n"
"  if (!m0 && !m1) return uint2(k0, same ? k0 : k1);\n"
"  uint4 pp = m0 ? p0 : p1;\n"
"  if (m0 && m1 && (p0.z >> 16) != (p1.z >> 16)) m1 = false;      /* another state: below */\n"
"  const device int *g = S + (pp.z >> 16) * 8;\n"
"  const device ushort *pl = pal + uint(g[6]) * 256u;\n"
"  bool l0 = m0 && (fl0 & 0x78u) && (fl0 & 0x600u) != 0x600u, l1 = m1 && (fl1 & 0x78u) && (fl1 & 0x600u) != 0x600u;\n"
"  uint sel = uint(g[4]) & 0xFF;\n"
"  const device ushort *vr = vram + (uint(g[5]) >> 24) * 0x8000u;\n"
"  int a = int(short(g[2] & 0xFFFF)), b = g[2] >> 16, c = int(short(g[3] & 0xFFFF)), d = g[3] >> 16;\n"
"  int uu = u, ee = v - 1024 * r, sx = 1, sy = 1;\n"
"  if (sel & 1) { uu = 255 * 1024 - uu; sx = -1; }\n"
"  if (sel & 2) { ee = -ee; sy = -1; }\n"
"  int hx = g[0] + a * (uu >> 10) + b * (ee >> 10), lx = a * (uu & 1023) + b * (ee & 1023);\n"
"  int hy = g[1] + c * (uu >> 10) + d * (ee >> 10), ly = c * (uu & 1023) + d * (ee & 1023);\n"
"  int ax = (abs(a) * U.step_u) >> 10, bx = (abs(b) * U.step_v) >> 10, ay = (abs(c) * U.step_u) >> 10, by = (abs(d) * U.step_v) >> 10;\n"
"  int w = max(ax + bx, ay + by);\n"
"  int n, base, ix = 0, iy = 0, ex = 0, ey = 0;\n"
"  float fx = 0, fy = 0;\n"
"  if (!l0 && !l1) { n = 1; base = -2; }\n"
"  else if (w <= 256) {\n"
"    int qx = hx + (lx >> 10) - 128, qy = hy + (ly >> 10) - 128;\n"
"    ix = qx >> 8; iy = qy >> 8; ex = m7_edge(qx & 255, ax, bx); ey = m7_edge(qy & 255, ay, by);\n"
"    if (ex == 256) { ix++; ex = 0; }\n"
"    if (ey == 256) { iy++; ey = 0; }\n"
"    fx = float(ex) / 256.0; fy = float(ey) / 256.0;\n"
"    n = (ex ? 2 : 1) * (ey ? 2 : 1); base = -1;\n"
"  } else {\n"
"    n = w <= 512 ? 4 : w <= 1024 ? 8 : 16; base = n == 4 ? 0 : n == 8 ? 4 : 12;\n"
"  }\n"
"  uint last = 0x100u, c0 = 0, c1 = 0, cur0 = 0, cur1 = 0, top0 = 0, top1 = 0;\n"
"  uint3 s0 = 0, s1 = 0;\n"
"  for (int k = 0; k < n; k++) {\n"
"    int px, py, ci = 0, cj = 0;\n"
"    if (base < 0) { ci = ex ? (k & 1) : 0; cj = ex ? (k >> 1) : k; px = ix + ci; py = iy + cj; }\n"
"    else {\n"
"      int eu = sx * U.du[base + k], ev = sy * U.dv[base + k];\n"
"      px = (hx + ((lx + a * eu + b * ev) >> 10)) >> 8; py = (hy + ((ly + c * eu + d * ev) >> 10)) >> 8;\n"
"    }\n"
"    uint pix = base == -2 ? 0u : m7_ptexel(plane, uint(g[5]) >> 24, vr, sel, px, py);\n"
"    if (pix != last) {\n"
"      last = pix;\n"
"      if (m0) c0 = m7_compose(p0, g, pl, l0 ? pix : 0u);\n"
"      if (m1) c1 = m7_compose(p1, g, pl, l1 ? pix : 0u);\n"
"    }\n"
"    if (base >= 0) { s0 += uint3((c0 >> 16) & 0xFF, (c0 >> 8) & 0xFF, c0 & 0xFF); s1 += uint3((c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF); continue; }\n"
"    cur0 = ci ? m7_mix(cur0, c0, fx) : c0; cur1 = ci ? m7_mix(cur1, c1, fx) : c1;\n"
"    if (!ex || ci) { top0 = cj ? m7_mix(top0, cur0, fy) : cur0; top1 = cj ? m7_mix(top1, cur1, fy) : cur1; }\n"
"  }\n"
"  if (base >= 0) {\n"
"    uint sh = n == 4 ? 2 : n == 8 ? 3 : 4; uint3 h = uint3(uint(n) / 2);\n"
"    s0 = (s0 + h) >> sh; s1 = (s1 + h) >> sh;\n"
"    top0 = 0xFF000000u | s0.x << 16 | s0.y << 8 | s0.z; top1 = 0xFF000000u | s1.x << 16 | s1.y << 8 | s1.z;\n"
"  }\n"
"  uint r0 = m0 ? top0 : k0;\n"
"  if (!two) return uint2(r0, r0);\n"
"  if (same) return uint2(r0, r0);\n"
"  if (m1) return uint2(r0, top1);\n"
"  if (!(fl1 & 3u)) return uint2(r0, m7_q(U, P, S, pal, vram, pic, plane, cv, i + 1, r, u, v));\n"
"  return uint2(r0, k1); }\n"
"static uint m7_pic(constant M7U &U, const device uint *pic, int x, int y) { return pic[clamp(y, 0, U.ph - 1) * U.pw + clamp(x, 0, U.pw - 1)]; }\n"
/* an output pixel: the picture pixels under it blended with sharp
   bilinear (rr_m7_pixel), a row of them at a time */
"fragment float4 m7fs(BO in [[stage_in]], constant M7U &U [[buffer(0)]], const device uint4 *P [[buffer(1)]],\n"
"                     const device int *S [[buffer(2)]], const device ushort *pal [[buffer(3)]], const device ushort *vram [[buffer(4)]],\n"
"                     const device uint *pic [[buffer(5)]], const device int *cols [[buffer(6)]], const device int *rows [[buffer(7)]],\n"
"                     const device uchar *cv [[buffer(8)]], texture2d_array<uint> plane [[texture(0)]]) {\n"
"  int X = int(in.pos.x) - U.d0, Y = int(in.pos.y) - U.d1;\n"
"  const device int *col = cols + X * 3, *row = rows + Y * 4;\n"
"  int tx = col[0], ty = row[0], u = col[2], v = row[3];\n"
"  float fx = as_type<float>(col[1]), fy = as_type<float>(row[1]);\n"
"  bool m7 = row[2] != 0, two = fx > 0;\n"
"  int nr = fy > 0 ? 2 : 1;\n"
"  uint top = 0;\n"
"  for (int j = 0; j < nr; j++) {\n"
"    uint cur;\n"
"    if (m7) { uint2 q = m7_row(U, P, S, pal, vram, pic, plane, cv, tx, two, ty + j, u, v); cur = two ? m7_mix(q.x, q.y, fx) : q.x; }\n"
"    else cur = two ? m7_mix(m7_pic(U, pic, tx, ty + j), m7_pic(U, pic, tx + 1, ty + j), fx) : m7_pic(U, pic, tx, ty + j);\n"
"    top = j ? m7_mix(top, cur, fy) : cur;\n"
"  }\n"
"  return float4(float((top >> 16) & 0xFF), float((top >> 8) & 0xFF), float(top & 0xFF), 255.0) / 255.0; }\n";

static id<MTLRenderPipelineState> s_m7;
static id<MTLComputePipelineState> s_cp, s_cpr; /* the compositor: a picture; a picture and its records */
static id<MTLBuffer> s_recbuf[3], s_picbuf[3];  /* each frame in flight's records and picture */
static id<MTLBuffer> s_m7buf[3][8];             /* each frame in flight's Mode 7 tables (m7_draw) */
#define CP_CV_WORD 13                           /* compose_body.h CP_CV: the canvas's first word */

static id<MTLBuffer> s_cpbuf[3];              /* each frame in flight's description */
static id<MTLTexture> s_plane;             /* the Mode 7 planes (rr_mode7.h), as last uploaded */
static uint32_t s_plane_gen;
static int s_planes;

/* the backend's open hook (rr_set_open_hook), at rr_open's end: the Mode 7
   shader and the compositor compiled; 0 (the backend not open) when they
   do not */
static int snes_metal_open(void)
{
    NSError *err = nil;
    id<MTLLibrary> lib;
    lib = [rr_metal_device() newLibraryWithSource:[NSString stringWithFormat:@"%@%s", rr_metal_vertex_src, k_m7_src]
                                          options:nil error:&err];
    if (!lib) {
        fprintf(stderr, "rr: metal shader: %s\n", [[err description] UTF8String]);
        return 0;
    }
    s_m7 = rr_metal_pipe(@"m7fs", lib, 0);
    if (!s_m7)
        return 0;
    {
        NSString *src = [NSString stringWithFormat:@"%s%s%s", k_compose_pre, k_compose_body, k_compose_post];
        id<MTLLibrary> cl = [rr_metal_device() newLibraryWithSource:src options:nil error:&err];
        id<MTLFunction> fn = cl ? [cl newFunctionWithName:@"cpk"] : nil;
        if (!fn) {
            fprintf(stderr, "rr: metal compositor: %s\n", [[err description] UTF8String]);
            return 0;
        }
        s_cp = [rr_metal_device() newComputePipelineStateWithFunction:fn error:&err];
        fn = [cl newFunctionWithName:@"cprk"];
        s_cpr = fn ? [rr_metal_device() newComputePipelineStateWithFunction:fn error:&err] : nil;
        if (!s_cp || !s_cpr)
            return 0;
    }
    return 1;
}

/* registered before main: the backend opens with it */
__attribute__((constructor)) static void snes_metal_init(void) { rr_set_open_hook(snes_metal_open); }

static int compose(const uint32_t *desc, size_t words, int w, int h, const uint32_t wp[4], const int d[4]);

int rr_compose(const uint32_t *desc, size_t words, int w, int h, const int d[4])
{
    static const uint32_t none[4] = { 0, 0, 0, 0 };
    return compose(desc, words, w, h, none, d);
}

int rr_compose_woven(const uint32_t *desc, size_t words, int ww, int wh, uint32_t base0, uint32_t base1,
                     const int d[4])
{
    uint32_t wp[4] = { (uint32_t)ww, base0, base1, 0 };
    return compose(desc, words, ww, wh, wp, d);
}

static int compose(const uint32_t *desc, size_t words, int w, int h, const uint32_t wp[4], const int d[4])
{
    @autoreleasepool {
        id<MTLTexture> t;
        id<MTLComputeCommandEncoder> ce;
        if (!s_cp || !rr_metal_encoding() || w <= 0 || h <= 0 || d[2] <= d[0] || d[3] <= d[1])
            return 0;
        t = rr_metal_picture_texture(w, h);
        if (!s_cpbuf[rr_metal_frame()] || [s_cpbuf[rr_metal_frame()] length] < words * 4)
            s_cpbuf[rr_metal_frame()] = [rr_metal_device() newBufferWithLength:words * 4 * 2 options:MTLResourceStorageModeShared];
        memcpy([s_cpbuf[rr_metal_frame()] contents], desc, words * 4);
        /* the picture first, in a pass of its own, then the target's pass
           goes on */
        ce = rr_metal_compute_begin();
        [ce setComputePipelineState:s_cp];
        [ce setBuffer:s_cpbuf[rr_metal_frame()] offset:0 atIndex:0];
        [ce setBytes:wp length:16 atIndex:1];
        [ce setTexture:t atIndex:0];
        [ce dispatchThreads:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1) threadsPerThreadgroup:MTLSizeMake(32, 8, 1)];
        rr_metal_compute_end(ce);
        rr_metal_draw_picture(t, w, h, d);
        return 1;
    }
}

static void m7_draw(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s, id<MTLBuffer> px,
                    id<MTLBuffer> pic, id<MTLBuffer> cv, size_t cv_off);

void rr_mode7(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s) { m7_draw(m, d, s, nil, nil, nil, 0); }

int rr_compose_mode7(const uint32_t *desc, size_t words, const struct RrMode7 *m, const int d[4],
                     const struct RrM7Samples *s)
{
    @autoreleasepool {
        size_t np = (size_t)m->pw * (size_t)m->ph;
        id<MTLComputeCommandEncoder> ce;
        if (!s_cpr || !rr_metal_encoding() || np == 0 || d[2] <= d[0] || d[3] <= d[1])
            return 0;
        if (!s_cpbuf[rr_metal_frame()] || [s_cpbuf[rr_metal_frame()] length] < words * 4)
            s_cpbuf[rr_metal_frame()] = [rr_metal_device() newBufferWithLength:words * 4 * 2 options:MTLResourceStorageModeShared];
        if (!s_recbuf[rr_metal_frame()] || [s_recbuf[rr_metal_frame()] length] < np * 16) {
            s_recbuf[rr_metal_frame()] = [rr_metal_device() newBufferWithLength:np * 16 options:MTLResourceStorageModePrivate];
            s_picbuf[rr_metal_frame()] = [rr_metal_device() newBufferWithLength:np * 4 options:MTLResourceStorageModePrivate];
        }
        if (!s_cpbuf[rr_metal_frame()] || !s_recbuf[rr_metal_frame()] || !s_picbuf[rr_metal_frame()])
            return 0;
        memcpy([s_cpbuf[rr_metal_frame()] contents], desc, words * 4);
        /* the picture and records first, in a pass of their own */
        ce = rr_metal_compute_begin();
        [ce setComputePipelineState:s_cpr];
        [ce setBuffer:s_cpbuf[rr_metal_frame()] offset:0 atIndex:0];
        [ce setBuffer:s_recbuf[rr_metal_frame()] offset:0 atIndex:1];
        [ce setBuffer:s_picbuf[rr_metal_frame()] offset:0 atIndex:2];
        [ce dispatchThreads:MTLSizeMake((NSUInteger)m->pw, (NSUInteger)m->ph, 1) threadsPerThreadgroup:MTLSizeMake(32, 8, 1)];
        rr_metal_compute_end(ce);
        /* the canvas's texels where the description holds them */
        m7_draw(m, d, s, s_recbuf[rr_metal_frame()], s_picbuf[rr_metal_frame()], s_cpbuf[rr_metal_frame()], (size_t)desc[CP_CV_WORD] * 4);
        return 1;
    }
}

/* n bytes of p in this frame's table buffer i, kept from frame to frame
   (made larger when it must be) */
static id<MTLBuffer> table_buf(int i, const void *p, size_t n)
{
    id<MTLBuffer> b = s_m7buf[rr_metal_frame()][i];
    if (n == 0)
        n = 4;
    if (!b || [b length] < n)
        b = s_m7buf[rr_metal_frame()][i] = [rr_metal_device() newBufferWithLength:n + n / 2 options:MTLResourceStorageModeShared];
    if (b && p)
        memcpy([b contents], p, n);
    return b;
}

/* m presented over d; its records and picture from px and pic where given
   (the compositor's), else m's own; its canvas's texels from cv at byte
   cv_off where given (the description's), else m's own */
static void m7_draw(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s, id<MTLBuffer> px,
                    id<MTLBuffer> pic, id<MTLBuffer> cv, size_t cv_off)
{
    @autoreleasepool {
        struct { int pw, ph, d0, d1, step_u, step_v, du[28], dv[28], cpp, left, cvk, cvlen, cvw, cvh; } u;
        int ow = d[2] - d[0], oh = d[3] - d[1];
        size_t np = (size_t)m->pw * (size_t)m->ph;
        id<MTLBuffer> b[8];
        long off;
        int i;
        if (!rr_metal_encoding() || ow <= 0 || oh <= 0 || np == 0 || (off = rr_metal_quad(d)) < 0)
            return;
        u.pw = m->pw;
        u.ph = m->ph;
        u.d0 = d[0];
        u.d1 = d[1];
        u.step_u = s->step_u;
        u.step_v = s->step_v;
        u.cpp = m->cpp;
        u.left = m->left;
        u.cvk = m->cv_k;
        u.cvlen = (int)m->cv_len;
        u.cvw = m->cv_w;
        u.cvh = m->cv_h;
        /* the supersamples' offsets: 4 from 0, 8 from 4, 16 from 12 */
        memcpy(u.du, s->du4, sizeof s->du4);
        memcpy(u.du + 4, s->du8, sizeof s->du8);
        memcpy(u.du + 12, s->du16, sizeof s->du16);
        memcpy(u.dv, s->dv4, sizeof s->dv4);
        memcpy(u.dv + 4, s->dv8, sizeof s->dv8);
        memcpy(u.dv + 12, s->dv16, sizeof s->dv16);
        /* the frame's record, its states, palettes, VRAM and picture, and
           the tables: each draw its own copies, the GPU reading them while
           the next frame is made */
        b[0] = px ? px : table_buf(0, m->px, np * sizeof *m->px);
        b[1] = table_buf(1, m->seg, (size_t)m->segs * sizeof *m->seg);
        b[2] = table_buf(2, m->pal, (size_t)m->pals * 512);
        b[3] = table_buf(3, m->vram, (size_t)m->vrams * 0x10000);
        b[4] = pic ? pic : table_buf(4, m->pic, np * 4);
        b[5] = table_buf(5, s->cols, (size_t)ow * 3 * 4);
        b[6] = table_buf(6, s->rows, (size_t)oh * 4 * 4);
        /* the canvas's texels (a byte where there is none) */
        b[7] = cv ? cv : table_buf(7, m->cv && m->cv_len ? (const void *)m->cv : NULL, m->cv && m->cv_len ? m->cv_len : 4);
        for (i = 0; i < 8; i++)
            if (!b[i])
                return;
        if (!s_plane || s_planes < m->vrams || s_plane_gen != m->plane_gen) {
            /* the planes changed: uploaded again, after the frames in
               flight that read them (this frame's draws so far have none) */
            int layers = m->vrams ? m->vrams : 1, k;
            if (!s_plane || (int)s_plane.arrayLength < layers) {
                MTLTextureDescriptor *td = [MTLTextureDescriptor new];
                td.textureType = MTLTextureType2DArray;
                td.pixelFormat = MTLPixelFormatR8Uint;
                td.width = td.height = 1024;
                td.arrayLength = (NSUInteger)layers;
                td.usage = MTLTextureUsageShaderRead;
                td.storageMode = MTLStorageModeShared;
                s_plane = [rr_metal_device() newTextureWithDescriptor:td];
            } else {
                rr_metal_wait_idle();
            }
            for (k = 0; k < m->vrams; k++)
                [s_plane replaceRegion:MTLRegionMake2D(0, 0, 1024, 1024) mipmapLevel:0 slice:(NSUInteger)k
                             withBytes:m->plane + (size_t)k * 0x100000u bytesPerRow:1024 bytesPerImage:0x100000];
            s_plane_gen = m->plane_gen;
            s_planes = m->vrams;
        }
        [rr_metal_encoder() setRenderPipelineState:s_m7];
        [rr_metal_encoder() setFragmentTexture:s_plane atIndex:0];
        [rr_metal_encoder() setVertexBuffer:rr_metal_vertices() offset:(NSUInteger)off atIndex:0];
        [rr_metal_encoder() setFragmentBytes:&u length:sizeof u atIndex:0];
        for (i = 0; i < 8; i++)
            [rr_metal_encoder() setFragmentBuffer:b[i] offset:i == 7 && cv ? cv_off : 0 atIndex:(NSUInteger)i + 1];
        [rr_metal_encoder() drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
}
