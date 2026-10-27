// SPDX-License-Identifier: GPL-3.0-or-later
// Mono float32 stream -> MonoProcessor float32 stream at 48 kHz.
// Args: gainDb trimDb mixPercent. Wet coefficients are the GainStage defaults
// (provisional, NOT a recovered Threshold/Ratio/Attack/Release mapping), so only
// Mix=0 output is a grounded comparison. Test utility, not a plugin host.
#include "MonoProcessor.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv) {
    if (argc != 4) return 2;
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1
        || _setmode(_fileno(stdout), _O_BINARY) == -1) return 3;
#endif
    tsunami::rcomp::MonoProcessor p;
    tsunami::rcomp::GainStage::Coefficients provisional;
    provisional.softScale = 0.0625f;
    if (!p.prepare(48000) || !p.configureWet(provisional)
        || !p.setOutput(std::strtof(argv[1], nullptr), std::strtof(argv[2], nullptr),
                        std::strtof(argv[3], nullptr))) return 4;
    float value = 0.0f;
    while (std::fread(&value, sizeof(value), 1, stdin) == 1) {
        p.process(&value, &value, 1);
        if (std::fwrite(&value, sizeof(value), 1, stdout) != 1) return 7;
    }
    return std::ferror(stdin) ? 5 : 0;
}
