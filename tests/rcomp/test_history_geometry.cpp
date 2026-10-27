// SPDX-License-Identifier: GPL-3.0-or-later
#include "HistoryGeometry.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace tsunami::rcomp;
static void check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
}
struct Band { int rate; unsigned H, log2H, A, second, shift; };
int main() {
    // Expected band table read from the reference geometry writer; edges on both sides.
    const Band bands[]{{1, 64, 6, 27, 38, 11}, {44100, 64, 6, 27, 38, 11},
        {48000, 64, 6, 27, 38, 11}, {63999, 64, 6, 27, 38, 11},
        {64000, 128, 7, 54, 75, 12}, {96000, 128, 7, 54, 75, 12},
        {127999, 128, 7, 54, 75, 12}, {128000, 256, 8, 108, 149, 14},
        {192000, 256, 8, 108, 149, 14}, {384000, 256, 8, 108, 149, 14}};
    for (const Band& b : bands) {
        HistoryGeometry g{};
        check(historyGeometryForRate(b.rate, g), "valid rate accepted");
        check(g.delay == b.H && g.delayLog2 == b.log2H && g.firstSpan == b.A
            && g.secondRectangle == b.second && g.shift == b.shift, "band table");
        // Independent invariants, not a copy of the writer's arithmetic.
        check(g.secondRectangle == g.delay - g.firstSpan + 1, "second rectangle = H-A+1");
        check(g.middleSpan == g.secondRectangle - g.firstSpan, "kernel middle span");
        check(2 * g.delay + 2 * g.firstSpan + g.middleSpan == g.sharedLength(1)
            && g.sharedLength(1) == 3 * g.delay + 1, "mono ring R=2H+2A+B=3H+1");
        const unsigned product = g.firstSpan * g.secondRectangle;
        check((1u << (g.shift - 1)) < product && product <= (1u << g.shift), "shift=ceil(log2 product)");
        check(std::abs(g.normalization * product / std::ldexp(1.0, static_cast<int>(g.shift)) - 1.0) < 1e-15,
            "normalization");
        check(std::abs(g.normalization * g.inverseNormalization - 1.0) < 1e-15, "reciprocal");
        const PrefillLayout p = prefillLayout(g);
        check(p.zeroHead == g.delay && p.constantStart == g.delay
            && p.constantCount == g.delay + 1 && p.constant == 0.0625
            && p.constantStart + p.constantCount <= g.sharedLength(1), "nonzero prefill span");
    }
    std::cout << "PASS rate bands and edges, spans, shift, normalization, prefill layout\n";
    HistoryGeometry kept{};
    check(historyGeometryForRate(48000, kept), "48k");
    check(!historyGeometryForRate(0, kept) && !historyGeometryForRate(-48000, kept)
        && kept.delay == 64, "invalid rate rejected without mutation");
    std::cout << "PASS invalid rate rejection\n";
    // 48 kHz: normalization 2048/1026 > 1 at 0 dB -> one halving.
    MakeupSplit m{};
    check(splitMakeup(0.0f, kept, m), "0 dB");
    check(m.scaled == 2048.0 / 1026.0 && m.scaledTimes16 == 16.0 * 2048.0 / 1026.0
        && m.mantissa == 1024.0 / 1026.0 && m.exponent == 1 && m.shiftCompensation == 5
        && m.historyScale == 1026.0 / 2048.0 * 0.0625, "0 dB split");
    // At -30 dB no halving occurs; writer still halves once and compensates by one.
    check(splitMakeup(-30.0f, kept, m), "-30 dB");
    const double lin = std::pow(10.0, -30.0 / 20.0) * (2048.0 / 1026.0);
    check(m.exponent == 1 && m.shiftCompensation == 5 && m.mantissa == lin * 0.5, "-30 dB split");
    check(splitMakeup(30.0f, kept, m), "+30 dB");
    check(m.mantissa > 0.5 && m.mantissa <= 1.0 && m.exponent == 6
        && m.shiftCompensation == 10
        && std::ldexp(m.mantissa, m.exponent) == m.scaled, "+30 dB exact power-of-two split");
    const MakeupSplit before = m;
    check(!splitMakeup(30.5f, kept, m) && !splitMakeup(NAN, kept, m)
        && m.mantissa == before.mantissa && m.exponent == before.exponent, "invalid gain rejected");
    std::cout << "PASS makeup mantissa/exponent normalization split\n";
}
