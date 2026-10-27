// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using tsunami::rvox::Processor;
void check(bool ok, const char* text) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
int main() {
 Processor p; p.prepare(48000,1,512);
 check(p.latencySamples()==64,"48k recovered lookahead is 64 samples");
 p.prepare(96000,2,512); check(p.latencySamples()==128,"96k recovered lookahead is 128 samples");
 p.prepare(44100,1,512); check(p.latencySamples()==64,"44.1k recovered lookahead is 64 samples");
 std::puts("PASS recovered lookahead/rate geometry");
 p.prepare(48000,1,512); p.setParameter(0,-18); p.setParameter(1,-80); p.setParameter(2,0);
 float x=.25f,y=0; const float* in[]={&x}; float* out[]={&y};
 for(unsigned i=0;i<96000;++i) p.process(in,out,1);
 const double t=std::pow(10.,-18./20.);
 const double expected=.99*.25/(.25*(1-t)+t);
 check(std::abs(y-expected)<2e-5,"rational compression with inverse threshold makeup, not ratio/knee");
 std::puts("PASS recovered DC compression law");
 p.setParameter(0,0); p.reset();
 float maximum=0;
 for(unsigned i=0;i<1024;++i) {p.process(in,out,1);if(i>=64)maximum=std::max(maximum,std::abs(y-.99f*x));}
 check(maximum<1e-6,"fresh default has immediate unity envelope, including first delayed sample");
}
