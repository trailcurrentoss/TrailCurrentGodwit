/*
 * Quadrature decoding for a mechanical detented encoder, as a state machine.
 *
 * A detent is reported only when the phases have gone through one complete,
 * valid cycle. At speed the encoder's contact chatter (up to 5 ms on the EC3501)
 * is as long as the time between its edges (3 ms at 80 detents a second), so
 * edges overlap. Counting them, as a hardware pulse counter does, lets chatter
 * run the count backwards: on hardware, a quarter of the detents reported in a
 * fast spin went the wrong way. Here, chatter on one phase only moves back and
 * forth between neighbouring states, and a garbled sequence falls back to the
 * start. So what chatter costs is a missed detent. To report one the wrong way
 * it would have to fake a whole cycle in reverse.
 *
 * Plain C with no dependencies, so the test can run on a PC.
 */
#pragma once

#include <stdint.h>

/* The phases: bit 1 = A, bit 0 = B, each 1 when high. Turning the count up goes
 * 11 -> 01 -> 00 -> 10 -> 11. */
enum {
    QUAD_START, QUAD_UP_BEGIN, QUAD_UP_MID, QUAD_UP_FINAL,
    QUAD_DOWN_BEGIN, QUAD_DOWN_MID, QUAD_DOWN_FINAL,
    QUAD_EMIT_UP = 0x10,
    QUAD_EMIT_DOWN = 0x20,
};

static const uint8_t quad_table[7][4] = {
    /* phases:        00            01             10              11 */
    [QUAD_START]      = { QUAD_START,    QUAD_UP_BEGIN, QUAD_DOWN_BEGIN, QUAD_START },
    [QUAD_UP_BEGIN]   = { QUAD_UP_MID,   QUAD_UP_BEGIN, QUAD_START,      QUAD_START },
    [QUAD_UP_MID]     = { QUAD_UP_MID,   QUAD_UP_BEGIN, QUAD_UP_FINAL,   QUAD_START },
    [QUAD_UP_FINAL]   = { QUAD_UP_MID,   QUAD_START,    QUAD_UP_FINAL,   QUAD_START | QUAD_EMIT_UP },
    [QUAD_DOWN_BEGIN] = { QUAD_DOWN_MID, QUAD_START,    QUAD_DOWN_BEGIN, QUAD_START },
    [QUAD_DOWN_MID]   = { QUAD_DOWN_MID, QUAD_DOWN_FINAL, QUAD_DOWN_BEGIN, QUAD_START },
    [QUAD_DOWN_FINAL] = { QUAD_DOWN_MID, QUAD_DOWN_FINAL, QUAD_START,    QUAD_START | QUAD_EMIT_DOWN },
};

/* One sample of the phases: +1 or -1 when a detent completes, otherwise 0 */
static inline int quad_step(uint8_t *state, unsigned phases)
{
    const uint8_t next = quad_table[*state & 7][phases & 3];
    *state = next & 7;
    return (next & QUAD_EMIT_UP) ? 1 : (next & QUAD_EMIT_DOWN) ? -1 : 0;
}
