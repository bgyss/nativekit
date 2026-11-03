/* YM2612 port, timer, DAC, and timestamped sample state. */
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "sound.h"
#include "nk/fatal.h"

#define YM_STATUS_TIMER_A 0x01u
#define YM_STATUS_TIMER_B 0x02u
#define YM_STATUS_BUSY 0x80u
#define YM_MASTER_CLOCKS_PER_SAMPLE 1008u
#define YM_SAMPLE_CAPACITY 65536u
#define YM_TIMER_CLOCK_DENOMINATOR 1000000ull
#define YM_BUSY_START_DELAY 42ull
#define YM_BUSY_CYCLES 32ull
#define YM_CLOCK_INC 42ull
#define YM_NEVER UINT64_MAX
#define YM_ZERO_OFFSET 112
#define YM_VOLUME_MULTIPLIER 79
#define YM_VOLUME_DIVISOR 120

/* Quarter-wave log-sine (4.8) and fractional power (3.11) lookup tables. */
static const uint16_t log_sine_quarter[256] = {
    2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137,
    1091, 1050, 1013, 979, 949, 920, 894, 869,
    846, 825, 804, 785, 767, 749, 732, 717,
    701, 687, 672, 659, 646, 633, 621, 609,
    598, 587, 576, 566, 556, 546, 536, 527,
    518, 509, 501, 492, 484, 476, 468, 461,
    453, 446, 439, 432, 425, 418, 411, 405,
    399, 392, 386, 380, 375, 369, 363, 358,
    352, 347, 341, 336, 331, 326, 321, 316,
    311, 307, 302, 297, 293, 289, 284, 280,
    276, 271, 267, 263, 259, 255, 251, 248,
    244, 240, 236, 233, 229, 226, 222, 219,
    215, 212, 209, 205, 202, 199, 196, 193,
    190, 187, 184, 181, 178, 175, 172, 169,
    167, 164, 161, 159, 156, 153, 151, 148,
    146, 143, 141, 138, 136, 134, 131, 129,
    127, 125, 122, 120, 118, 116, 114, 112,
    110, 108, 106, 104, 102, 100, 98, 96,
    94, 92, 91, 89, 87, 85, 83, 82,
    80, 78, 77, 75, 74, 72, 70, 69,
    67, 66, 64, 63, 62, 60, 59, 57,
    56, 55, 53, 52, 51, 49, 48, 47,
    46, 45, 43, 42, 41, 40, 39, 38,
    37, 36, 35, 34, 33, 32, 31, 30,
    29, 28, 27, 26, 25, 24, 23, 23,
    22, 21, 20, 20, 19, 18, 17, 17,
    16, 15, 15, 14, 13, 13, 12, 12,
    11, 10, 10, 9, 9, 8, 8, 7,
    7, 7, 6, 6, 5, 5, 5, 4,
    4, 4, 3, 3, 3, 2, 2, 2,
    2, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const uint16_t power_fraction[256] = {
    2042, 2037, 2031, 2026, 2020, 2015, 2010, 2004,
    1999, 1993, 1988, 1983, 1977, 1972, 1966, 1961,
    1956, 1951, 1945, 1940, 1935, 1930, 1924, 1919,
    1914, 1909, 1904, 1898, 1893, 1888, 1883, 1878,
    1873, 1868, 1863, 1858, 1853, 1848, 1843, 1838,
    1833, 1828, 1823, 1818, 1813, 1808, 1803, 1798,
    1794, 1789, 1784, 1779, 1774, 1769, 1765, 1760,
    1755, 1750, 1746, 1741, 1736, 1732, 1727, 1722,
    1717, 1713, 1708, 1704, 1699, 1694, 1690, 1685,
    1681, 1676, 1672, 1667, 1663, 1658, 1654, 1649,
    1645, 1640, 1636, 1631, 1627, 1623, 1618, 1614,
    1609, 1605, 1601, 1596, 1592, 1588, 1584, 1579,
    1575, 1571, 1566, 1562, 1558, 1554, 1550, 1545,
    1541, 1537, 1533, 1529, 1525, 1520, 1516, 1512,
    1508, 1504, 1500, 1496, 1492, 1488, 1484, 1480,
    1476, 1472, 1468, 1464, 1460, 1456, 1452, 1448,
    1444, 1440, 1436, 1433, 1429, 1425, 1421, 1417,
    1413, 1409, 1406, 1402, 1398, 1394, 1391, 1387,
    1383, 1379, 1376, 1372, 1368, 1364, 1361, 1357,
    1353, 1350, 1346, 1342, 1339, 1335, 1332, 1328,
    1324, 1321, 1317, 1314, 1310, 1307, 1303, 1300,
    1296, 1292, 1289, 1286, 1282, 1279, 1275, 1272,
    1268, 1265, 1261, 1258, 1255, 1251, 1248, 1244,
    1241, 1238, 1234, 1231, 1228, 1224, 1221, 1218,
    1214, 1211, 1208, 1205, 1201, 1198, 1195, 1192,
    1188, 1185, 1182, 1179, 1176, 1172, 1169, 1166,
    1163, 1160, 1157, 1154, 1150, 1147, 1144, 1141,
    1138, 1135, 1132, 1129, 1126, 1123, 1120, 1117,
    1114, 1111, 1108, 1105, 1102, 1099, 1096, 1093,
    1090, 1087, 1084, 1081, 1078, 1075, 1072, 1069,
    1066, 1064, 1061, 1058, 1055, 1052, 1049, 1046,
    1044, 1041, 1038, 1035, 1032, 1030, 1027, 1024
};

enum {
    YM_ENV_ATTACK,
    YM_ENV_DECAY,
    YM_ENV_SUSTAIN,
    YM_ENV_RELEASE
};

#define YM_OPERATOR_COUNT 24u
#define YM_CHANNEL_COUNT 6u
#define YM_MAX_ENVELOPE 0xFFCu
#define YM_SSG_CENTER 0x800u
#define YM_SSG_ENABLE 0x08u
#define YM_SSG_ALTERNATE 0x02u
#define YM_SSG_HOLD 0x01u
#define YM_SSG_INVERT 0x04u

typedef struct ym_operator_state {
    uint32_t phase_counter;
    uint32_t phase_increment;
    uint16_t envelope;
    uint16_t sustain_level;
    uint16_t total_level;
    uint8_t detune;
    uint8_t multiple;
    uint8_t key_scaling;
    uint8_t am;
    uint8_t ssg;
    uint8_t inverted;
    uint8_t phase_overflow;
    uint8_t env_phase;
    uint8_t rates[4];
    int16_t output;
} ym_operator_state;

typedef struct ym_channel_state {
    uint16_t fnum;
    uint8_t block;
    uint8_t fnum_latch;
    uint8_t keycode;
    uint8_t algorithm;
    uint8_t feedback;
    uint8_t lr;
    uint8_t ams;
    uint8_t pms;
    uint8_t keyon;
    int16_t output;
    int16_t op1_old;
    int16_t op2_old;
} ym_channel_state;

typedef struct ym_supplementary_frequency {
    uint16_t fnum;
    uint8_t block;
    uint8_t fnum_latch;
    uint8_t keycode;
} ym_supplementary_frequency;

static const uint8_t fnum_to_keycode[16] = {
    0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3
};

static const uint8_t detune_delta[32][4] = {
    {0, 0, 1, 2}, {0, 0, 1, 2}, {0, 0, 1, 2}, {0, 0, 1, 2},
    {0, 1, 2, 2}, {0, 1, 2, 3}, {0, 1, 2, 3}, {0, 1, 2, 3},
    {0, 1, 2, 4}, {0, 1, 3, 4}, {0, 1, 3, 4}, {0, 1, 3, 5},
    {0, 2, 4, 5}, {0, 2, 4, 6}, {0, 2, 4, 6}, {0, 2, 5, 7},
    {0, 2, 5, 8}, {0, 3, 6, 8}, {0, 3, 6, 9}, {0, 3, 7, 10},
    {0, 4, 8, 11}, {0, 4, 8, 12}, {0, 4, 9, 13}, {0, 5, 10, 14},
    {0, 5, 11, 16}, {0, 6, 12, 17}, {0, 6, 13, 19}, {0, 7, 14, 20},
    {0, 8, 16, 22}, {0, 8, 16, 22}, {0, 8, 16, 22}, {0, 8, 16, 22}
};

static const uint8_t envelope_rate_base[64] = {
    0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 1, 1, 0, 1,
    0, 1, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 1, 1, 1, 2,
    1, 2, 1, 2, 1, 2, 1, 2,
    1, 2, 2, 2, 1, 2, 2, 2
};

static const uint8_t lfo_pm_base[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 4, 4, 4, 4},
    {0, 0, 0, 4, 4, 4, 8, 8},
    {0, 0, 4, 4, 8, 8, 12, 12},
    {0, 0, 4, 8, 8, 8, 12, 16},
    {0, 0, 8, 12, 16, 16, 20, 24},
    {0, 0, 16, 24, 32, 32, 40, 48},
    {0, 0, 32, 48, 64, 64, 80, 96}
};

static const uint8_t lfo_timer_values[8] = { 108, 77, 71, 67, 62, 44, 8, 5 };
static const int8_t ams_shift[4] = { 8, 1, -1, -2 };
static uint8_t envelope_rate_table[64u * 8u];

md_ym_state md_ym;

static uint64_t master_clock_hz;
static double ym_source_rate;
static int16_t sample_ring[YM_SAMPLE_CAPACITY];
static int16_t stereo_sample_ring[YM_SAMPLE_CAPACITY * 2u];
static uint64_t sample_read;
static uint64_t sample_write;
static ym_operator_state ym_operators[YM_OPERATOR_COUNT];
static ym_channel_state ym_channels[YM_CHANNEL_COUNT];
static ym_supplementary_frequency ym_ch3_supp[3];
static uint64_t next_operator_clock;
static uint16_t envelope_counter;
static uint8_t current_op;
static uint8_t current_env_op;
static uint8_t channel3_mode;
static uint8_t lfo_enable;
static uint8_t lfo_freq;
static uint8_t lfo_counter;
static uint8_t lfo_am_step;
static uint8_t lfo_pm_step;

static void build_envelope_rate_table(void)
{
    unsigned rate, cycle;

    for (rate = 0; rate < 64; rate++) {
        for (cycle = 0; cycle < 8; cycle++) {
            uint8_t value;

            if (rate < 2)
                value = 0;
            else if (rate >= 60)
                value = 8;
            else if (rate < 8)
                value = envelope_rate_base[((rate & 6u) == 6u ? 16u : 0u) + cycle];
            else if (rate < 48)
                value = envelope_rate_base[(rate & 3u) * 8u + cycle];
            else
                value = (uint8_t)(envelope_rate_base[32u + (rate & 3u) * 8u + cycle] <<
                                  ((rate - 48u) >> 2));
            envelope_rate_table[rate * 8u + cycle] = value;
        }
    }
}

static void add_clock(uint64_t clock, uint64_t delta, uint64_t *result)
{
    if (clock > UINT64_MAX - delta)
        nk_fatal("YM2612 clock overflow");
    *result = clock + delta;
}

static ym_supplementary_frequency *supplementary_frequency_for_operator(
    unsigned operator_index)
{
    unsigned index;

    if (!channel3_mode || operator_index < 8u || operator_index >= 11u)
        return NULL;
    index = operator_index - 8u;
    if (index < 2u)
        index ^= 1u;
    return &ym_ch3_supp[index];
}

static uint64_t timer_period_numerator(unsigned timer)
{
    uint64_t timer_value;
    uint64_t period_microseconds;

    if (!timer) {
        timer_value = ((uint64_t)md_ym.registers[0][0x24] << 2) |
                      (md_ym.registers[0][0x25] & 0x03u);
        period_microseconds = 18u * (1024u - timer_value);
    } else {
        timer_value = md_ym.registers[0][0x26];
        period_microseconds = 288u * (256u - timer_value);
    }
    return period_microseconds * master_clock_hz;
}

static uint64_t timer_first_delta(uint64_t numerator, uint64_t *fraction)
{
    uint64_t whole = numerator / YM_TIMER_CLOCK_DENOMINATOR;
    uint64_t remainder = numerator % YM_TIMER_CLOCK_DENOMINATOR;

    *fraction = remainder;
    if (remainder)
        whole++;
    return whole;
}

static uint64_t timer_next_delta(uint64_t numerator, uint64_t *fraction)
{
    uint64_t whole = numerator / YM_TIMER_CLOCK_DENOMINATOR;
    uint64_t remainder = numerator % YM_TIMER_CLOCK_DENOMINATOR;

    *fraction += remainder;
    if (*fraction >= YM_TIMER_CLOCK_DENOMINATOR) {
        *fraction -= YM_TIMER_CLOCK_DENOMINATOR;
        whole++;
    }
    return whole;
}

static void timer_start(unsigned timer, uint64_t clock)
{
    uint64_t numerator = timer_period_numerator(timer);
    uint64_t delta;
    uint64_t *fraction = timer ? &md_ym.timer_b_fraction : &md_ym.timer_a_fraction;
    uint64_t *deadline = timer ? &md_ym.timer_b_deadline : &md_ym.timer_a_deadline;

    delta = timer_first_delta(numerator, fraction);
    add_clock(clock, delta, deadline);
}

static void timer_reschedule(unsigned timer)
{
    uint64_t numerator = timer_period_numerator(timer);
    uint64_t *fraction = timer ? &md_ym.timer_b_fraction : &md_ym.timer_a_fraction;
    uint64_t *deadline = timer ? &md_ym.timer_b_deadline : &md_ym.timer_a_deadline;
    uint64_t delta = timer_next_delta(numerator, fraction);

    add_clock(*deadline, delta, deadline);
}

static int16_t clamp_s16(int32_t value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

static void sample_push(int16_t mono, int16_t left, int16_t right)
{
    uint64_t index;

    if (sample_write - sample_read >= YM_SAMPLE_CAPACITY)
        nk_fatal("YM2612 sample stream overflow before audio drain");
    index = sample_write % YM_SAMPLE_CAPACITY;
    sample_ring[index] = mono;
    stereo_sample_ring[index * 2u] = left;
    stereo_sample_ring[index * 2u + 1u] = right;
    sample_write++;
    md_ym.sample_count++;
}

static void ym_output_sample(void)
{
    int32_t mono = 0;
    int32_t left = 0;
    int32_t right = 0;
    unsigned channel;

    for (channel = 0; channel < YM_CHANNEL_COUNT; channel++) {
        int32_t output = ym_channels[channel].output;
        int32_t value = output + (output >= 0 ? YM_ZERO_OFFSET : -YM_ZERO_OFFSET);

        mono += output;
        if (ym_channels[channel].lr & 0x80u) {
            left += (value * YM_VOLUME_MULTIPLIER) / YM_VOLUME_DIVISOR;
        } else {
            left += ((value >= 0 ? YM_ZERO_OFFSET : -YM_ZERO_OFFSET) *
                     YM_VOLUME_MULTIPLIER) / YM_VOLUME_DIVISOR;
            left += (value * YM_VOLUME_MULTIPLIER) /
                    (60 * YM_VOLUME_DIVISOR);
        }
        if (ym_channels[channel].lr & 0x40u) {
            right += (value * YM_VOLUME_MULTIPLIER) / YM_VOLUME_DIVISOR;
        } else {
            right += ((value >= 0 ? YM_ZERO_OFFSET : -YM_ZERO_OFFSET) *
                      YM_VOLUME_MULTIPLIER) / YM_VOLUME_DIVISOR;
            right += (value * YM_VOLUME_MULTIPLIER) /
                     (60 * YM_VOLUME_DIVISOR);
        }
    }
    sample_push(clamp_s16(mono), clamp_s16(left), clamp_s16(right));
}

void md_ym_reset(nmd_region region)
{
    unsigned channel, operator;

    memset(&md_ym, 0, sizeof md_ym);
    memset(sample_ring, 0, sizeof sample_ring);
    memset(stereo_sample_ring, 0, sizeof stereo_sample_ring);
    memset(ym_operators, 0, sizeof ym_operators);
    memset(ym_channels, 0, sizeof ym_channels);
    memset(ym_ch3_supp, 0, sizeof ym_ch3_supp);
    sample_read = 0;
    sample_write = 0;
    build_envelope_rate_table();
    md_ym.busy_start = YM_NEVER;
    md_ym.busy_until = YM_NEVER;
    md_ym.timer_a_deadline = YM_NEVER;
    md_ym.timer_b_deadline = YM_NEVER;
    md_ym.next_sample_clock = YM_CLOCK_INC * (YM_OPERATOR_COUNT - 1u);
    master_clock_hz = region == NMD_PAL ? 53203395ull : 53693175ull;
    ym_source_rate = (double)master_clock_hz / YM_MASTER_CLOCKS_PER_SAMPLE;
    next_operator_clock = 0;
    envelope_counter = 0;
    current_op = 0;
    current_env_op = 0;
    channel3_mode = 0;
    lfo_enable = 0;
    lfo_freq = 0;
    lfo_counter = 0;
    lfo_am_step = 0;
    lfo_pm_step = 0;
    for (channel = 0; channel < YM_CHANNEL_COUNT; channel++)
        ym_channels[channel].lr = 0xC0u;
    for (channel = 0; channel < 3; channel++) {
        md_ym.registers[0][0xB4u + channel] = 0xC0u;
        md_ym.registers[1][0xB4u + channel] = 0xC0u;
    }
    for (operator = 0; operator < YM_OPERATOR_COUNT; operator++) {
        ym_operators[operator].envelope = YM_MAX_ENVELOPE;
        ym_operators[operator].env_phase = YM_ENV_RELEASE;
    }
}

double md_ym_source_rate(void)
{
    return ym_source_rate;
}

uint64_t md_ym_next_event(void)
{
    uint64_t next = YM_NEVER;

    if (md_ym.timer_a_running)
        next = md_ym.timer_a_deadline;
    if (md_ym.timer_b_running && md_ym.timer_b_deadline < next)
        next = md_ym.timer_b_deadline;
    return next;
}

static int16_t lfo_phase_delta(uint16_t fnum, uint8_t pms, uint8_t step)
{
    unsigned bit;
    unsigned shift;
    int value = 0;

    if (!pms || !lfo_enable)
        return 0;
    for (bit = 0x40u, shift = 0; bit; bit >>= 1, shift++) {
        unsigned phase = (step & 0x08u) ? ((7u - step) & 7u) : (step & 7u);
        if ((fnum >> 4) & bit)
            value += lfo_pm_base[pms][phase] >> shift;
    }
    if (step & 0x10u)
        value = -value;
    return (int16_t)value;
}

static uint32_t ym_phase_increment(unsigned operator_index)
{
    unsigned channel_index = operator_index / 4u;
    ym_operator_state *operator = &ym_operators[operator_index];
    ym_channel_state *channel = &ym_channels[channel_index];
    ym_supplementary_frequency *supplementary =
        supplementary_frequency_for_operator(operator_index);
    uint16_t fnum = channel->fnum;
    uint8_t block = channel->block;
    uint8_t keycode = channel->keycode;
    uint32_t increment;
    uint32_t delta;

    if (supplementary) {
        fnum = supplementary->fnum;
        block = supplementary->block;
        keycode = supplementary->keycode;
    }

    increment = fnum;
    if (channel->pms) {
        increment = increment * 2u +
                    (uint16_t)lfo_phase_delta(fnum, channel->pms, lfo_pm_step);
        increment &= 0x0FFFu;
    }
    if (block == 0)
        increment >>= 1;
    else
        increment <<= block - 1u;

    if (channel->pms)
        increment >>= 1;

    delta = detune_delta[keycode & 0x1Fu][operator->detune & 0x03u];
    if (operator->detune & 0x04u)
        increment = (increment - delta) & 0x1FFFFu;
    else
        increment += delta;
    if (operator->multiple)
        increment = (increment * operator->multiple) & 0xFFFFFu;
    else
        increment >>= 1;
    return increment;
}

static uint8_t effective_envelope_rate(const ym_operator_state *operator,
                                       const ym_channel_state *channel,
                                       unsigned operator_index, unsigned phase)
{
    unsigned rate = operator->rates[phase];
    const ym_supplementary_frequency *supplementary =
        supplementary_frequency_for_operator(operator_index);

    if (rate)
        rate = rate * 2u +
               ((supplementary ? supplementary->keycode : channel->keycode) >>
                operator->key_scaling);
    return (uint8_t)(rate > 63u ? 63u : rate);
}

static void start_envelope(ym_operator_state *operator, const ym_channel_state *channel,
                           unsigned operator_index)
{
    uint8_t rate = effective_envelope_rate(operator, channel, operator_index,
                                           YM_ENV_ATTACK);

    if (rate >= 62u) {
        operator->env_phase = YM_ENV_DECAY;
        operator->envelope = 0;
    } else {
        operator->env_phase = YM_ENV_ATTACK;
    }
}

static void key_on(ym_operator_state *operator, const ym_channel_state *channel,
                   unsigned operator_index)
{
    start_envelope(operator, channel, operator_index);
    operator->phase_counter = 0;
    operator->inverted = operator->ssg & YM_SSG_INVERT;
}

static void key_off(ym_operator_state *operator)
{
    operator->env_phase = YM_ENV_RELEASE;
    if (operator->inverted) {
        operator->inverted = 0;
        operator->envelope = (uint16_t)((YM_SSG_CENTER - operator->envelope) &
                                        YM_MAX_ENVELOPE);
    }
}

static void envelope_tick(ym_operator_state *operator, ym_channel_state *channel,
                          unsigned operator_index)
{
    uint8_t rate;
    unsigned cycle_shift, update_cycle;
    uint16_t increment;

    if (operator->env_phase == YM_ENV_DECAY &&
        operator->envelope >= operator->sustain_level)
        operator->env_phase = YM_ENV_SUSTAIN;
    rate = effective_envelope_rate(operator, channel, operator_index,
                                   operator->env_phase);
    cycle_shift = rate < 0x30u ? ((0x2Fu - rate) >> 2) : 0u;
    if (envelope_counter & ((1u << cycle_shift) - 1u))
        return;
    update_cycle = (envelope_counter >> cycle_shift) & 7u;
    increment = envelope_rate_table[rate * 8u + update_cycle];
    if (operator->env_phase == YM_ENV_ATTACK) {
        uint16_t old = operator->envelope;
        uint32_t attack = (uint32_t)(~operator->envelope) * increment;
        operator->envelope = (uint16_t)(operator->envelope + ((attack >> 4) & 0xFFFFFFFCu));
        if (operator->envelope > old)
            operator->envelope = 0;
        if (!operator->envelope)
            operator->env_phase = YM_ENV_DECAY;
    } else {
        if (operator->ssg) {
            if (operator->envelope < YM_SSG_CENTER)
                increment = (uint16_t)(increment * 4u);
            else
                increment = 0;
        }
        operator->envelope = (uint16_t)(operator->envelope + (increment << 2));
        if (operator->envelope > YM_MAX_ENVELOPE ||
            (operator->env_phase == YM_ENV_RELEASE &&
             operator->envelope >= YM_SSG_CENTER))
            operator->envelope = YM_MAX_ENVELOPE;
    }
}

static void lfo_tick(void)
{
    if (!lfo_enable) {
        lfo_am_step = 0;
        lfo_pm_step = 0;
        return;
    }
    if (lfo_counter >= lfo_timer_values[lfo_freq]) {
        lfo_counter = 0;
        lfo_am_step = (uint8_t)((lfo_am_step + 2u) & 0xFEu);
        lfo_pm_step = (uint8_t)(lfo_am_step / 8u);
    } else {
        lfo_counter++;
    }
}

static int16_t sine_output(uint16_t phase, int16_t modulation, uint16_t envelope)
{
    unsigned phase_index = (unsigned)(phase + modulation) & 0x03FFu;
    unsigned table_index = phase_index & 0x01FFu;
    uint32_t attenuation, magnitude;
    int shift;

    if (envelope > YM_MAX_ENVELOPE)
        envelope = YM_MAX_ENVELOPE;
    if (table_index > 255u)
        table_index = 511u - table_index;
    attenuation = log_sine_quarter[table_index] + envelope;
    magnitude = power_fraction[attenuation & 0xFFu];
    shift = (int)(attenuation >> 8) - 2;
    if (shift < 0)
        magnitude <<= (unsigned)(-shift);
    else if (shift < 16)
        magnitude >>= (unsigned)shift;
    else
        magnitude = 0;
    if (phase_index & 0x0200u)
        return (int16_t)-(int32_t)magnitude;
    return (int16_t)magnitude;
}

static int32_t operator_modulation(unsigned operator_index)
{
    unsigned channel_index = operator_index / 4u;
    unsigned slot = operator_index & 3u;
    unsigned base = channel_index * 4u;
    ym_channel_state *channel = &ym_channels[channel_index];
    int32_t modulation = 0;

    if (!slot) {
        if (channel->feedback)
            return (channel->op1_old + ym_operators[operator_index].output) >>
                   (10u - channel->feedback);
        return 0;
    }

    switch (channel->algorithm & 7u) {
    case 0:
        if (slot == 1u) modulation = ym_operators[base + 2u].output;
        else if (slot == 2u) modulation = ym_operators[base].output;
        else modulation = ym_operators[base + 1u].output;
        break;
    case 1:
        if (slot == 1u) modulation = channel->op1_old + ym_operators[base + 2u].output;
        else if (slot == 3u) modulation = ym_operators[base + 1u].output;
        break;
    case 2:
        if (slot == 1u) modulation = ym_operators[base + 2u].output;
        else if (slot == 3u)
            modulation = ym_operators[base].output + ym_operators[base + 1u].output;
        break;
    case 3:
        if (slot == 2u) modulation = ym_operators[base].output;
        else if (slot == 3u) modulation = channel->op2_old + ym_operators[base + 1u].output;
        break;
    case 4:
        if (slot == 2u) modulation = ym_operators[base].output;
        else if (slot == 3u) modulation = ym_operators[base + 1u].output;
        break;
    case 5:
        if (slot == 1u) modulation = channel->op1_old;
        else if (slot == 2u || slot == 3u) modulation = ym_operators[base].output;
        break;
    case 6:
        if (slot == 2u) modulation = ym_operators[base].output;
        break;
    case 7:
        break;
    }
    return modulation >> 1;
}

static int16_t clip_channel_output(int32_t output)
{
    if (output > 0x1FE0)
        output = 0x1FE0;
    else if (output < -0x1FF0)
        output = -0x1FF0;
    if (output >= 0)
        output &= ~0x1F;
    else
        output = -(((-output + 0x1F) & ~0x1F));
    return (int16_t)output;
}

static void update_channel_output(unsigned channel_index)
{
    ym_channel_state *channel = &ym_channels[channel_index];
    unsigned base = channel_index * 4u;
    int32_t output = 0;
    unsigned op;

    if (channel->algorithm < 4u) {
        output = ym_operators[base + 3u].output;
    } else if (channel->algorithm == 4u) {
        output = (ym_operators[base + 3u].output & ~0x1F) +
                 (ym_operators[base + 2u].output & ~0x1F);
    } else {
        unsigned first = channel->algorithm == 7u ? 0u : 1u;
        for (op = first; op < 4u; op++)
            output = clip_channel_output(output + (ym_operators[base + op].output & ~0x1F));
    }
    channel->output = clip_channel_output(output);
}

static void operator_cycle(void)
{
    unsigned operator_index = current_op;
    unsigned channel_index = operator_index / 4u;
    unsigned slot = operator_index & 3u;
    ym_operator_state *operator = &ym_operators[operator_index];
    ym_channel_state *channel = &ym_channels[channel_index];

    if (!current_op)
        lfo_tick();
    if (!(current_op % 3u)) {
        envelope_tick(&ym_operators[current_env_op],
                      &ym_channels[current_env_op / 4u], current_env_op);
        current_env_op++;
        if (current_env_op == YM_OPERATOR_COUNT) {
            current_env_op = 0;
            envelope_counter++;
        }
    }

    if (channel_index != 5u || !md_ym.dac_enabled) {
        uint16_t phase = (uint16_t)(operator->phase_counter >> 10) & 0x03FFu;
        uint32_t old_phase = operator->phase_counter;
        int16_t previous_output = operator->output;
        int32_t modulation = operator_modulation(operator_index);
        uint16_t envelope = operator->envelope;

        operator->phase_increment = ym_phase_increment(operator_index);
        operator->phase_counter = (operator->phase_counter + operator->phase_increment) &
                                  0xFFFFFu;
        operator->phase_overflow =
            (old_phase & 0xFFFFFu) > (operator->phase_counter & 0xFFFFFu);
        if (operator->ssg && envelope >= YM_SSG_CENTER) {
            if (operator->ssg & YM_SSG_ALTERNATE) {
                if (operator->env_phase != YM_ENV_RELEASE &&
                    (!(operator->ssg & YM_SSG_HOLD) ||
                     ((operator->ssg ^ operator->inverted) & YM_SSG_INVERT) == 0))
                    operator->inverted ^= YM_SSG_INVERT;
            } else if (!(operator->ssg & YM_SSG_HOLD)) {
                phase = 0;
                operator->phase_counter = 0;
            }
            if ((operator->env_phase == YM_ENV_DECAY ||
                 operator->env_phase == YM_ENV_SUSTAIN) &&
                !(operator->ssg & YM_SSG_HOLD)) {
                start_envelope(operator, channel, operator_index);
                envelope = operator->envelope;
            }
        }
        if (operator->inverted)
            envelope = (uint16_t)((YM_SSG_CENTER - envelope) & YM_MAX_ENVELOPE);
        envelope = (uint16_t)(envelope + operator->total_level);
        if (operator->am && lfo_enable) {
            uint16_t base_am = (lfo_am_step & 0x80u ? lfo_am_step : (uint8_t)~lfo_am_step) & 0x7Eu;
            int shift = ams_shift[channel->ams];
            if (shift >= 0)
                envelope = (uint16_t)(envelope + ((base_am >> shift) & YM_MAX_ENVELOPE));
            else
                envelope = (uint16_t)(envelope + (base_am << (unsigned)-shift));
        }
        if (envelope > YM_MAX_ENVELOPE)
            envelope = YM_MAX_ENVELOPE;
        operator->output = sine_output(phase, (int16_t)modulation, envelope);
        if (!slot)
            channel->op1_old = previous_output;
        else if (slot == 2u)
            channel->op2_old = previous_output;
        if (slot == 3u)
            update_channel_output(channel_index);
    }

    current_op++;
    if (current_op == YM_OPERATOR_COUNT) {
        current_op = 0;
        ym_output_sample();
        md_ym.next_sample_clock = next_operator_clock +
                                  YM_CLOCK_INC * YM_OPERATOR_COUNT;
    }
}

void md_ym_advance_to(uint64_t master_clock)
{
    if (master_clock < md_ym.current_clock)
        nk_fatal("YM2612 timestamp moved backward");

    for (;;) {
        uint64_t next_timer = md_ym_next_event();
        int timer_due = next_timer != YM_NEVER && next_timer <= master_clock;
        int operator_due = next_operator_clock < master_clock;

        if (!timer_due && !operator_due)
            break;

        if (timer_due && (!operator_due || next_timer <= next_operator_clock)) {
            if (md_ym.timer_a_running && md_ym.timer_a_deadline == next_timer) {
                if (md_ym.registers[0][0x27] & 0x04u)
                    md_ym.status |= YM_STATUS_TIMER_A;
                timer_reschedule(0);
            }
            if (md_ym.timer_b_running && md_ym.timer_b_deadline == next_timer) {
                if (md_ym.registers[0][0x27] & 0x08u)
                    md_ym.status |= YM_STATUS_TIMER_B;
                timer_reschedule(1);
            }
        } else {
            if (next_operator_clock > UINT64_MAX - YM_CLOCK_INC)
                nk_fatal("YM2612 operator clock overflow");
            operator_cycle();
            next_operator_clock += YM_CLOCK_INC;
        }
    }
    md_ym.current_clock = master_clock;
}

int md_ym_take_samples(int16_t *samples, int max_samples)
{
    int count = 0;

    if (max_samples < 0 || (max_samples && !samples))
        nk_fatal("invalid YM2612 sample drain buffer");
    while (sample_read != sample_write && count < max_samples) {
        samples[count++] = sample_ring[sample_read % YM_SAMPLE_CAPACITY];
        sample_read++;
    }
    return count;
}

int md_ym_take_stereo_samples(int16_t *samples, int max_frames)
{
    int count = 0;

    if (max_frames < 0 || (max_frames && !samples))
        nk_fatal("invalid YM2612 stereo sample drain buffer");
    while (sample_read != sample_write && count < max_frames) {
        uint64_t index = sample_read % YM_SAMPLE_CAPACITY;
        samples[count * 2] = stereo_sample_ring[index * 2u];
        samples[count * 2 + 1] = stereo_sample_ring[index * 2u + 1u];
        sample_read++;
        count++;
    }
    return count;
}

static uint8_t ym_status(uint64_t clock)
{
    uint8_t status = md_ym.status;

    if (clock >= md_ym.busy_start && clock < md_ym.busy_until)
        status |= YM_STATUS_BUSY;
    return status;
}

uint8_t md_ym_read(uint8_t port, uint64_t clock)
{
    md_ym_advance_to(clock);
    if (port > 3u)
        nk_fatal("YM2612 port %u is outside 0-3", (unsigned)port);
    /* Sega documents status reads from all four byte ports. */
    return ym_status(clock);
}

static void timer_control_write(uint8_t value, uint8_t old, uint64_t clock)
{
    if (value & 0x10u)
        md_ym.status &= (uint8_t)~YM_STATUS_TIMER_A;
    if (value & 0x20u)
        md_ym.status &= (uint8_t)~YM_STATUS_TIMER_B;

    if (value & 0x01u) {
        if (!(old & 0x01u)) {
            md_ym.timer_a_running = 1;
            timer_start(0, clock);
        }
    } else {
        md_ym.timer_a_running = 0;
        md_ym.timer_a_deadline = YM_NEVER;
    }
    if (value & 0x02u) {
        if (!(old & 0x02u)) {
            md_ym.timer_b_running = 1;
            timer_start(1, clock);
        }
    } else {
        md_ym.timer_b_running = 0;
        md_ym.timer_b_deadline = YM_NEVER;
    }
    channel3_mode = value & 0xC0u;
}

static void keyon_write(uint8_t value)
{
    static const uint8_t slot_bits[4] = { 0x10u, 0x40u, 0x20u, 0x80u };
    unsigned channel_index = value & 7u;
    unsigned slot;
    uint8_t changes;

    if (channel_index == 3u || channel_index == 7u)
        return;
    if (channel_index > 2u)
        channel_index--;
    changes = (uint8_t)((value ^ ym_channels[channel_index].keyon) & 0xF0u);
    ym_channels[channel_index].keyon = value & 0xF0u;
    for (slot = 0; slot < 4; slot++) {
        if (changes & slot_bits[slot]) {
            ym_operator_state *operator = &ym_operators[channel_index * 4u + slot];
            if (value & slot_bits[slot])
                key_on(operator, &ym_channels[channel_index],
                       channel_index * 4u + slot);
            else
                key_off(operator);
        }
    }
}

static void update_keycode(ym_channel_state *channel)
{
    channel->block = (channel->fnum_latch >> 3) & 7u;
    channel->fnum = (uint16_t)(((channel->fnum_latch & 7u) << 8) |
                               (channel->fnum & 0xFFu));
    channel->keycode = (uint8_t)((channel->block << 2) |
                                 fnum_to_keycode[(channel->fnum >> 7) & 0x0Fu]);
}

static void channel_register_write(unsigned bank, uint8_t address, uint8_t value)
{
    unsigned local = address & 3u;
    unsigned channel_index;
    ym_channel_state *channel;

    if (local == 3u)
        return;
    channel_index = bank * 3u + local;
    channel = &ym_channels[channel_index];
    switch (address & 0xFCu) {
    case 0xA0u:
        channel->fnum = (uint16_t)((channel->fnum & 0x0700u) | value);
        update_keycode(channel);
        break;
    case 0xA4u:
        channel->fnum_latch = value;
        break;
    case 0xA8u:
        if (!bank && local < 3u) {
            ym_ch3_supp[local].block = (ym_ch3_supp[local].fnum_latch >> 3) & 7u;
            ym_ch3_supp[local].fnum = (uint16_t)(((ym_ch3_supp[local].fnum_latch & 7u) << 8) |
                                                 value);
            ym_ch3_supp[local].keycode = (uint8_t)((ym_ch3_supp[local].block << 2) |
                fnum_to_keycode[(ym_ch3_supp[local].fnum >> 7) & 0x0Fu]);
        }
        break;
    case 0xACu:
        if (!bank && local < 3u)
            ym_ch3_supp[local].fnum_latch = value;
        break;
    case 0xB0u:
        channel->algorithm = value & 7u;
        channel->feedback = (value >> 3) & 7u;
        break;
    case 0xB4u:
        channel->lr = value & 0xC0u;
        channel->ams = (value >> 4) & 3u;
        channel->pms = value & 7u;
        break;
    }
}

static void register_write(unsigned bank, uint8_t address, uint8_t value,
                           uint64_t clock)
{
    uint8_t old = md_ym.registers[bank][address];
    unsigned local, slot, channel_index;

    if (bank && address < 0x30u)
        return;
    md_ym.registers[bank][address] = value;

    if (address < 0x30u) {
        if (address == 0x22u) {
            lfo_enable = value & 0x08u;
            lfo_freq = value & 7u;
            if (!lfo_enable) {
                lfo_counter = 0;
                lfo_am_step = 0;
                lfo_pm_step = 0;
            }
        } else if (address == 0x27u) {
            timer_control_write(value, old, clock);
        } else if (address == 0x28u) {
            keyon_write(value);
        } else if (address == 0x2Bu) {
            md_ym.dac_enabled = (uint8_t)((value & 0x80u) != 0);
        } else if (address == 0x2Au) {
            md_ym.dac_data = value;
            if (md_ym.dac_enabled) {
                md_ym.dac_sample = (int16_t)(((int)value - 0x80) * 64);
                ym_channels[5].output = md_ym.dac_sample;
            }
        }
        return;
    }

    if (address < 0xA0u) {
        local = address & 3u;
        if (local == 3u)
            return;
        channel_index = bank * 3u + local;
        slot = (address >> 2) & 3u;
        {
            ym_operator_state *operator = &ym_operators[channel_index * 4u + slot];
            switch (address & 0xF0u) {
            case 0x30u:
                operator->detune = (value >> 4) & 7u;
                operator->multiple = value & 0x0Fu;
                break;
            case 0x40u:
                operator->total_level = (value & 0x7Fu) << 5;
                break;
            case 0x50u:
                operator->key_scaling = 3u - (value >> 6);
                operator->rates[YM_ENV_ATTACK] = value & 0x1Fu;
                break;
            case 0x60u:
                operator->am = value & 0x80u;
                operator->rates[YM_ENV_DECAY] = value & 0x1Fu;
                break;
            case 0x70u:
                operator->rates[YM_ENV_SUSTAIN] = value & 0x1Fu;
                break;
            case 0x80u:
                operator->rates[YM_ENV_RELEASE] = (uint8_t)(((value & 0x0Fu) << 1) | 1u);
                operator->sustain_level = (value & 0xF0u) << 3;
                if (operator->sustain_level == 0x780u)
                    operator->sustain_level = YM_MAX_ENVELOPE;
                break;
            case 0x90u:
                operator->ssg = value & YM_SSG_ENABLE ? value : 0;
                break;
            }
        }
        return;
    }

    channel_register_write(bank, address, value);
}

void md_ym_write(uint8_t port, uint8_t value, uint64_t clock)
{
    unsigned bank;

    md_ym_advance_to(clock);
    if (port > 3u)
        nk_fatal("YM2612 port %u is outside 0-3", (unsigned)port);
    bank = port >> 1;
    if (!(port & 1u)) {
        md_ym.address_latch[bank] = value;
        return;
    }

    md_ym.busy_start = clock + YM_BUSY_START_DELAY;
    if (md_ym.busy_start < clock ||
        md_ym.busy_start > UINT64_MAX - YM_BUSY_CYCLES * YM_CLOCK_INC)
        md_ym.busy_until = UINT64_MAX;
    else
        md_ym.busy_until = md_ym.busy_start + YM_BUSY_CYCLES * YM_CLOCK_INC;
    register_write(bank, md_ym.address_latch[bank], value, clock);
}
