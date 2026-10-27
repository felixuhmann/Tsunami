// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Independently written building blocks whose *structure* was identified in
// the reference kernel (detector, static curve lookup, ballistics, blend
// polynomial, hum oscillator, noise source, parameter smoothing). No vendor
// code or coefficient tables are reproduced here; coefficient values are
// supplied by the caller (see Processor.h for their provenance status).
#include <cstdint>
#include <utility>
#include "GainMemory.h"
namespace tsunami::cla2a {

// Full-wave rectifying peak detector (the reference "envelope" is |x|).
inline double detect(double x) noexcept { return x < 0 ? -x : x; }

// Piecewise-linear static curve. Input floored at 0; values at/below the
// first breakpoint map to y[0], values at/above the last map to y[n-1].
inline double interpolateCurve(double v, const double* x, const double* y, unsigned n) noexcept {
    if (v < 0) v = 0;
    if (v <= x[0]) return y[0];
    if (v >= x[n - 1]) return y[n - 1];
    unsigned i = 1;
    while (x[i] < v) ++i;
    return y[i - 1] + (y[i] - y[i - 1]) * (v - x[i - 1]) / (x[i] - x[i - 1]);
}

struct Poles { double fast, slow, weight; };

// Program-dependent attack: level*sensitivity clamped to [levels[n-1], 1],
// looked up in a descending level table to give both pole values.
inline Poles attackPoles(double level, double sensitivity, const double* levels,
                         const double* fast, const double* slow, unsigned n) noexcept {
    double u = sensitivity * level;
    if (u > 1.0) u = 1.0;
    if (u < levels[n - 1]) u = levels[n - 1];
    unsigned i = 1;
    while (i < n - 1 && !(levels[i] < u)) ++i;
    const double t = (u - levels[i - 1]) / (levels[i] - levels[i - 1]);
    return {fast[i - 1] + (fast[i] - fast[i - 1]) * t,
            slow[i - 1] + (slow[i] - slow[i - 1]) * t, 0.75};
}

// Two-memory ballistics: attack poles while the target lies below the
// current summed memory, release poles otherwise.
class Ballistics {
public:
    void reset() noexcept { memory_.reset(); }
    double value() const noexcept { return memory_.value(); }
    double advance(double target, const Poles& attack, const Poles& release) noexcept {
        const Poles& p = (memory_.value() <= target) ? release : attack;
        return memory_.advance(target, p.fast, p.slow);
    }
private:
    GainMemory memory_;
};

// Gain-dependent blend weight: clamp gain to [floor, 1], evaluate cubic.
struct BoostPolynomial {
    double floor, c0, c1, c2, c3;
    double operator()(double g) const noexcept {
        if (g > 1.0) g = 1.0;
        if (g < floor) g = floor;
        return ((c3 * g + c2) * g + c1) * g + c0;
    }
};

// Quadrature oscillator with first-order amplitude renormalisation.
class HumOscillator {
public:
    void setRotation(double c, double s) noexcept { c_ = c; s_ = s; }
    void setShape(double square, double linear) noexcept { a_ = square; b_ = linear; }
    void reset(double re, double im) noexcept { re_ = re; im_ = im; }
    double next() noexcept {
        const double re = c_ * re_ - s_ * im_;
        const double im = c_ * im_ + s_ * re_;
        const double k = 2.0 - (re * re + im * im);
        re_ = k * re; im_ = k * im;
        return (a_ * im_ + b_) * im_;
    }
private:
    double c_ = 1, s_ = 0, a_ = 0, b_ = 0, re_ = 1, im_ = 0;
};

// Wrapping 32-bit LCG noise; two draws produce a (sum, difference) pair.
class NoiseSource {
public:
    void reset(std::int32_t seed) noexcept { state_ = seed; }
    std::int32_t state() const noexcept { return state_; }
    std::pair<double, double> next() noexcept {
        const double a = draw(), b = draw();
        return {(a + b) * 0.25, (a - b) * 0.25};
    }
private:
    double draw() noexcept {
        state_ = static_cast<std::int32_t>(static_cast<std::uint32_t>(state_) * 0x9E3779u + 1u);
        return state_ / 2147483648.0;
    }
    std::int32_t state_ = 0;
};

class Smoother {
public:
    void reset(double v) noexcept { v_ = v; }
    double value() const noexcept { return v_; }
    double next(double target, double pole) noexcept { v_ = (v_ - target) * pole + target; return v_; }
private:
    double v_ = 0;
};
}
