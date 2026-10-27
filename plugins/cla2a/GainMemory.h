// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace tsunami::cla2a {
// Independently expressed mathematical recurrence identified in the reference
// hot loop. Coefficient generation/control mapping is intentionally separate.
class GainMemory {
public:
    void reset() noexcept { fast_ = 0.75; slow_ = 0.25; }
    double value() const noexcept { return fast_ + slow_; }
    double advance(double target, double fastPole, double slowPole) noexcept {
        fast_ = fastPole * fast_ + (1.0 - fastPole) * (0.75 * target);
        slow_ = slowPole * slow_ + (1.0 - slowPole) * (0.25 * target);
        return value();
    }
private:
    // Fresh 3:1 unity initialization independently confirmed in Windows init.
    double fast_ = 0.75;
    double slow_ = 0.25;
};
}
