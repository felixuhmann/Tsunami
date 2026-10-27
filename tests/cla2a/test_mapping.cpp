// SPDX-License-Identifier: GPL-3.0-or-later
#include "ControlMapping.h"
#include <cmath>
#include <cstdio>
#include <set>
int main() {
    using namespace tsunami::cla2a;
    if (std::abs(gainToNormalized(40.0)-0.535) > 0.0006 ||
        std::abs(peakReductionToNormalized(46.0)-0.598) > 0.0006) {
        std::puts("FAIL reference default control mapping"); return 1;
    }
    for (unsigned i=0; i<=1000; ++i) {
        double n=i/1000.0;
        if (std::abs(gainToNormalized(normalizedToGain(n))-n)>1e-12 ||
            std::abs(peakReductionToNormalized(normalizedToPeakReduction(n))-n)>1e-12) return 2;
    }
    // Genuine host enumeration (evidence: docs/evidence/cla2a/README.md): a
    // 1000-step normalized sweep displayed at 0.04 resolution yields exactly
    // 929 (Gain) / 931 (Peak Reduction) distinct values starting 0, 0.24, 0.52, 0.76
    // (Gain) and 0, 0.28, 0.52, 0.80 (Peak Reduction), ending at 100.
    std::set<long> g, pr;
    for (unsigned i=0; i<=1000; ++i) {
        g.insert(std::lround(normalizedToGain(i/1000.0)/0.04));
        pr.insert(std::lround(normalizedToPeakReduction(i/1000.0)/0.04));
    }
    if (g.size()!=929 || pr.size()!=931) { std::puts("FAIL host display sweep cardinality"); return 3; }
    const long ga[]{0,6,13,19}, pa[]{0,7,13,20};
    auto gi=g.begin(); auto pi=pr.begin();
    for (int k=0;k<4;++k,++gi,++pi) if (*gi!=ga[k] || *pi!=pa[k]) { std::puts("FAIL host display anchors"); return 4; }
    if (*g.rbegin()!=2500 || *pr.rbegin()!=2500) { std::puts("FAIL host display end"); return 5; }
    std::puts("PASS nonlinear Gain / Peak Reduction control mappings");
}
