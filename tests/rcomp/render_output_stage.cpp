// SPDX-License-Identifier: GPL-3.0-or-later
// Paired aligned dry/wet float32 stream -> output-stage float32 stream.
// Test utility, not a compressor host or production Processor.
#include "OutputStage.h"
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
    tsunami::rcomp::OutputStage stage;
    if (!stage.setGainTrim(std::strtof(argv[1], nullptr), std::strtof(argv[2], nullptr))
        || !stage.setMix(std::strtof(argv[3], nullptr))) return 4;
    float pair[2];
    while (true) {
        const auto count = std::fread(pair, sizeof(float), 2, stdin);
        if (count == 0) return std::ferror(stdin) ? 5 : 0;
        if (count != 2) return 6;
        const float value = stage.tick(pair[0], pair[1]);
        if (std::fwrite(&value, sizeof(value), 1, stdout) != 1) return 7;
    }
}
