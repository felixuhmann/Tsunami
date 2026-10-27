// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc,char**argv) {
 if(argc!=8) return 2;
 const unsigned channels=std::strtoul(argv[3],nullptr,10);
 const unsigned rate=std::strtoul(argv[4],nullptr,10);
 FILE*f=std::fopen(argv[1],"rb"); if(!f)return 3;
 std::fseek(f,0,SEEK_END); const auto count=std::ftell(f)/sizeof(float); std::rewind(f);
 const unsigned frames=count/channels;
 std::vector<float> interleaved(count); if(std::fread(interleaved.data(),sizeof(float),count,f)!=static_cast<size_t>(count))return 4;std::fclose(f);
 tsunami::rvox::Processor p;p.prepare(rate,channels,512);
 for(unsigned i=0;i<3;++i)p.setParameter(i,std::strtof(argv[i+5],nullptr));
 const unsigned latency=p.latencySamples();
 std::vector<std::vector<float>> x(channels,std::vector<float>(frames+latency)),y=x;
 for(unsigned n=0;n<frames;++n)for(unsigned c=0;c<channels;++c)x[c][n]=interleaved[n*channels+c];
 const float*in[2];float*out[2];for(unsigned c=0;c<channels;++c){in[c]=x[c].data();out[c]=y[c].data();}
 p.process(in,out,frames+latency);
 for(unsigned n=0;n<frames;++n)for(unsigned c=0;c<channels;++c)interleaved[n*channels+c]=y[c][n+latency];
 f=std::fopen(argv[2],"wb");if(!f)return 5;const bool ok=std::fwrite(interleaved.data(),sizeof(float),count,f)==static_cast<size_t>(count);std::fclose(f);return ok?0:6;
}
