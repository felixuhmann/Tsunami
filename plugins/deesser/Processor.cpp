// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.h"
namespace tsunami::deesser {
void Processor::prepare(double,unsigned c,unsigned){channels_=c;}
void Processor::reset() noexcept {}
void Processor::setParameter(unsigned i,float v) noexcept {if(i<6)params_[i]=v;}
float Processor::getParameter(unsigned i) const noexcept {return i<6?params_[i]:0;}
void Processor::process(const float* const* in,float* const* out,unsigned n) noexcept {for(unsigned c=0;c<channels_;++c)for(unsigned i=0;i<n;++i)out[c][i]=params_[5]>0.5f?in[c][i]:0;}
}
