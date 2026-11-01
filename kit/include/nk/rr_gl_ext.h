/*
 * rr_gl_ext.h: the OpenGL backend's programs, quad and target, for a
 * console's own drawing on the GPU (its compositor, its own pictures).
 * Its programs are made in the backend's open hook (rr_set_open_hook).
 */
#ifndef NK_RR_GL_EXT_H
#define NK_RR_GL_EXT_H

#include "gl_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* vs and fs compiled and linked, attribute 0 "a" and output "o", made
   current with sampler "t" on unit 0; 0 on failure (the message printed) */
GLuint rr_gl_program(const char *vs, const char *fs);
extern const char *const rr_gl_quad_vs;   /* the backend's vertex shader (a: xy in NDC, uv), for a console's programs */
void rr_gl_draw(GLuint tex, int w, int h, const int d[4]);   /* texture tex (w x h, unit 0) over d: one to one or sharp bilinear */
GLuint rr_gl_picture_texture(int w, int h);   /* the picture texture, bound on unit 0, w x h (made so when not) */
void rr_gl_target_bind(void);                 /* the offscreen target bound as the framebuffer */
void rr_gl_target_size(int *w, int *h);       /* the offscreen target's size */
GLuint rr_gl_vertex_array(void);              /* the quad's vertex array (attribute 0, 4 floats a vertex) */
GLuint rr_gl_vertex_buffer(void);             /* its vertex buffer, 6 vertices a draw */

#ifdef __cplusplus
}
#endif
#endif
