// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only raw interleaved stereo float32 transport, NOT a plugin host.
// Usage: driver INDEX=VALUE ...   (physical values, Processor parameter indices)
#include "Processor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
    tsunami::cla2a::Processor p;
    p.prepare(48000, 2, 1);
    for (int i = 1; i < argc; ++i) {
        const char* eq = std::strchr(argv[i], '=');
        if (!eq) return 2;
        p.setParameter(unsigned(std::strtoul(argv[i], nullptr, 10)), std::strtof(eq + 1, nullptr));
    }
    float x[2], y[2];
    const float* input[]{x, x + 1}; float* output[]{y, y + 1};
    while (std::fread(x, sizeof(float), 2, stdin) == 2) {
        p.process(input, output, 1);
        if (std::fwrite(y, sizeof(float), 2, stdout) != 2) return 3;
    }
    return std::ferror(stdin) ? 4 : 0;
}
