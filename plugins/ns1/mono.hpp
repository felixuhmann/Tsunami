// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "detector.hpp"
#include <cstddef>
namespace tsunami::ns1 {
struct FrontCoefficients {
 AllpassCoefficients second{};
 float first=0, firstWeight=0, secondWeight=-1;
};
struct BandConfiguration {
 DetectorCoefficients detector{};
 std::array<AllpassCoefficients,3> audio{};
 float scale=1, activityRatio=0;
};
struct MonoConfiguration {
 std::array<FrontCoefficients,2> front{};
 std::array<BandConfiguration,6> bands{};
 float activityGain=1, minimumActivityGain=1,adaptive=1,thresholdBlend=1;
 unsigned activityOnSamples=1,activityOffSamples=1,activityBandLast=3,activityBandCount=1;
};
struct MonoInitialState {
 std::array<DetectorState,6> bands{};
 float activityGain=1;
 unsigned activityOnCounter=0,activityOffCounter=0;
};
class Mono {
 public:
 Mono(const MonoConfiguration& c,const MonoInitialState& s) noexcept : config_(c) {reset(s);}
 void reset(const MonoInitialState&) noexcept;
 // Swap coefficients while keeping all runtime state (no allocation).
 void configure(const MonoConfiguration& c) noexcept {config_=c;}
 float process(float) noexcept;
 void process(const float* in,float* out,std::size_t frames) noexcept;
 // Linked stereo (shared detector); mono-only configuration and state otherwise.
 void processLinked(float left,float right,float& outLeft,float& outRight) noexcept;
 private:
 struct FrontState {AllpassState second{};double previousInput=0,previousOutput=0;};
 float frontStage(float,std::array<FrontState,2>&) noexcept;
 void detect(float front,std::array<float,6>& gains) noexcept;
 float audioStage(float front,const std::array<float,6>& gains,std::array<std::array<AllpassState,3>,6>&) noexcept;
 MonoConfiguration config_;
 std::array<FrontState,2> front_{},frontRight_{};
 std::array<std::array<AllpassState,3>,6> audioRight_{};
 std::array<std::array<AllpassState,3>,6> audio_{};
 std::array<DetectorState,6> detector_{};
 std::array<float,6> envelope_{};
 float activityGain_=1;
 unsigned on_=0,off_=0;
};
}
