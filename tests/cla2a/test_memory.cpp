// SPDX-License-Identifier: GPL-3.0-or-later
#include "GainMemory.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using tsunami::cla2a::GainMemory;
void check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
int main() {
    GainMemory memory;
    check(memory.value() == 1.0, "fresh unity gain");
    // Synthetic coefficients, NOT recovered device coefficients.
    check(std::abs(memory.advance(0.2, 0.5, 0.75) - 0.65) < 1e-14, "two weighted memory poles");
    check(std::abs(memory.advance(0.2, 0.5, 0.75) - 0.4625) < 1e-14, "continued memory");
    memory.reset();
    check(memory.value() == 1.0, "reset restores unity");
    std::puts("PASS: recovered two-pole gain-memory recurrence (synthetic coefficients)");
}
