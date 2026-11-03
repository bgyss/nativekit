/* A memory-to-VRAM DMA copies ordered 68000 words through the VDP. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <nmd/hw.h>
#include <nmd/nmd.h>

#include "md_internal.h"

static uint8_t rom[0x400];
static unsigned irq_count;

static void irq6(void)
{
    irq_count++;
}

static const nmd_routine routines[] = {
    { 0x100, irq6, "irq6" },
};

static void reg_write(unsigned reg, uint8_t value)
{
    wr16(NMD_VDP_CONTROL, (uint16_t)(0x8000u | (reg << 8) | value));
}

static void control_write(uint32_t command)
{
    wr32(NMD_VDP_CONTROL, command);
}

static void source_address_set(uint32_t address)
{
    reg_write(21, (uint8_t)(address >> 1));
    reg_write(22, (uint8_t)(address >> 9));
    reg_write(23, (uint8_t)(address >> 17));
}

static void dma_registers_set(uint32_t source, uint16_t words)
{
    reg_write(1, 0x10); /* Enable DMA. */
    reg_write(15, 2);
    reg_write(19, (uint8_t)words);
    reg_write(20, (uint8_t)(words >> 8));
    source_address_set(source);
}

static void test_memory_dma(nmd_cart *cart, uint32_t destination_command,
                            uint32_t read_command, uint32_t source, uint16_t words,
                            const uint16_t *expected)
{
    uint16_t value;
    uint16_t status;
    uint64_t before;
    unsigned i;

    nmd_power(cart);
    nmd_wait_vblank();
    dma_registers_set(source, words);
    before = md_clock;
    control_write(destination_command);
    assert(md_clock - before > (destination_command == 0x40000080 ? 100u : 56u));
    control_write(read_command);
    for (i = 0; i < words; i++) {
        value = rd16(NMD_VDP_DATA);
        assert(value == expected[i]);
    }
    status = rd16(NMD_VDP_CONTROL);
    assert((status & 0x0002u) == 0);
}

static void test_active_display_dma_wait(nmd_cart *cart, int h40)
{
    uint64_t before;

    nmd_power(cart);
    reg_write(1, 0x54); /* Display and DMA enabled; start in H32 active scan. */
    reg_write(12, (uint8_t)(h40 != 0));
    reg_write(15, 2);
    reg_write(19, 2);
    reg_write(20, 0);
    source_address_set(0x200);
    before = md_clock;
    control_write(0x40000080);
    assert(md_clock - before > (h40 ? 350u : 400u));
}

static void test_interrupt_waits_for_dma(nmd_cart *cart)
{
    uint64_t before;

    nmd_power(cart);
    nmd_cpu.a[7] = 0x00FF0000;
    nmd_cpu.sr = 0x2000;
    reg_write(1, 0x14);
    reg_write(15, 2);
    reg_write(19, 2);
    reg_write(20, 0);
    source_address_set(0x200);
    irq_count = 0;
    before = md_clock;
    md_irq_request_at(6, before + 65u);
    control_write(0x40000080);
    assert(irq_count == 1);
    assert(md_irq_last_service_clock == before + 105u);
}

static int dma_idle(void *ctx)
{
    (void)ctx;
    return !md_dma_busy();
}

static void test_fill_dma(nmd_cart *cart)
{
    uint16_t first;
    uint16_t second;
    uint16_t third;
    uint16_t status;

    nmd_power(cart);
    nmd_wait_vblank();
    reg_write(1, 0x10);
    reg_write(15, 1);
    reg_write(19, 3); /* Fill three VRAM bytes after the seed byte. */
    reg_write(20, 0);
    reg_write(23, 0x80); /* VRAM fill mode. */
    control_write(0x41000080); /* Destination $0100. */
    wr16(NMD_VDP_DATA, 0xA500);

    status = rd16(NMD_VDP_CONTROL);
    assert((status & 0x0002u) != 0);
    (void)nmd_wait_until(dma_idle, NULL);
    control_write(0x01000000);
    first = rd16(NMD_VDP_DATA);
    second = rd16(NMD_VDP_DATA);
    third = rd16(NMD_VDP_DATA);
    assert(first == 0xA500);
    assert(second == first); /* VRAM reads ignore A0 while autoincrement is one. */
    assert(third == 0xA5A5);
}

static void test_copy_dma(nmd_cart *cart)
{
    uint16_t copied;

    nmd_power(cart);
    nmd_wait_vblank();
    reg_write(1, 0x10);
    reg_write(15, 1);
    control_write(0x40000000);
    wr16(NMD_VDP_DATA, 0xA55A);
    reg_write(19, 2); /* Copy two VRAM bytes. */
    reg_write(20, 0);
    reg_write(21, 0);
    reg_write(22, 0);
    reg_write(23, 0xC0); /* VRAM copy mode, source $0000. */
    control_write(0x40040080); /* Destination $0004. */
    (void)nmd_wait_until(dma_idle, NULL);
    control_write(0x00040000);
    copied = rd16(NMD_VDP_DATA);
    assert(copied == 0xA55A);
}

static int fatal_case(const char *mode, nmd_cart *cart)
{
    uint32_t source;
    uint16_t words;
    if (!strcmp(mode, "fill-non-vram") || !strcmp(mode, "copy-non-vram")) {
        nmd_power(cart);
        nmd_wait_vblank();
        reg_write(1, 0x10);
        reg_write(19, 1);
        reg_write(20, 0);
        reg_write(23, !strcmp(mode, "fill-non-vram") ? 0x80 : 0xC0);
        control_write(0xC0000080); /* CRAM plus DMA: fill/copy must reject it. */
        return 3;
    }
    if (!strcmp(mode, "source-bounds")) {
        source = 0x400;
        words = 1;
    } else if (!strcmp(mode, "zero-count")) {
        source = 0x3FE;
        words = 0;
    } else {
        return 2;
    }
    nmd_power(cart);
    nmd_wait_vblank();
    dma_registers_set(source, words);
    control_write(0x40000080);
    return 3;
}

int main(int argc, char **argv)
{
    nmd_cart cart = {
        .title = "DMA check",
        .region = NMD_NTSC,
        .rom = rom,
        .rom_size = sizeof rom,
        .routines = routines,
        .routine_count = sizeof routines / sizeof routines[0],
    };
    static const uint16_t vram_expected[] = { 0xA1B2, 0xC3D4 };
    static const uint16_t cram_expected[] = { 0x00A2, 0x02C4 };
    static const uint16_t vsram_expected[] = { 0xA1B2, 0xC3D4 };
    static const uint16_t work_ram_expected[] = { 0x1357 };

    rom[0x200] = 0xA1;
    rom[0x201] = 0xB2;
    rom[0x202] = 0xC3;
    rom[0x203] = 0xD4;
    rom[0x78] = 0;
    rom[0x79] = 0;
    rom[0x7A] = 1;
    rom[0x7B] = 0;
    if (argc == 2)
        return fatal_case(argv[1], &cart);

    test_memory_dma(&cart, 0x40000080, 0x00000000, 0x200, 2, vram_expected);
    test_memory_dma(&cart, 0xC0000080, 0x00000020, 0x200, 2, cram_expected);
    test_memory_dma(&cart, 0x40000090, 0x00000010, 0x200, 2, vsram_expected);
    test_active_display_dma_wait(&cart, 0);
    test_active_display_dma_wait(&cart, 1);
    test_interrupt_waits_for_dma(&cart);

    test_fill_dma(&cart);
    test_copy_dma(&cart);

    nmd_power(&cart);
    nmd_wait_vblank();
    wr16(0xFF0000, 0x1357);
    dma_registers_set(0xFF0000, 1);
    control_write(0x41000080);
    control_write(0x01000000);
    assert(rd16(NMD_VDP_DATA) == work_ram_expected[0]);
    return 0;
}
