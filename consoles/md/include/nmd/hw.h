/* Public Mega Drive memory-mapped hardware ports. */
#ifndef NMD_HW_H
#define NMD_HW_H

#include "nmd/version.h"

#define NMD_BUTTON_UP    (1u << 0)
#define NMD_BUTTON_DOWN  (1u << 1)
#define NMD_BUTTON_LEFT  (1u << 2)
#define NMD_BUTTON_RIGHT (1u << 3)
#define NMD_BUTTON_B     (1u << 4)
#define NMD_BUTTON_C     (1u << 5)
#define NMD_BUTTON_A     (1u << 6)
#define NMD_BUTTON_START (1u << 7)
#define NMD_BUTTON_X     (1u << 8)
#define NMD_BUTTON_Y     (1u << 9)
#define NMD_BUTTON_Z     (1u << 10)
#define NMD_BUTTON_MODE  (1u << 11)

#define NMD_VDP_DATA    0xC00000u
#define NMD_VDP_CONTROL 0xC00004u
#define NMD_VDP_HV      0xC00008u
#define NMD_PSG         0xC00011u

#endif
