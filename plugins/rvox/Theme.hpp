// SPDX-License-Identifier: GPL-3.0-or-later
// RVox editor skins. Light is the existing project look. Dark and Legacy are
// independently authored theme alternatives, not measured reproductions of any
// original skin. The skin is UI only: no parameter, state or audio effect.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tsunami::rvox {
enum class Skin { light = 1, dark = 2, legacy = 3 }; // values are the selector item IDs

struct Palette {
    juce::Colour body, bodyShade, ink, value, accent, track, display, displayEdge, clipBand, brand, frameTop;
};

inline const Palette& palette(Skin s) {
    static const Palette light{juce::Colour(0xffe9e6de), juce::Colour(0xffd6d2c8), juce::Colour(0xff4a4a48), juce::Colour(0xff262626),
                               juce::Colour(0xffee8a1c), juce::Colour(0xff23466b), juce::Colour(0xff16324f), juce::Colour(0xff0c1b2b),
                               juce::Colour(0xff7d2027), juce::Colour(0xffa3262f), juce::Colours::white};
    static const Palette dark{juce::Colour(0xff2c2e33), juce::Colour(0xff1d1f23), juce::Colour(0xffd6d6d0), juce::Colour(0xfff4f4ef),
                              juce::Colour(0xfff29a3a), juce::Colour(0xff0f2236), juce::Colour(0xff0b1a2b), juce::Colour(0xff04090f),
                              juce::Colour(0xff7a1f26), juce::Colour(0xffe25a63), juce::Colour(0xff4a4d54)};
    static const Palette legacy{juce::Colour(0xffc4c7ca), juce::Colour(0xffa3a8ad), juce::Colour(0xff17212b), juce::Colour(0xff0a121a),
                                juce::Colour(0xff3c9a46), juce::Colour(0xff1d2329), juce::Colour(0xff10251a), juce::Colour(0xff070f0a),
                                juce::Colour(0xff6e2a1e), juce::Colour(0xff1f3f73), juce::Colour(0xffe8eaec)};
    return s == Skin::dark ? dark : s == Skin::legacy ? legacy : light;
}

inline const char* skinName(Skin s) { return s == Skin::dark ? "Dark" : s == Skin::legacy ? "Legacy" : "Light"; }

// Product-scoped default skin for new editors, one word in a small text file. Read
// when an editor opens and written when the user picks a skin, both on the message
// thread; never touched by the audio thread. Missing, unreadable or malformed files
// give Light; a failed write leaves the current editor's choice in place.
struct SkinPreference {
    // Normal location; tests point this at a temporary file before opening editors.
    static juce::File& location() {
        static juce::File f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("Tsunami").getChildFile("RVox").getChildFile("skin.txt");
        return f;
    }
    static Skin load() {
        JUCE_ASSERT_MESSAGE_THREAD
        const auto f = location();
        // A FIFO or device reports size 0 and opening a FIFO blocks, so reject a non-positive
        // size before opening (an empty file gives Light anyway). This covers such a path at
        // check time only; it is not a general non-blocking guarantee (the path can change
        // before the open, and ordinary file I/O can still block).
        const auto size = f.getSize();
        if (!f.existsAsFile() || size <= 0 || size > 64) return Skin::light;
        juce::FileInputStream in(f); // bounded read even if the file grew after the size check
        char text[65] = {};
        const auto n = in.openedOk() ? in.read(text, 65) : -1;
        if (n < 0 || n > 64) return Skin::light;
        const auto word = juce::String::fromUTF8(text, n).trim();
        for (auto s : {Skin::light, Skin::dark, Skin::legacy})
            if (word == juce::String(skinName(s)).toLowerCase()) return s;
        return Skin::light;
    }
    static bool save(Skin s) {
        JUCE_ASSERT_MESSAGE_THREAD
        const auto f = location();
        return f.getParentDirectory().createDirectory().wasOk() && f.replaceWithText(juce::String(skinName(s)).toLowerCase());
    }
};
} // namespace tsunami::rvox
