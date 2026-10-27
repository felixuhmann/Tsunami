// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
void require(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
int main(){
 tsunami::deesser::Processor p; p.prepare(48000,1,64); p.setParameter(5,1);
 float x[]={-2,-1,-0.5f,0,0.5f,1,2}, y[7]{}; const float* in[]={x}; float* out[]={y};
 p.process(in,out,7);
 for(unsigned i=0;i<7;++i) require(y[i]==x[i],"explicit bypass is transparent including overs");
 require(p.latencySamples()==0,"zero latency");
 p.reset(); p.process(in,out,0);
 std::puts("PASS bypass, zero frames, zero latency");
}
