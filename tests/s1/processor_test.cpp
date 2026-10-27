// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
void require(bool condition, const char* message) { if (!condition) { std::cerr << message << '\n'; std::exit(1); } }
int main() {
    tsunami::s1::Processor p;
    p.prepare(48000, 2, 16);
    float l[]{0.25f, 0.f, -0.25f}, r[]{0.f, 0.5f, 0.25f}, a[3]{}, b[3]{};
    const float* in[]{l,r}; float* out[]{a,b}; p.process(in,out,3);
    for(unsigned i=0;i<3;++i) {require(std::abs(a[i]-l[i])<1e-7f,"default left identity");require(std::abs(b[i]-r[i])<1e-7f,"default right identity");}
    require(p.latencySamples()==0 && p.tailSeconds()==0.,"matrix has no audio delay or tail");
    // Recovered widening law is NOT unnormalised generic M/S width.
    p.setParameter(1,3.f); p.reset(); p.process(in,out,3);
    const double q=std::sqrt((1.+0.6/9.)/3.2), s=std::sqrt(0.5)*q;
    require(std::abs(a[0]-float(4*s*.25))<1e-7f,"width3 normalized direct coefficient");
    require(std::abs(b[0]-float(-2*s*.25))<1e-7f,"width3 recovered crosstalk");
    require(p.getParameter(1)==3.f,"width physical roundtrip");
    p.setParameter(1,1.f);p.setParameter(2,30.f);p.reset();p.process(in,out,3);
    require(std::abs(a[0]-.25f*float(std::sqrt(.75)))<1e-7f,"rotation30 left coefficient");
    require(std::abs(b[0]-.125f)<1e-7f,"rotation30 right crosstalk coefficient");
    p.setParameter(2,0.f);p.setParameter(3,60.f);p.reset();p.process(in,out,3);
    require(std::abs(a[0]-.25f*float((std::sqrt(.5)+std::cos(105.*std::acos(-1.)/180.))*std::sqrt(.5)))<1e-7f,"asymmetry60 left coefficient");
    p.setParameter(3,0.f);p.setParameter(0,-6.f);p.reset();p.process(in,out,3);
    require(std::abs(a[0]-.25f*float(std::pow(10.,-.3)))<1e-7f,"gain minus6");
    p.setParameter(0,0.f);p.setParameter(4,1.f);p.reset();p.process(in,out,3);
    require(std::abs(a[0]-r[0])<1e-7f && std::abs(b[0]-l[0])<1e-7f,"swap is input columns, not UI label only");
    p.setParameter(4,0.f);p.setParameter(5,1.f);p.reset();p.process(in,out,3);
    require(a[0]==-l[0] && b[1]==r[1],"left input polarity");
    p.setParameter(5,0.f);p.setParameter(6,1.f);p.reset();p.process(in,out,3);
    require(a[0]==l[0] && b[1]==-r[1],"right input polarity");
    p.setParameter(6,0.f);p.setParameter(7,1.f);p.reset();p.process(in,out,3);
    require(a[0]==l[0]+r[0] && b[1]==l[1]-r[1],"M/S input uses sum and difference without half scale");
    p.setParameter(8,1.f);p.process(in,out,3);require(a[1]==l[1] && b[1]==r[1],"explicit replacement bypass");
    tsunami::s1::Processor smooth;
    smooth.prepare(48000,2,16);
    smooth.process(in,out,3);smooth.setParameter(1,0.f);
    smooth.process(in,out,1);
    const double alpha=float(std::pow(0.36787944117,1./(48000.*.040)));
    require(std::abs(a[0]-float(.25*(.5+.5*alpha)))<1e-7f,"40ms recovered coefficient smoothing");
    smooth.reset();smooth.process(in,out,1);require(std::abs(a[0]-.125f)<1e-7f,"reset snaps coefficients and preserves controls");
    smooth.setParameter(1,99);require(smooth.getParameter(1)==3,"bounded width");
    smooth.setParameter(1,NAN);require(smooth.getParameter(1)==3,"ignore nonfinite control");
    smooth.prepare(44100,1,16);const float* monoIn[]{l};float* monoOut[]{a};smooth.process(monoIn,monoOut,3);
    require(a[0]==l[0],"unsupported mono safety passthrough (not stereo equivalence)");
    std::cout << "recovered controls, smoothing and safety passed\n";
}
