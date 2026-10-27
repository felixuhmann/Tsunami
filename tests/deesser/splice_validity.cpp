// SPDX-License-Identifier: GPL-3.0-or-later
// Evidence tool: is a splice of two static renders a valid model of a mid-stream change? (Tsunami core only)
#include "Processor.h"
#include <cmath>
#include <cstdio>
#include <vector>
using tsunami::deesser::Processor;
static std::vector<float> run(const std::vector<float>& x,float thr,unsigned idx,float a,float b,unsigned N,bool change){
 Processor p; p.prepare(48000,1,512); p.setParameter(1,thr); p.setParameter(3,1); p.setParameter(idx,a);
 std::vector<float> y(x.size());
 for(size_t s=0;s<x.size();s+=512){ if(change&&s==N)p.setParameter(idx,b);
  const float* in[]={x.data()+s}; float* out[]={y.data()+s}; p.process(in,out,512);} return y;}
int main(){
 const unsigned L=48128,N=24064; std::vector<float> x(L);
 for(unsigned i=0;i<L;++i)x[i]=.5f*float(std::sin(2*3.14159265358979*7000.0*i/48000.0));
 struct C{const char* n;float thr;unsigned idx;float a,b;} cs[]={{"threshold 0->-40 (Wideband)",0,1,0,-40},{"freq 4000->12000 (thr -40, Wideband)",-40,0,4000,12000},{"monitor Output->SideChain (thr -40)",-40,4,0,1},{"audio Split->Wideband (thr -40)",-40,3,0,1}};
 for(auto& c:cs){ auto cont=run(x,c.thr,c.idx,c.a,c.b,N,true); auto A=run(x,c.thr,c.idx,c.a,c.a,N,false); auto B=run(x,c.thr,c.idx,c.b,c.b,N,false);
  double m=0; unsigned last=N; for(unsigned i=N;i<L;++i){double d=std::abs(double(cont[i])-B[i]); if(d>m)m=d; if(d>1e-6)last=i;}
  double pre=0; for(unsigned i=0;i<N;++i)pre=std::max(pre,std::abs(double(cont[i])-A[i]));
  std::printf("%-40s pre_N_max_diff=%.3g post_N_max_diff=%.6g last_sample_over_1e-6=%u (%.1f ms after N) splice_model=%s\n",c.n,pre,m,last,(last-N)/48.0,m<=1e-6?"VALID(core)":"INVALID");}
}
