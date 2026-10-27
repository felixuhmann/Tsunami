// SPDX-License-Identifier: GPL-3.0-or-later
// hdelay_render <in.f32> <out.f32> <rate> <block> [index=value ...]  (mono raw float32)
#include "Processor.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    FILE* f = std::fopen(argv[1], "rb"); if (!f) return 3;
    std::vector<float> x; float v; while (std::fread(&v, 4, 1, f) == 1) x.push_back(v); std::fclose(f);
    tsunami::hdelay::Processor p; const unsigned block = unsigned(std::atoi(argv[4]));
    p.prepare(std::atof(argv[3]), 1, block);
    for (int a = 5; a < argc; ++a) { std::string s = argv[a]; auto e = s.find('=');
        p.setParameter(unsigned(std::stoul(s.substr(0, e))), std::stof(s.substr(e + 1))); }
    p.reset(); std::vector<float> y(x.size());
    for (size_t o = 0; o < x.size(); o += block) { unsigned b = unsigned(std::min<size_t>(block, x.size() - o));
        const float* i[] = {x.data() + o}; float* q[] = {y.data() + o}; p.process(i, q, b); }
    f = std::fopen(argv[2], "wb"); std::fwrite(y.data(), 4, y.size(), f); std::fclose(f);
}
