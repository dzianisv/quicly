/*
 * Copyright (c) 2021 Fastly, Kazuho Oku
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "quicly/pacer.h"
#include "test.h"

static void test_calc_rate(void)
{
    uint64_t bytes_per_msec;

    bytes_per_msec = quicly_pacer_calc_send_rate(50 * 1200, 10);
    ok(bytes_per_msec == 12000); /* send 60KB packets in 5ms */

    bytes_per_msec = quicly_pacer_calc_send_rate(100 * 1200, 10);
    ok(bytes_per_msec == 24000); /* 2x CWND, 2x flow rate */

    bytes_per_msec = quicly_pacer_calc_send_rate(50 * 1200, 100);
    ok(bytes_per_msec == 1200); /* 10x RTT, 1/10 flow rate */
}

static const uint16_t mtu = 1200;

struct pattern {
    int64_t at;
    size_t avail;
    size_t consume;
};

static int64_t test_pattern(quicly_pacer_t *pacer, int64_t now, const uint32_t bytes_per_msec, const struct pattern *expected)
{
    for (; expected->at != 0; ++expected) {
        int64_t send_at = quicly_pacer_can_send_at(pacer, bytes_per_msec, mtu);
        if (now == expected->at) {
            ok(send_at <= now);
        } else {
            ok(send_at == expected->at);
            now = send_at;
        }
        size_t window = quicly_pacer_get_window(pacer, now, bytes_per_msec, mtu);
        ok(window == expected->avail);
        quicly_pacer_consume_window(pacer, expected->consume);
    }

    return now;
}

static void test_medium(void)
{
    const uint32_t bytes_per_msec = 4 * mtu;
    quicly_pacer_t pacer;
    int64_t now = 1;

    quicly_pacer_reset(&pacer);

    /* 3x pacer-restricted, then non-pacer-restricted */
    now = test_pattern(&pacer, now, bytes_per_msec,
                       (const struct pattern[]){
                           {1, 10 * mtu, 10 * mtu},
                           {2, 4 * mtu, 4 * mtu},
                           {3, 4 * mtu, 4 * mtu},
                           {4, 4 * mtu, 1 * mtu},
                           {0},
                       });
    ok(now == 4);

    /* in the next millisecond, we have new data to send, and we borrow 3mtu from the previous millisec */
    now = 5;
    now = test_pattern(&pacer, now, bytes_per_msec,
                       (const struct pattern[]){
                           {5, 7 * mtu, 7 * mtu},
                           {6, 4 * mtu, 1 * mtu},
                           {0},
                       });
    ok(now == 6);

    /* skip 2ms, and we can send a burst */
    now = 8;
    now = test_pattern(&pacer, now, bytes_per_msec,
                       (const struct pattern[]){
                           {8, 10 * mtu, 10 * mtu},
                           {9, 4 * mtu, 1 * mtu},
                           {0},
                       });
    ok(now == 9);
}

static void test_slow(void)
{
    const uint32_t bytes_per_msec = 500;
    quicly_pacer_t pacer;
    int64_t now = 1;

    quicly_pacer_reset(&pacer);

    now = test_pattern(&pacer, now, bytes_per_msec,
                       (const struct pattern[]){
                           {1, 10 * mtu, 10 * mtu},
                           {2, 1 * mtu, 1 * mtu},  /* borrowing 700 bytes */
                           {4, 1 * mtu, 1 * mtu},  /* borrowing 900 bytes */
                           {6, 1 * mtu, 1 * mtu},  /* borrowing 1100 bytes */
                           {9, 1 * mtu, 1 * mtu},  /* borrowing 800 bytes, after 3ms  */
                           {11, 1 * mtu, 1 * mtu}, /* borrowing 1000 bytes */
                           {14, 1 * mtu, 1 * mtu}, /* borrowing 700 bytes, after 3ms */
                           {16, 1 * mtu, 1 * mtu}, /* borrowing 900 bytes */
                           {18, 1 * mtu, 1 * mtu}, /* borrowing 1100 bytes */
                           {21, 1 * mtu, 1 * mtu}, /* borrowing 800 bytes, after 3ms  */
                           {0},
                       });
}

void test_pacer(void)
{
    subtest("calc-rate", test_calc_rate);
    subtest("medium", test_medium);
    subtest("slow", test_slow);
}
