/* Shared timestamped interfaces for the Mega Drive sound chips. */
#ifndef NMD_SOUND_H
#define NMD_SOUND_H

#include <stdint.h>

void md_ym_reset(void);
uint8_t md_ym_read(uint8_t port, uint64_t clock);
void md_ym_write(uint8_t port, uint8_t value, uint64_t clock);

void md_psg_reset(void);
void md_psg_write(uint8_t value, uint64_t clock);

#endif
