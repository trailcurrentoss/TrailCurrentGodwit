/*
 * Runs the dial's quadrature decoder against a simulated encoder: real edges at
 * a chosen speed, with contact chatter on top, sampled as the firmware samples.
 * No hardware or ESP-IDF needed:
 *
 *   cc -O2 -I../components/crowpanel_board/src quad_decoder_test.c -o /tmp/quad && /tmp/quad
 */
#include "quad_decoder.h"

#include <stdio.h>
#include <stdlib.h>

#define SAMPLE_US 500               /* as board_input.c samples */

static int failures;

static void check(int ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

/* Phases going up: 11 01 00 10 */
static const unsigned UP[4] = { 3, 1, 0, 2 };

/* A contact that chatters for a while after each change: it reads randomly until
 * the chatter ends, then settles at its true level */
struct contact { int level; long settle_us; };

static int read_contact(const struct contact *c, long t)
{
    return t < c->settle_us ? rand() & 1 : c->level;
}

/* A steady spin: returns detents reported each way */
static void spin(int dir, int detents, double per_second, long chatter_us, int *up, int *down)
{
    const long edge_us = (long)(1e6 / per_second / 4);
    const long end = edge_us * 4L * detents + 20000;
    struct contact a = { 1, 0 }, b = { 1, 0 };
    uint8_t st = QUAD_START;
    int pos = 0;
    long next_edge = edge_us;
    *up = *down = 0;
    for (long t = 0; t < end; t += SAMPLE_US) {
        while (t >= next_edge && next_edge <= edge_us * 4L * detents) {
            pos += dir;
            const unsigned p = UP[((pos % 4) + 4) % 4];
            const int na = (p >> 1) & 1, nb = p & 1;
            if (na != a.level) { a.level = na; a.settle_us = next_edge + (chatter_us ? rand() % chatter_us : 0); }
            if (nb != b.level) { b.level = nb; b.settle_us = next_edge + (chatter_us ? rand() % chatter_us : 0); }
            next_edge += edge_us;
        }
        const unsigned phases = ((unsigned)read_contact(&a, t) << 1) | (unsigned)read_contact(&b, t);
        const int r = quad_step(&st, phases);
        *up += r > 0;
        *down += r < 0;
    }
}

int main(void)
{
    int up, down;
    srand(1);

    spin(1, 10, 5, 0, &up, &down);
    check(up == 10 && down == 0, "clean, turned up: 10 detents up");
    spin(-1, 10, 5, 0, &up, &down);
    check(up == 0 && down == 10, "clean, turned down: 10 detents down");

    spin(1, 1, 5, 2000, &up, &down);
    check(up == 1 && down == 0, "one click with chatter is one click");

    static const struct { double rate; long chatter; double limit; const char *name; } cases[] = {
        { 4, 5000, 0.01, "4 detents/s, 5 ms chatter" },
        { 30, 2000, 0.01, "30 detents/s, 2 ms chatter" },
        { 30, 5000, 0.01, "30 detents/s, 5 ms chatter" },
        { 80, 2000, 0.01, "80 detents/s, 2 ms chatter" },
        /* The known limit: every edge overlaps the next one's chatter, here pure
         * noise for up to 5 ms. Detents are lost and a few go astray. The pulse
         * counter this replaced got 25-30% wrong on a real fast spin. */
        { 80, 5000, 0.05, "80 detents/s, 5 ms chatter (worst case)" },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        long right = 0, wrong = 0;
        const int runs = 2000, n = 20;
        for (int k = 0; k < runs; ++k) {
            int dir = (k & 1) ? 1 : -1;
            spin(dir, n, cases[i].rate, cases[i].chatter, &up, &down);
            right += dir > 0 ? up : down;
            wrong += dir > 0 ? down : up;
        }
        const double total = runs * n;
        printf("     %-68s reported %5.1f%%, wrong way %.3f%%\n", cases[i].name, 100 * right / total, 100 * wrong / total);
        check(wrong / total < cases[i].limit, cases[i].limit < 0.02 ? "under 1% go the wrong way" : "under 5% go the wrong way");
    }
    printf("%s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
