// SPDX-License-Identifier: GPL-3.0-or-later
// Stereo comparison harness: render planar float32 stereo (all L then all R) through
// Processor (2 channels, 48 kHz, 512-frame blocks) at a normalized control.
// Usage: ns1_render_stereo control in.f32 out.f32 | ns1_render_stereo --self-test
#include "Processor.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static std::vector<float> render(float control,const std::vector<float>& x){
 const size_t n=x.size()/2;std::vector<float> y(2*n);
 tsunami::ns1::Processor p;p.prepare(48000,2,512);p.setParameter(0,control);
 for(size_t o=0;o<n;o+=512){const unsigned m=unsigned(n-o<512?n-o:512);
  const float* i[]={x.data()+o,x.data()+n+o};float* q[]={y.data()+o,y.data()+n+o};p.process(i,q,m);}
 return y;
}
int main(int argc,char** argv){
 if(argc==2&&std::strcmp(argv[1],"--self-test")==0){
  const size_t n=48000;std::vector<float> x(2*n);unsigned s=1;
  for(size_t k=0;k<n;++k){s=s*1664525u+1013904223u;x[k]=x[n+k]=float(int(s>>9)-(1<<22))/float(1<<23)*.5f;}
  const auto y=render(.5f,x);
  for(size_t k=0;k<n;++k)if(!std::isfinite(y[k])||y[k]!=y[n+k])return 1;  // identical channels stay identical
  std::puts("stereo harness self-test passed");return 0;}
 if(argc!=4){std::fprintf(stderr,"usage: %s control in.f32 out.f32 | --self-test\n",argv[0]);return 2;}
 std::FILE* f=std::fopen(argv[2],"rb");if(!f)return 3;
 std::vector<float> x;float v;
 while(std::fread(&v,4,1,f)==1)x.push_back(v);
 std::fclose(f);
 const auto y=render(float(std::atof(argv[1])),x);
 f=std::fopen(argv[3],"wb");if(!f)return 4;std::fwrite(y.data(),4,y.size(),f);std::fclose(f);return 0;
}
