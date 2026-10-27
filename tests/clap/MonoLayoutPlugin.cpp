// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic mono-capable entry point for the infrastructure fixture core. Test-only: not a target.
#include "Adapter.hpp"
#include "Generated.hpp"
namespace {
struct MonoLayoutSpec : tsunami::PluginSpec { static constexpr bool supportsMono = true; };
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tsunami::Adapter<MonoLayoutSpec>(); }
