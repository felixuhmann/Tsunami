// SPDX-License-Identifier: GPL-3.0-or-later
#include "design.hpp"
#include <cmath>
#include <iostream>
int main(){
 using namespace tsunami::ns1;
 const float controls[]={0,.5f,1};
 // Golden output samples from the retained ordinary unmodified reference host.
 const float first[]={0.0839412584900856f,-4.600763350026682e-05f,1.0007677701651119e-05f};
 for(unsigned k=0;k<3;++k){
  Mono p(designSampledMono(48000,controls[k]),initialMonoState());
  for(unsigned i=0;i<96000;++i)if(p.process(0)!=0){std::cerr<<"silence initialization failed\n";return 1;}
  const float y=p.process(0.5000152587890625f);
  if(std::abs(y-first[k])>2e-6f){std::cerr<<"initialized impulse mismatch "<<controls[k]<<' '<<y<<'\n';return 1;}
 }
 std::cout<<"initialized sampled-control impulse checks passed\n";
}
