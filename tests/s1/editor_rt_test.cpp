// SPDX-License-Identifier: GPL-3.0-or-later
// Regression: host automation notified from a non-message thread while the S1
// editor is open must not allocate per notification in editor-owned callbacks.
// JUCE attachments' AsyncUpdater message is pre-allocated; one warm notification
// is excluded so first-use costs are not attributed to the editor. This does not
// claim every JUCE host-automation path is allocation-free.
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Adapter.hpp"
#include "Generated.hpp"

static thread_local bool counting = false;
static std::atomic<int> allocations{0};
void* operator new(std::size_t size) {
    if (counting) allocations.fetch_add(1);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    tsunami::Adapter<tsunami::PluginSpec> p;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    if (!editor) return 1;
    int perNotification = -1;
    std::thread worker([&] {
        auto& params = p.getParameters();
        for (auto* parameter : params) parameter->setValueNotifyingHost(.3f); // warm: attachments queue once
        counting = true;
        for (int i = 0; i < 20; ++i)
            for (auto* parameter : params) parameter->setValueNotifyingHost(i % 2 ? .7f : .4f);
        counting = false;
        perNotification = allocations.load();
    });
    worker.join(); // message loop deliberately not pumped before measuring
    if (perNotification != 0) {
        std::cerr << "FAIL worker-thread notifications allocated " << perNotification << " times\n";
        return 2;
    }
    std::cout << "S1 editor worker-thread parameter notifications: 0 allocations after warm-up PASS\n";
    return 0;
}
