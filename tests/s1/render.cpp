// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>
// Offline test harness: interleaved float32 stdin/stdout; physical controls argv.
int main(int argc,char**argv) {
 tsunami::s1::Processor p;
 p.prepare(48000,2,512);
 for(int i=1;i<argc;++i) p.setParameter(unsigned(i-1),std::strtof(argv[i],nullptr));
 p.reset();
 std::vector<float> interleaved; float x;
 while(std::cin.read(reinterpret_cast<char*>(&x),sizeof x)) interleaved.push_back(x);
 if(interleaved.size()%2) return 2;
 const auto frames=interleaved.size()/2;
 std::vector<float> l(frames),r(frames),a(frames),b(frames);
 for(size_t i=0;i<frames;++i){l[i]=interleaved[i*2];r[i]=interleaved[i*2+1];}
 const float* in[]{l.data(),r.data()};float* out[]{a.data(),b.data()};p.process(in,out,unsigned(frames));
 for(size_t i=0;i<frames;++i){std::cout.write(reinterpret_cast<char*>(&a[i]),4);std::cout.write(reinterpret_cast<char*>(&b[i]),4);}
}
