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
}
