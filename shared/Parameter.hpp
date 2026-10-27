// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace tsunami {
struct Parameter {
    const char* id;
    const char* name;
    float minimum, maximum, initial;
    const char* unit;
    float step;
    float skew = 1.f;              // JUCE NormalisableRange skew (1 = linear; <1 log-like taper)
    const char* choices = nullptr; // '|'-separated labels for indices minimum..maximum (step 1)
    int displayDecimals = -1;      // >= 0: host text shows the physical value with this many decimals
                                   // (text only; range, step and parsing unchanged). -1: JUCE default
};
}
