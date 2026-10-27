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
        if (argv[i][0] == 't' && argv[i][1] == '=') {  // fit hook: t=af0,af1,as0,as1,rf,rs
            tsunami::cla2a::Processor::TimeConstants t{};
            if (std::sscanf(argv[i] + 2, "%lf,%lf,%lf,%lf,%lf,%lf", &t.attackFastMs[0], &t.attackFastMs[1],
                            &t.attackSlowMs[0], &t.attackSlowMs[1], &t.releaseFastS, &t.releaseSlowS) != 6) return 2;
            p.setTimeConstants(t);
            continue;
        }
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
