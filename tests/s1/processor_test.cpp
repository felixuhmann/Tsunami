// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
void require(bool condition, const char* message) { if (!condition) { std::cerr << message << '\n'; std::exit(1); } }
int main() {
    tsunami::s1::Processor p;
    p.prepare(48000, 2, 16);
    float l[]{0.25f, 0.f, -0.25f}, r[]{0.f, 0.5f, 0.25f}, a[3]{}, b[3]{};
    const float* in[]{l,r}; float* out[]{a,b}; p.process(in,out,3);
    for(unsigned i=0;i<3;++i) {require(std::abs(a[i]-l[i])<1e-7f,"default left identity");require(std::abs(b[i]-r[i])<1e-7f,"default right identity");}
    require(p.latencySamples()==0 && p.tailSeconds()==0.,"matrix has no audio delay or tail");
    std::cout << "default identity passed\n";
}
