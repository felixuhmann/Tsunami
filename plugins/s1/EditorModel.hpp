// SPDX-License-Identifier: GPL-3.0-or-later
// Framework-independent model for the S1 control-transfer vector display and drags.
#pragma once
#include "Processor.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace tsunami::s1 {
struct VectorPoint { float angleDegrees, levelDb; }; // 0 deg = centre/up, unity L at -45, -90 = full side
struct VectorModel { VectorPoint left, center, right; };

inline VectorPoint vectorPoint(double l, double r) noexcept {
    constexpr double degrees=57.29577951308232;
    // Ordinary Mid/Side polar coordinates: x=(R-L)/sqrt2 (right), y=(R+L)/sqrt2 (up).
    // Exact original angle/radial projection is not established (pending reference).
    const double angle=std::atan2(r-l, r+l)*degrees;
    const double magnitude=std::hypot(l,r);
    return {float(angle), float(magnitude>1e-6 ? 20.*std::log10(magnitude) : -120.)};
}

// Uses a private Processor instance (never the audio-thread core) so the display
// is derived from exactly the same matrix as the DSP. Parameters in physical units.
inline VectorModel vectorModel(const std::array<float,Processor::Count>& parameters) noexcept {
    Processor model;
    for(unsigned i=0;i<Processor::Count;++i) model.setParameter(i, parameters[i]);
    const auto m=model.matrix();
    constexpr double centre=0.7071067811865476; // equal-power centre input
    return {vectorPoint(m[0],m[3]), vectorPoint(centre*(m[0]+m[1]),centre*(m[3]+m[2])), vectorPoint(m[1],m[2])};
}

// Display drag per manual key commands: plain X=rotation, Y=gain; Alt X=asymmetry, Y=width.
// image = {gain dB, width, rotation deg, asymmetry deg}; dx/dy in pixels (dy positive downwards).
inline std::array<float,4> applyVectorDrag(std::array<float,4> image, float dx, float dy, bool alt) noexcept {
    if(alt) {
        image[3]=std::clamp(image[3]+dx*.5f,-90.f,90.f);
        image[1]=std::clamp(image[1]-dy*.01f,0.f,3.f);
    } else {
        image[2]=std::clamp(image[2]+dx*.25f,-45.f,45.f);
        image[0]=std::clamp(image[0]-dy*.05f,-24.f,0.f);
    }
    return image;
}
} // namespace tsunami::s1
