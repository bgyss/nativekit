#include <stdio.h>
#include <string.h>

#include "session.h"
#include "fatal.h"

static FILE *poll_file;
static int reading;
static unsigned long long poll_index;

void nk_session_polls_open(const char *path, int replay)
{
    static const char signature[] = "NKP1";
    char header[4];

    if (poll_file)
        nk_fatal("only one poll recording or replay may be active");
    poll_file = fopen(path, replay ? "rb" : "wb");
    if (!poll_file)
        nk_fatal("cannot open poll stream %s", path);
    reading = replay;
    poll_index = 0;
    if (reading) {
        if (fread(header, 1, 4, poll_file) != 4 || memcmp(header, signature, 4))
            nk_fatal("invalid poll stream %s", path);
    } else if (fwrite(signature, 1, 4, poll_file) != 4) {
        nk_fatal("cannot write poll stream %s", path);
    }
}

uint32_t nk_session_poll(unsigned player, uint32_t mask)
{
    uint8_t record[5];

    if (!poll_file)
        return mask;
    ++poll_index;
    if (player >= NK_PLAYERS)
        nk_fatal("invalid player at poll %llu", poll_index);
    if (reading) {
        if (fread(record, 1, sizeof record, poll_file) != sizeof record)
            nk_fatal("poll stream ended at poll %llu", poll_index);
        if (record[0] != player)
            nk_fatal("poll %llu requested player %u, recorded %u",
                     poll_index, player, (unsigned)record[0]);
        return record[1] | (uint32_t)record[2] << 8 |
               (uint32_t)record[3] << 16 | (uint32_t)record[4] << 24;
    }
    record[0] = (uint8_t)player;
    for (unsigned i = 0; i < 4; ++i)
        record[i + 1] = (uint8_t)(mask >> (8 * i));
    if (fwrite(record, 1, sizeof record, poll_file) != sizeof record)
        nk_fatal("cannot write poll %llu", poll_index);
    return mask;
}
