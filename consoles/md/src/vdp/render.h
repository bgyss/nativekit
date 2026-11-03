/* Internal Mode 5 renderer interface. */
#ifndef NMD_RENDER_H
#define NMD_RENDER_H

#include <stdint.h>

void md_vdp_render_reset(void);
void md_vdp_render_line(unsigned line);
const uint32_t *md_vdp_picture(int *width, int *height);
void md_vdp_dimensions(int *width, int *height);

#endif
