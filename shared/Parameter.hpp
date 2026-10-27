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
};
}
