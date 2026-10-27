// SPDX-License-Identifier: GPL-3.0-or-later
// Offline renderer: raw native float32 mono in -> out through the public Processor.
#include "Processor.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc,char** argv){
 if(argc!=6){std::fprintf(stderr,"usage: %s rate amountPercent block in.f32 out.f32\n",argv[0]);return 2;}
 const double rate=std::atof(argv[1]);const float amount=float(std::atof(argv[2]));const unsigned block=unsigned(std::atoi(argv[3]));
 std::FILE* f=std::fopen(argv[4],"rb");if(!f)return 3;std::vector<float> x;float v;while(std::fread(&v,4,1,f)==1)x.push_back(v);std::fclose(f);
 tsunami::ns1::Processor p;p.prepare(rate,1,block);p.setParameter(tsunami::ns1::Processor::Amount,amount);
 for(std::size_t o=0;o<x.size();o+=block){const unsigned n=unsigned(x.size()-o<block?x.size()-o:block);float* io[]={x.data()+o};const float* in[]={x.data()+o};p.process(in,io,n);}
 f=std::fopen(argv[5],"wb");if(!f)return 4;std::fwrite(x.data(),4,x.size(),f);std::fclose(f);
 std::fprintf(stderr,"established=%d\n",int(p.controlEstablished()));return 0;
}
