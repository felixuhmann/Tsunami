// SPDX-License-Identifier: GPL-3.0-or-later
// Derivation tool: render raw float32 mono through Mono with explicit DesignParameters.
// Used only to test candidate control laws against derivation renders; not a plugin path.
#include "design.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc,char** argv){
 if(argc!=10){std::fprintf(stderr,"usage: %s rate lowerHz upperHz attenuationDb relativeThreshold nominalFloor floorCap in.f32 out.f32\n",argv[0]);return 2;}
 tsunami::ns1::DesignParameters p{};
 p.lowerHz=std::atof(argv[2]);p.upperHz=std::atof(argv[3]);p.attenuationDb=std::atof(argv[4]);
 p.relativeThreshold=float(std::atof(argv[5]));p.nominalFloor=float(std::atof(argv[6]));p.floorCap=float(std::atof(argv[7]));
 std::FILE* f=std::fopen(argv[8],"rb");if(!f)return 3;std::vector<float> x;float v;while(std::fread(&v,4,1,f)==1)x.push_back(v);std::fclose(f);
 tsunami::ns1::Mono m(tsunami::ns1::designMono(std::atof(argv[1]),p),tsunami::ns1::initialMonoState());
 m.process(x.data(),x.data(),x.size());
 f=std::fopen(argv[9],"wb");if(!f)return 4;std::fwrite(x.data(),4,x.size(),f);std::fclose(f);return 0;
}
