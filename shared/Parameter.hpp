// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace tsunami {
struct Parameter {
    const char* id;
    const char* name;
    float minimum, maximum, initial;
    const char* unit;
    float step;
};
}
