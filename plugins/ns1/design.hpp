// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mono.hpp"
namespace tsunami::ns1 {
// Experimental: only the three measured controls; initialization is supplied separately.
MonoConfiguration designSampledMono(double sampleRate,float normalizedControl);
MonoInitialState initialMonoState() noexcept;
}
