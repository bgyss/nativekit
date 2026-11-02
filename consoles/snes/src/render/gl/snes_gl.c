/*
 * snes_gl.c: the SNES's picture on the OpenGL backend (the kit's rr_gl.c,
 * through rr_gl_ext.h): its compositor (compose_body.h) in a fragment
 * shader, and a frame with Mode 7 with rr_mode7.h's arithmetic in a
 * fragment shader. The programs are compiled at start-up, in the backend's
 * open hook (snes_gl_open).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes_rr.h"
#include "rr_mode7.h"
#include "nk/rr_gl_ext.h"

/* a frame with Mode 7 presented (rr_mode7.h, the same arithmetic): the
   shader the Vulkan backend's is made from (vulkan/shaders/m7_body.glsl) */
#include "rr_m7_glsl.h"
#include "rr_compose_src.h"

/* the compositor (compose_body.h): a fragment a picture pixel, the frame's
   description in a buffer texture */
static const char k_cp_pre[] =
    "#version 330 core\n"
    "uniform usamplerBuffer fdb;\n"
    "#define CP_FN\n"
    "#define CP_VAR\n"
    "#define U32 uint\n"
    "#define I32 int\n"
    "#define U_(x) uint(x)\n"
    "#define I_(x) int(x)\n"
    "#define FD_(i) texelFetch(fdb, int(cp_fdb + uint(i))).r\n";
/* a frame presenting Mode 7 or a canvas: its records and picture, into
   two integer targets */
static const char k_cpr_post[] =
    "layout(location = 0) out uvec4 rec;\n"
    "layout(location = 1) out uint picv;\n"
    "void main() {\n"
    "    uint X = uint(gl_FragCoord.x), Y = uint(gl_FragCoord.y);\n"
    "    cp_fdb = 0u;\n"
    "    picv = cp_pixel(X, Y);\n"
    "    cp_record(X, Y);\n"
    "    rec = uvec4(cp_r0, cp_r1, cp_r2, cp_r3);\n"
    "}\n";
static const char k_cp_post[] =
    "out vec4 o;\n"
    "uniform uint U_ww, U_b0, U_b1;\n"
    "void main() {\n"
    "uint p;\n"
    "cp_fdb = 0u;\n"
    "p = U_ww == 0u ? cp_pixel(uint(gl_FragCoord.x), uint(gl_FragCoord.y))\n"
    "               : cp_woven(uint(gl_FragCoord.x), uint(gl_FragCoord.y), U_ww, U_b0, U_b1);\n"
    "    o = vec4(float((p >> 16) & 255u), float((p >> 8) & 255u), float(p & 255u), 255.0) / 255.0;\n"
    "}\n";

static GLuint s_m7;
static GLuint s_m7buf[8], s_m7tex[8], s_plane;
static uint32_t s_plane_gen;
static int s_planes, s_plane_layers;
static GLuint s_cp, s_cpfbo, s_cpbuf, s_cptex;   /* the compositor, its target and description */
static GLuint s_cpr, s_rectex, s_picutex;        /* the records' program and targets */
static int s_recw, s_rech;
static void m7_draw(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s, int gpu);

/* the backend's open hook (rr_set_open_hook), in rr_open after the
   backend's own programs: the Mode 7 program, the compositor's two and
   their buffers and textures; 0 (the backend not open) when they do not
   compile */
int snes_gl_open(void)
{
    s_m7 = rr_gl_program(rr_gl_quad_vs, k_m7);
    if (!s_m7)
        return 0;
    {
        size_t n = sizeof k_cp_pre + sizeof k_compose_body + sizeof k_cp_post;
        char *fs = (char *)malloc(n);
        if (!fs)
            return 0;
        snprintf(fs, n, "%s%s%s", k_cp_pre, k_compose_body, k_cp_post);
        s_cp = rr_gl_program(rr_gl_quad_vs, fs);
        free(fs);
        if (!s_cp)
            return 0;
        glUniform1i(glGetUniformLocation(s_cp, "fdb"), 12);
        n = sizeof k_cp_pre + sizeof k_compose_body + sizeof k_cpr_post;
        fs = (char *)malloc(n);
        if (!fs)
            return 0;
        snprintf(fs, n, "%s%s%s", k_cp_pre, k_compose_body, k_cpr_post);
        s_cpr = rr_gl_program(rr_gl_quad_vs, fs);
        free(fs);
        if (!s_cpr)
            return 0;
        glUniform1i(glGetUniformLocation(s_cpr, "fdb"), 12);
        glGenTextures(1, &s_rectex);
        glGenTextures(1, &s_picutex);
        glGenFramebuffers(1, &s_cpfbo);
        glGenBuffers(1, &s_cpbuf);
        glGenTextures(1, &s_cptex);
    }
    {
        static const char *names[9] = { "Pb", "Sb", "palb", "vramb", "picb", "colsb", "rowsb", "planet", "cvb" };
        int i;
        glUseProgram(s_m7);
        glGenBuffers(8, s_m7buf);
        glGenTextures(8, s_m7tex);
        glGenTextures(1, &s_plane);
        for (i = 0; i < 9; i++)
            glUniform1i(glGetUniformLocation(s_m7, names[i]), i + 1);
    }
    return 1;
}

static int compose(const uint32_t *desc, size_t words, int w, int h, const uint32_t wp[3], const int d[4]);

int rr_compose(const uint32_t *desc, size_t words, int w, int h, const int d[4])
{
    static const uint32_t none[3] = { 0, 0, 0 };
    return compose(desc, words, w, h, none, d);
}

int rr_compose_woven(const uint32_t *desc, size_t words, int ww, int wh, uint32_t base0, uint32_t base1,
                     const int d[4])
{
    uint32_t wp[3] = { (uint32_t)ww, base0, base1 };
    return compose(desc, words, ww, wh, wp, d);
}

static int compose(const uint32_t *desc, size_t words, int w, int h, const uint32_t wp[3], const int d[4])
{
    static const float q[6][4] = { { -1, -1, 0, 0 }, { 1, -1, 1, 0 }, { -1, 1, 0, 1 },
                                   { -1, 1, 0, 1 }, { 1, -1, 1, 0 }, { 1, 1, 1, 1 } };
    GLuint pic;
    int rw, rh;
    if (!s_cp || w <= 0 || h <= 0 || d[2] <= d[0] || d[3] <= d[1])
        return 0;
    /* the picture, row 0 the texture's first (as rr_image's), drawn into
       by the compositor; then shown as rr_image shows it */
    pic = rr_gl_picture_texture(w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + 12);
    glBindBuffer(GL_TEXTURE_BUFFER, s_cpbuf);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(words * 4), desc, GL_STREAM_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, s_cptex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, s_cpbuf);
    glBindFramebuffer(GL_FRAMEBUFFER, s_cpfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pic, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        rr_gl_target_bind();
        return 0;
    }
    glViewport(0, 0, w, h);
    glUseProgram(s_cp);
    glUniform1ui(glGetUniformLocation(s_cp, "U_ww"), wp[0]);
    glUniform1ui(glGetUniformLocation(s_cp, "U_b0"), wp[1]);
    glUniform1ui(glGetUniformLocation(s_cp, "U_b1"), wp[2]);
    glBindBuffer(GL_ARRAY_BUFFER, rr_gl_vertex_buffer());
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    rr_gl_target_bind();
    rr_gl_target_size(&rw, &rh);
    glViewport(0, 0, rw, rh);
    glActiveTexture(GL_TEXTURE0);
    rr_gl_draw(pic, w, h, d);
    return 1;
}

int rr_compose_mode7(const uint32_t *desc, size_t words, const struct RrMode7 *m, const int d[4],
                     const struct RrM7Samples *s)
{
    static const float q[6][4] = { { -1, -1, 0, 0 }, { 1, -1, 1, 0 }, { -1, 1, 0, 1 },
                                   { -1, 1, 0, 1 }, { 1, -1, 1, 0 }, { 1, 1, 1, 1 } };
    static const GLenum both[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    int w = m->pw, h = m->ph;
    size_t np = (size_t)w * (size_t)h;
    if (!s_cpr || np == 0 || d[2] <= d[0] || d[3] <= d[1])
        return 0;
    /* the records and picture drawn into two integer targets, row 0 the
       first (as the picture's), then copied on the GPU into the
       presentation's buffers */
    if (w != s_recw || h != s_rech) {
        glBindTexture(GL_TEXTURE_2D, s_rectex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, w, h, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, s_picutex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        s_recw = w;
        s_rech = h;
    }
    glActiveTexture(GL_TEXTURE0 + 12);
    glBindBuffer(GL_TEXTURE_BUFFER, s_cpbuf);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(words * 4), desc, GL_STREAM_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, s_cptex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, s_cpbuf);
    glBindFramebuffer(GL_FRAMEBUFFER, s_cpfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_rectex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, s_picutex, 0);
    glDrawBuffers(2, both);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDrawBuffers(1, both);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
        rr_gl_target_bind();
        glActiveTexture(GL_TEXTURE0);
        return 0;
    }
    glViewport(0, 0, w, h);
    glUseProgram(s_cpr);
    glBindVertexArray(rr_gl_vertex_array());
    glBindBuffer(GL_ARRAY_BUFFER, rr_gl_vertex_buffer());
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s_m7buf[0]);
    glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)(np * 16), NULL, GL_STREAM_COPY);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, w, h, GL_RGBA_INTEGER, GL_UNSIGNED_INT, (void *)0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, s_m7buf[4]);
    glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)(np * 4), NULL, GL_STREAM_COPY);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, (void *)0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffers(1, both);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
    glActiveTexture(GL_TEXTURE0);
    m7_draw(m, d, s, 1);
    return 1;
}

void rr_mode7(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s) { m7_draw(m, d, s, 0); }

/* m presented over d; with gpu, its records and picture are in the
   presentation's buffers already (the compositor's) */
static void m7_draw(const struct RrMode7 *m, const int d[4], const struct RrM7Samples *s, int gpu)
{
    static const GLenum fmt[7] = { GL_RGBA32UI, GL_R32I, GL_R16UI, GL_R16UI, GL_R32UI, GL_R32I, GL_R32I };
    static const uint8_t none = 0;
    static const int ix[6] = { 0, 1, 2, 2, 1, 3 };
    int ow = d[2] - d[0], oh = d[3] - d[1], i, du[28], dv[28];
    size_t np = (size_t)m->pw * (size_t)m->ph;
    const void *src[7];
    size_t len[7];
    float c[4][4] = { { (float)d[0], (float)d[1], 0, 0 }, { (float)d[2], (float)d[1], 1, 0 },
                      { (float)d[0], (float)d[3], 0, 1 }, { (float)d[2], (float)d[3], 1, 1 } };
    float q[6][4];
    int rw, rh;
    if (ow <= 0 || oh <= 0 || np == 0)
        return;
    rr_gl_target_size(&rw, &rh);
    src[0] = m->px;
    len[0] = np * sizeof *m->px;
    src[1] = m->seg;
    len[1] = (size_t)(m->segs ? m->segs : 1) * sizeof *m->seg;
    src[2] = m->pal;
    len[2] = (size_t)(m->pals ? m->pals : 1) * 512;
    src[3] = m->vram;
    len[3] = (size_t)(m->vrams ? m->vrams : 1) * 0x10000;
    src[4] = m->pic;
    len[4] = np * 4;
    src[5] = s->cols;
    len[5] = (size_t)ow * 3 * 4;
    src[6] = s->rows;
    len[6] = (size_t)oh * 4 * 4;
    glUseProgram(s_m7);
    for (i = 0; i < 7; i++) {
        glBindBuffer(GL_TEXTURE_BUFFER, s_m7buf[i]);
        if (!gpu || (i != 0 && i != 4))
            glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)len[i], src[i], GL_STREAM_DRAW);
        glActiveTexture(GL_TEXTURE0 + 1 + (GLenum)i);
        glBindTexture(GL_TEXTURE_BUFFER, s_m7tex[i]);
        glTexBuffer(GL_TEXTURE_BUFFER, fmt[i], s_m7buf[i]);
    }
    glActiveTexture(GL_TEXTURE0 + 8);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_plane);
    if (s_planes < m->vrams || s_plane_gen != m->plane_gen) {
        /* the planes changed: uploaded again */
        int layers = m->vrams ? m->vrams : 1;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (s_plane_layers < layers) {
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8UI, 1024, 1024, layers, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            s_plane_layers = layers;
        }
        if (m->vrams)
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 1024, 1024, m->vrams, GL_RED_INTEGER, GL_UNSIGNED_BYTE, m->plane);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        s_plane_gen = m->plane_gen;
        s_planes = m->vrams;
    }
    /* the canvas's texels (a byte where there is none), on unit 9 */
    glBindBuffer(GL_TEXTURE_BUFFER, s_m7buf[7]);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(m->cv && m->cv_len ? m->cv_len : 1),
                 m->cv && m->cv_len ? (const void *)m->cv : &none, GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0 + 9);
    glBindTexture(GL_TEXTURE_BUFFER, s_m7tex[7]);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R8UI, s_m7buf[7]);
    /* the supersamples' offsets: 4 from 0, 8 from 4, 16 from 12 */
    memcpy(du, s->du4, sizeof s->du4);
    memcpy(du + 4, s->du8, sizeof s->du8);
    memcpy(du + 12, s->du16, sizeof s->du16);
    memcpy(dv, s->dv4, sizeof s->dv4);
    memcpy(dv + 4, s->dv8, sizeof s->dv8);
    memcpy(dv + 12, s->dv16, sizeof s->dv16);
    glUniform1i(glGetUniformLocation(s_m7, "U_pw"), m->pw);
    glUniform1i(glGetUniformLocation(s_m7, "U_ph"), m->ph);
    glUniform1i(glGetUniformLocation(s_m7, "U_d0"), d[0]);
    glUniform1i(glGetUniformLocation(s_m7, "U_d1"), d[1]);
    glUniform1i(glGetUniformLocation(s_m7, "U_step_u"), s->step_u);
    glUniform1i(glGetUniformLocation(s_m7, "U_step_v"), s->step_v);
    glUniform1i(glGetUniformLocation(s_m7, "U_rh"), rh);
    glUniform1i(glGetUniformLocation(s_m7, "U_cpp"), m->cpp);
    glUniform1i(glGetUniformLocation(s_m7, "U_left"), m->left);
    glUniform1i(glGetUniformLocation(s_m7, "U_cvk"), m->cv_k);
    glUniform1i(glGetUniformLocation(s_m7, "U_cvlen"), (int)m->cv_len);
    glUniform1i(glGetUniformLocation(s_m7, "U_cvw"), m->cv_w);
    glUniform1i(glGetUniformLocation(s_m7, "U_cvh"), m->cv_h);
    glUniform1iv(glGetUniformLocation(s_m7, "U_du_"), 28, du);
    glUniform1iv(glGetUniformLocation(s_m7, "U_dv_"), 28, dv);
    for (i = 0; i < 6; i++) {
        q[i][0] = c[ix[i]][0] / rw * 2 - 1;
        q[i][1] = 1 - c[ix[i]][1] / rh * 2;
        q[i][2] = c[ix[i]][2];
        q[i][3] = c[ix[i]][3];
    }
    rr_gl_target_bind();
    glViewport(0, 0, rw, rh);
    glBindVertexArray(rr_gl_vertex_array());
    glBindBuffer(GL_ARRAY_BUFFER, rr_gl_vertex_buffer());
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glActiveTexture(GL_TEXTURE0);
}
