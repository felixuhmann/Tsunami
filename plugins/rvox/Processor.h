// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace tsunami::rvox {
class Processor {
public:
 void prepare(double rate, unsigned channels, unsigned) {
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || channels < 1 || channels > 2)
   throw std::invalid_argument("rvox supports 8-384kHz, mono/stereo");
  rate_=rate; channels_=channels; lookahead_=1;
  while (lookahead_<static_cast<unsigned>(rate/1000)) lookahead_*=2;
 }
 unsigned latencySamples() const noexcept { return lookahead_; }
private:
 double rate_=48000;
 unsigned channels_=1, lookahead_=64;
};
}
