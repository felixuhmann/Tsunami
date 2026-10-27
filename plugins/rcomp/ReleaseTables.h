// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Release-table builder and lookup, ported from the structure read out of the
// reference builder 0x18002d7d0 and kernel lookup 0x101100
// (docs/evidence/rcomp/r2t4-kernel-findings.json).
//
// Status: formulas ported; NOT wired into MonoProcessor. Still unresolved:
//  - k (integer control 0x34, table scale) at any setting,
//  - the slow follower s11 recurrence and its coefficients (control slots 0x2c/0x30),
//  - the unit of R (milliseconds assumed by the 0.001 factor),
//  - which user controls drive the curve switch (+0x66b0) and index switch (+0x84b0).
// Those are therefore inputs here, never defaults.
#include <algorithm>
#include <array>
#include <cmath>

namespace tsunami::rcomp {

// Integer mode at coefficient block +0x66b0 selects c = 10 or c = 3 (d = c^2 * 0.01).
enum class ReleaseCurve { C10, C3 };

struct ReleaseTableSettings {
    double sampleRate;   // fs
    double release;      // R, control at +0xb0 (ms assumed)
    ReleaseCurve curve;  // +0x66b0
    bool reversedIndex;  // +0x84b0 != 0 -> x_j = 256 - j
    bool halveB;         // non-native build halves B
};

class ReleaseTables {
public:
    static constexpr int kSize = 256;

    double a(int j) const { return table_[static_cast<std::size_t>(j)]; }
    double b(int j) const { return table_[static_cast<std::size_t>(kSize + j)]; }
    // Flat table as laid out in the kernel buffer: A[0..255] then B[0..255].
    double at(int i) const { return table_[static_cast<std::size_t>(i)]; }

    // Release memory for the next sample:
    //   T[(int)((s11 + 1) * k + 1)] * T[(int)(min(s10, s11) * k)]
    // s10 = gain follower state, s11 = slow follower of s10, k = int control 0x34.
    // Indices are clamped to the 512-entry buffer as a memory-safety guard only;
    // the reference clamping behavior is not recovered.
    double lookup(double s10, double s11, int k) const {
        const int i1 = clampIndex(static_cast<int>((s11 + 1.0) * k + 1.0));
        const int i2 = clampIndex(static_cast<int>(std::min(s10, s11) * k));
        return at(i1) * at(i2);
    }

private:
    friend ReleaseTables buildReleaseTables(const ReleaseTableSettings&);
    static int clampIndex(int i) { return std::clamp(i, 0, 2 * kSize - 1); }
    std::array<double, 2 * kSize> table_{};
};

inline ReleaseTables buildReleaseTables(const ReleaseTableSettings& s) {
    ReleaseTables t;
    const double c = s.curve == ReleaseCurve::C10 ? 10.0 : 3.0;
    const double d = c * c * 0.01;
    for (int j = 0; j < ReleaseTables::kSize; ++j) {
        const double aj = std::pow(0.5, d / static_cast<double>(j + 1));
        const double x = s.reversedIndex ? static_cast<double>(256 - j) : static_cast<double>(j);
        const double u = x / 256.0;
        const double u4 = (u * u) * (u * u);
        const double shape = std::pow(1.0 - u4, 4.0);
        const double timeMs = std::max(0.002, shape * s.release);
        double bj = std::exp(-1.0 / (timeMs * s.sampleRate * 0.001)) / aj;
        if (s.halveB) bj *= 0.5;
        t.table_[static_cast<std::size_t>(j)] = aj;
        t.table_[static_cast<std::size_t>(ReleaseTables::kSize + j)] = bj;
    }
    return t;
}

}  // namespace tsunami::rcomp
