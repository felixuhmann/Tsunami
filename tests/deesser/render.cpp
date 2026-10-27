// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only planar float32 stdin/stdout adapter. Arguments: rate channels frames block six physical controls.
#include "Processor.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc,char** argv){
 if(argc!=11)return 2;
 unsigned channels=unsigned(std::atoi(argv[2])),frames=unsigned(std::atoi(argv[3])),block=unsigned(std::atoi(argv[4]));
 if((channels!=1&&channels!=2)||!block)return 2;
#ifdef _WIN32
 // Binary float32 pipes: the CRT defaults to text mode (CR/LF translation, 0x1a ends input).
 if(_setmode(_fileno(stdin),_O_BINARY)==-1||_setmode(_fileno(stdout),_O_BINARY)==-1)return 5;
#endif
 tsunami::deesser::Processor p;p.prepare(std::atof(argv[1]),channels,block);
 for(unsigned i=0;i<6;++i)p.setParameter(i,std::strtof(argv[5+i],nullptr));
 std::vector<float> data(channels*frames);if(std::fread(data.data(),sizeof(float),data.size(),stdin)!=data.size())return 3;
 for(unsigned i=0;i<frames;i+=block){float* out[2]={data.data()+i,channels==2?data.data()+frames+i:nullptr};const float* in[2]={out[0],out[1]};p.process(in,out,std::min(block,frames-i));}
 return std::fwrite(data.data(),sizeof(float),data.size(),stdout)==data.size()?0:4;
}
