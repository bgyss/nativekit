/* Present the VDP's software frame through the shared renderer interface. */
#include "md_present.h"

#include "nk/rr.h"
#include "../vdp/render.h"

void md_present(const int dst[4])
{
    int width, height;
    const uint32_t *picture = md_vdp_picture(&width, &height);
    rr_image(picture, width, height, dst);
}
