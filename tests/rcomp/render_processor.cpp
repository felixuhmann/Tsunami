// SPDX-License-Identifier: GPL-3.0-or-later
// Mono float32 stream -> MonoProcessor float32 stream at 48 kHz.
// Args: gainDb trimDb mixPercent [thresholdDb ratio attackMs releaseMs].
// With the optional controls, wet coefficients use the characterized static
// curve, ratio floor and attack memory; the release memory is a PROVISIONAL
// one-pole of the Release time (ARC d7d0 tables not recovered), the detector
// is instantaneous (64-sample sliding maximum not ported) and phase/ceiling
// release are zero. The ceiling follows the recovered Gain law. Test utility,
// not a plugin host.
#include "CoefficientLaws.h"
#include "MonoProcessor.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv) {
    if (argc != 4 && argc != 8) return 2;
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1
        || _setmode(_fileno(stdout), _O_BINARY) == -1) return 3;
#endif
    tsunami::rcomp::MonoProcessor p;
    tsunami::rcomp::GainStage::Coefficients provisional;
    provisional.softScale = 0.0625f;
    if (argc == 8) {
        using namespace tsunami::rcomp;
        const StaticCurve c = staticCurveForThresholdDb(std::strtod(argv[4], nullptr));
        const SoftGain g = softGainForFloor(ratioFloor(std::strtod(argv[5], nullptr)));
        provisional.thresholdScale = static_cast<float>(c.thresholdScale);
        provisional.slope = static_cast<float>(c.slope);
        provisional.shape = static_cast<float>(c.shape);
        provisional.softScale = static_cast<float>(g.scale);
        provisional.softOffset = static_cast<float>(g.offset);
        provisional.gainDecrease = static_cast<float>(attackMemory(48000, std::strtod(argv[6], nullptr)));
        provisional.gainIncrease = static_cast<float>(timeConstantMemory(48000, std::strtod(argv[7], nullptr)));
    }
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
