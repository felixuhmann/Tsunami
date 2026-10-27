// SPDX-License-Identifier: GPL-3.0-or-later
// Recovered control laws (provenance: plugins/cla2a/ControlLaws.h).
#include "ControlLaws.h"
#include <cmath>
#include <cstdio>
static bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
int main() {
    using namespace tsunami::cla2a;
    // Gain knob -> makeup dB: knob/10 indexes an 11-point table, linear between.
    if (!near(gainMakeupDb(0), -51.0) || !near(gainMakeupDb(40), 8.8) ||
        !near(gainMakeupDb(100), 39.0) || !near(gainMakeupDb(46), 8.8 + 0.6 * (17.2 - 8.8)) ||
        !near(gainMakeupDb(-5), -51.0) || !near(gainMakeupDb(250), 39.0)) {
        std::puts("FAIL gain makeup law"); return 1;
    }
    // Peak Reduction knob -> sidechain drive dB: knob/25 indexes a 5-point table.
    if (!near(peakReductionDriveDb(0), -50.0) || !near(peakReductionDriveDb(25), -33.0) ||
        !near(peakReductionDriveDb(50), 15.6) || !near(peakReductionDriveDb(75), 31.0) ||
        !near(peakReductionDriveDb(100), 39.0) ||
        !near(peakReductionDriveDb(46), -33.0 + 0.84 * (15.6 + 33.0))) {
        std::puts("FAIL peak reduction drive law"); return 2;
    }
    // Auto-makeup compensation keyed by Peak Reduction: 6-point table whose
    // first point (0, 0) precedes the 45.12 breakpoint in the reference data.
    if (!near(autoMakeupDb(0), 0.0) || !near(autoMakeupDb(22.56), 0.35) || !near(autoMakeupDb(45.12), 0.7) ||
        !near(autoMakeupDb(54.96), 5.0) || !near(autoMakeupDb(100), 18.1) ||
        !near(autoMakeupDb(57.54), 6.3)) {
        std::puts("FAIL auto makeup table"); return 3;
    }
    return 0;
}
