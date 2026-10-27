# Parallel implementation contract v1

Each worker owns only `plugins/<id>/`, `tests/<id>/` and `docs/evidence/<id>/` on its own `plugin/<id>` branch/worktree. Do not edit root build/CI/shared files; request foundation integration instead. The foundation worker owns common framework/build/UI adapters.

Implement independent portable C++17 DSP in namespace `tsunami::<id>`, class `Processor`, with these public operations:

- `void prepare(double sampleRate, unsigned channels, unsigned maxBlockSize)` — allocate/precompute off the audio thread.
- `void reset() noexcept` — clear documented runtime state.
- `void setParameter(unsigned index, float physicalValue) noexcept` — bounded realtime-safe control updates.
- `float getParameter(unsigned index) const noexcept`.
- `void process(const float* const* inputs, float* const* outputs, unsigned frames) noexcept` — channel-major noninterleaved float32; support in-place, no audio-thread allocation/I/O/locks.
- `unsigned latencySamples() const noexcept` and `double tailSeconds() const noexcept`.

Keep parameter metadata in `plugins/<id>/parameters.json`: stable index/id, name, physical minimum/maximum/default/unit, type/logarithmic flags and UI grouping. Provide a plugin-local CMakeLists and standalone test executable so progress does not wait on framework/UI integration. UI adapters may wrap this contract; if the real reference requires an extension, document evidence and coordinate instead of guessing.

Each worker must record measured scope/limitations, actual TDD/test output and reproducible commands in `docs/evidence/<id>/README.md`, checkpoint local commits, then report for parent verification. A partial implementation must be explicitly labelled partial and must not receive a verified status.
