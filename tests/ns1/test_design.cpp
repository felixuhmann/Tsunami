// SPDX-License-Identifier: GPL-3.0-or-later
#include "design.hpp"
#include <cmath>
#include <iostream>
int main() {
 using namespace tsunami::ns1;
 const auto zero=designSampledMono(48000,0);
 const auto half=designSampledMono(48000,.5f);
 const auto full=designSampledMono(48000,1);
 if(!(half.bands[0].detector.nominalFloor<zero.bands[0].detector.nominalFloor))return 1;
 if(!(full.bands[0].detector.relativeThreshold>half.bands[0].detector.relativeThreshold))return 1;
 if(!(half.front[0].firstWeight>0))return 1;
 bool rejected=false;try{designSampledMono(48000,.25f);}catch(...){rejected=true;}
 if(!rejected)return 1;
 std::cout<<"sampled-control design checks passed\n";
}
