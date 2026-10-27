// SPDX-License-Identifier: GPL-3.0-or-later
// Automation / preset hidden-state matrix for the portable RVox core.
// Grounding (ELF RVoxMproc export): per-call controls are read from a
// coefficient block and all filter/envelope/smoother history lives in a
// separate state block that control updates do not touch; the gate envelope
// is tracked every sample whether or not the gate is engaged; makeup is a
// plain per-call multiplier (no ramp). Hence: controls take effect at the
// update boundary without smoothing and without resetting hidden state.
#include "Processor.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using tsunami::rvox::Processor;
static void check(bool ok,const char* text) {if(!ok) {std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
using Buffer=std::array<std::vector<float>,2>;
struct Event {unsigned at,index; float value;};
static Buffer input(unsigned frames) {
 Buffer x; for(auto& c:x) c.resize(frames);
 for(unsigned n=0;n<frames;++n) {
  // Bursts and near-silent gaps so both the compressor and the gate act.
  const double env=(n%16000<8000?.3:.004)*(.6+.4*std::sin(n*2e-4));
  x[0][n]=float(env*std::sin(n*.031)); x[1][n]=float(env*std::sin(n*.047+1));
 }
 return x;
}
// Renders with sample-indexed control events, splitting blocks at events.
static Buffer render(Processor& p,const Buffer& x,unsigned block,const std::vector<Event>& events,unsigned channels=2) {
 const unsigned frames=unsigned(x[0].size()); Buffer y; for(auto& c:y) c.assign(frames,0);
 size_t next=0;
 for(unsigned n=0;n<frames;) {
  while(next<events.size() && events[next].at==n) {p.setParameter(events[next].index,events[next].value);++next;}
  unsigned end=std::min(frames,n+block);
  if(next<events.size()) end=std::min(end,events[next].at);
  const float* in[]={x[0].data()+n,x[1].data()+n}; float* out[]={y[0].data()+n,y[1].data()+n};
  p.process(in,out,end-n); n=end;
 }
 (void)channels; return y;
}
static Processor make(float c,float g,float k,double rate=48000) {
 Processor p; p.prepare(rate,2,512); p.setParameter(0,c); p.setParameter(1,g); p.setParameter(2,k); return p;
}
static bool same(const Buffer& a,const Buffer& b,unsigned from=0,unsigned to=~0u) {
 to=std::min<unsigned>(to,unsigned(a[0].size()));
 for(unsigned c=0;c<2;++c) if(std::memcmp(a[c].data()+from,b[c].data()+from,(to-from)*sizeof(float))) return false;
 return true;
}
int main() {
 constexpr unsigned frames=48000, at=20011;
 const Buffer x=input(frames);
 // 1. Gain automation is stateless and immediate: before the event equals the
 //    old static render, from the event on equals the new static render.
 {Processor a=make(-24,-80,0),s0=make(-24,-80,0),s1=make(-24,-80,-9);
  const Buffer y=render(a,x,257,{{at,2,-9}}),y0=render(s0,x,257,{}),y1=render(s1,x,257,{});
  check(same(y,y0,0,at),"gain automation: unchanged before event");
  check(same(y,y1,at),"gain automation: immediate, unsmoothed, no state reset");}
 // 2. Gate engage mid-stream uses the continuously tracked hidden envelope:
 //    after the triangular smoother span it equals a render gated from start.
 {Processor a=make(-18,-80,0),off=make(-18,-80,0),on=make(-18,-30,0);
  const Buffer y=render(a,x,64,{{at,1,-30}}),yo=render(off,x,64,{}),yn=render(on,x,64,{});
  check(same(y,yo,0,at),"gate automation: unchanged before engage");
  check(!same(y,yo,at),"gate automation: engage has an effect");
  check(same(y,yn,at+2*a.latencySamples()),"gate automation: hidden envelope kept tracking while disengaged");}
 // 3. Re-sending identical controls (host automation/preset re-apply) is a
 //    bitwise no-op: no hidden-state reset on control writes.
 {Processor a=make(-30,-40,-6),b=make(-30,-40,-6);
  std::vector<Event> resend; for(unsigned n=1000;n<frames;n+=997) for(unsigned i=0;i<3;++i) resend.push_back({n,i,a.getParameter(i)});
  check(same(render(a,x,128,resend),render(b,x,128,{})),"identical control writes do not disturb state");}
 // 4. Sample-scheduled automation of every control is block-partition invariant.
 {const std::vector<Event> ev{{3001,0,-12},{7777,1,-50},{9000,2,-4.5f},{15000,0,-33},{15000,1,-80},{30001,2,0}};
  Processor a=make(-6,-80,0),b=make(-6,-80,0),c=make(-6,-80,0);
  const Buffer y1=render(a,x,1,ev),y2=render(b,x,127,ev),y3=render(c,x,4096,ev);
  check(same(y1,y2)&&same(y1,y3),"automation schedule block-partition invariant");}
 // 5. Preset recall: order of control writes is irrelevant (controls are
 //    rebuilt from the final value set), and recall survives prepare().
 {Processor a,b; a.prepare(48000,2,512); b.prepare(48000,2,512);
  a.setParameter(0,-21); a.setParameter(1,-35); a.setParameter(2,-7);
  b.setParameter(2,-7); b.setParameter(1,-35); b.setParameter(0,-21);
  check(a.makeupCoefficient()==b.makeupCoefficient(),"recall order-independent makeup");
  check(same(render(a,x,512,{}),render(b,x,512,{})),"recall order-independent audio");
  Processor c; c.setParameter(0,-21); c.setParameter(1,-35); c.setParameter(2,-7); c.prepare(48000,2,512);
  for(unsigned i=0;i<3;++i) check(c.getParameter(i)==a.getParameter(i),"controls persist across prepare");
  a.reset(); check(same(render(a,x,512,{}),render(c,x,512,{})),"reset restores fresh hidden state (incl. 130ms gate release)");}
 // 6. Rate change re-derives every rate-dependent hidden coefficient: a
 //    re-prepared instance equals a fresh one at the new rate.
 {Processor a=make(-18,-20,-3,44100); render(a,x,256,{});
  a.prepare(96000,2,512); Processor b=make(-18,-20,-3,96000);
  check(a.latencySamples()==b.latencySamples(),"rate change lookahead");
  check(same(render(a,x,256,{}),render(b,x,256,{})),"re-prepare equals fresh instance at new rate");}
 std::puts("PASS automation/preset hidden-state matrix (gain/gate/resend/partition/recall/re-prepare)");
}
