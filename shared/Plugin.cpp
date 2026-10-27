// SPDX-License-Identifier: GPL-3.0-or-later
#include "Adapter.hpp"
#include "Generated.hpp"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tsunami::Adapter<tsunami::PluginSpec>(); }
