// SPDX-License-Identifier: GPL-3.0-or-later
#include <limits>
#include <vector>
#include "Processor.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
void require(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
int main(){
 tsunami::deesser::Processor p; p.prepare(48000,1,64); p.setParameter(5,1);
 float x[]={-2,-1,-0.5f,0,0.5f,1,2}, y[7]{}; const float* in[]={x}; float* out[]={y};
 p.process(in,out,7);
 for(unsigned i=0;i<7;++i) require(y[i]==x[i],"explicit bypass is transparent including overs");
 require(p.latencySamples()==0,"zero latency");
 p.reset(); p.process(in,out,0);
 p.setParameter(5,0); p.process(in,out,7);
 require(y[0]==-1.0f && y[6]==0.999999f,"active output clips asymmetrically at unity threshold");
 p.prepare(48000,2,64); p.setParameter(1,-40); p.setParameter(4,1);
 float anti[7]; for(unsigned i=0;i<7;++i)anti[i]=-x[i];
 const float* si[]={x,anti}; float r[7]{}; float* so[]={y,r}; p.process(si,so,7);
 for(unsigned i=0;i<7;++i)require(y[i]==0 && r[i]==0,"stereo detector monitors mid sum, not max energy");
 p.prepare(48000,1,64); p.setParameter(4,1); float impulse[64]{}, response[64]{}; impulse[0]=0.1f;
 const float* mi[]={impulse}; float* mo[]={response}; p.process(mi,mo,64);
 require(std::abs(response[0])>0.001f,"detector monitor has a real recovered highpass response");
 require(std::abs(response[63])<0.0001f,"detector impulse decays");
 p.reset(); p.process(mi,mo,64); float first=response[0]; p.reset(); p.process(mi,mo,1);
 require(response[0]==first,"reset recreates filter state");
 p.setParameter(4,0); p.setParameter(1,-30); p.setParameter(3,1); p.reset();
 float tone[4096],wet[4096]; for(unsigned i=0;i<4096;++i)tone[i]=.5f*std::sin(float(i)*1.1f);
 const float* ti[]={tone};float* to[]={wet};p.process(ti,to,4096);
 double energy=0;for(unsigned i=2048;i<4096;++i)energy+=wet[i]*wet[i];
 require(energy<30,"recovered threshold law attenuates high-frequency tone");
 p.setParameter(3,0);p.reset();p.process(ti,to,4096);
 double splitEnergy=0;for(unsigned i=2048;i<4096;++i)splitEnergy+=wet[i]*wet[i];
 require(splitEnergy>energy,"split preserves an uncompressed band");
 {
  // Genuine renders: the detector frequency is applied in whole Hz (9513.66 Hz host value behaves as 9514 Hz).
  float chirp[2048],a[2048],b[2048]; for(unsigned i=0;i<2048;++i)chirp[i]=.3f*std::sin(.0005f*float(i)*float(i));
  const float* ci[]={chirp}; float* ao[]={a}; float* bo[]={b};
  tsunami::deesser::Processor q; q.prepare(44100,1,2048); q.setParameter(1,-45); q.setParameter(2,0); q.setParameter(3,1);
  q.setParameter(0,9513.66f); q.process(ci,ao,2048);
  q.setParameter(0,9514.0f); q.reset(); q.process(ci,bo,2048);
  for(unsigned i=0;i<2048;++i)require(a[i]==b[i],"freq is quantized to whole Hz");
  require(q.getParameter(0)==9514.0f,"getParameter reports the applied whole-Hz frequency");
 }
 { // NaN-safe: a single non-finite input sample must not poison later output (review finding)
  tsunami::deesser::Processor q; q.prepare(48000,1,512); q.setParameter(1,-40);
  std::vector<float> a(1024),b(1024); for(unsigned i=0;i<1024;++i)a[i]=.5f*float(std::sin(2*3.14159265358979*7000.0*i/48000.0));
  a[100]=std::numeric_limits<float>::quiet_NaN(); a[200]=std::numeric_limits<float>::infinity();
  const float* in[]={a.data()}; float* out[]={b.data()}; q.process(in,out,1024);
  for(unsigned i=0;i<1024;++i)require(std::isfinite(b[i]),"non-finite input sample does not poison output");
  require(std::isfinite(q.gainReductionDb())&&std::isfinite(q.detectorLevelDb()),"meters stay finite after non-finite input");
 }
 { // Replacement numeric-domain policy: finite overload must not create NaN audio or poison state.
  // The old stereo sum overflowed to infinity; in SideChain monitor the next sample became NaN.
  for (unsigned channels : {1u, 2u}) for (unsigned audio : {0u, 1u}) for (unsigned monitor : {0u, 1u}) {
   tsunami::deesser::Processor q; q.prepare(48000,channels,64); q.setParameter(1,-40); q.setParameter(3,float(audio)); q.setParameter(4,float(monitor));
   std::vector<float> a(64),b(64),l(64),r(64);
   for(unsigned i=0;i<64;++i)a[i]=b[i]=(i%2?-1.f:1.f)*std::numeric_limits<float>::max();
   const float* input[]={a.data(),b.data()}; float* output[]={l.data(),r.data()};
   q.process(input,output,64);
   for(unsigned c=0;c<channels;++c)for(unsigned i=0;i<64;++i)
    require(std::isfinite(output[c][i]),"finite overload stays finite in audio and SideChain modes");
   for(unsigned i=0;i<64;++i)a[i]=b[i]=.25f*std::sin(float(i)*.7f);
   q.process(input,output,64);
   for(unsigned c=0;c<channels;++c)for(unsigned i=0;i<64;++i)
    require(std::isfinite(output[c][i]),"valid continued audio stays finite after overload");
   require(std::isfinite(q.gainReductionDb())&&std::isfinite(q.detectorLevelDb()),"meters stay finite after finite overload");
   q.reset(); q.process(input,output,64);
   tsunami::deesser::Processor fresh; fresh.prepare(48000,channels,64); fresh.setParameter(1,-40); fresh.setParameter(3,float(audio)); fresh.setParameter(4,float(monitor));
   std::vector<float> fl(64),fr(64); float* freshOutput[]={fl.data(),fr.data()}; fresh.process(input,freshOutput,64);
   for(unsigned c=0;c<channels;++c)for(unsigned i=0;i<64;++i)
    require(output[c][i]==freshOutput[c][i],"reset after finite overload reproduces fresh audio");
   q.setParameter(5,1); for(unsigned i=0;i<64;++i)a[i]=b[i]=std::numeric_limits<float>::max(); q.process(input,output,64);
   for(unsigned c=0;c<channels;++c)for(unsigned i=0;i<64;++i)
    require(output[c][i]==input[c][i],"bypass still preserves finite extremes without saturation");
  }
 }
 std::puts("PASS bypass, clipping, detector, reset, compression and mode behavior");
}
