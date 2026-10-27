// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Independently evaluate the gate and the triangular FIR by direct summation,
// rather than duplicating Processor's integer second-difference recurrence.
// Original Windows fresh-state RealWorld slot 2 is 130 milliseconds, not 100.
int main() {
 constexpr unsigned frames=24000, latency=64, a=27, b=38, shift=11;
 std::vector<float> x(frames), y(frames), gain(frames);
 for(unsigned n=0;n<frames;++n) x[n]=n<4000?.4f:.002f;
 tsunami::rvox::Processor p; p.prepare(48000,1,127); p.setParameter(1,-20);
 const float* in[]={x.data()};float* out[]={y.data()};p.process(in,out,frames);
 std::array<float,6> state{};
 constexpr float ap[]={.6303f,.3583f,.1607f};
 const float release=static_cast<float>(std::pow(.367879441,1./(48000*.130)));
 float envelope=0;
 for(unsigned n=0;n<frames;++n) {
  float peak=std::abs(x[n]);
  for(unsigned k=0;k<3;++k) {
   const float v=(x[n]-state[2*k+1])*ap[k]+state[2*k];
   state[2*k]=x[n];state[2*k+1]=v;peak=std::max(peak,std::abs(v));
  }
  envelope=peak>=envelope?peak:peak+(envelope-peak)*release;
  const float g=std::min(1.f,envelope*10.f);
  gain[n]=static_cast<float>(static_cast<int32_t>(double(g)*2147483600.)>>shift);
 }
 double maximum=0;
 const float makeup=static_cast<float>((2048./(a*b))*.99);
 for(unsigned n=latency+4000;n<frames;++n) {
  int64_t sum=0;
  for(unsigned i=0;i<a;++i)for(unsigned j=0;j<b;++j)sum+=static_cast<int64_t>(gain[n-i-j]);
  // The recovered reset seeds accumulators one quantization unit higher than
  // history. Its constant a*b offset is retained here, not fitted away.
  const float smooth=static_cast<float>(sum+a*b)*4.656613e-10;
  const float expected=x[n-latency]*(makeup*smooth);
  maximum=std::max(maximum,std::abs(double(y[n])-expected));
 }
 std::printf("130ms gate / direct FIR max_abs=%.12g\n",maximum);
 if(maximum>2e-8) {std::fputs("FAIL: fresh Windows gate release is 130ms\n",stderr);return 1;}
 std::puts("PASS recovered gate initialization and direct FIR");
}
