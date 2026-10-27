# Tsunami campaign rules

User goal: complete the exact 11 targets in targets.json, one PR per target, original UI/screenshots, reference evidence, native Windows/Linux support and verified release mechanisms. Direct REA-guided independent rewrite is authorized; no strict clean-room barrier. Do not copy/ship vendor source, pseudocode snippets, binaries or assets; do not bypass licensing.

Read docs/ACCEPTANCE.md and docs/PLUGIN_CONTRACT.md before coding. Keep one worker per plugin/worktree. Use TDD and actual source-grounded DSP, not generic approximations passed off as equivalents. No completion without native platform evidence and measured comparisons. Do not merge PRs automatically.

Private references/toolchain are OUTSIDE this repository under /home/felix/work/ns1-research and /home/felix/work/tsunami-reference; never add them to git. Per-plugin working state and worker leases live outside tracked source under /home/felix/work/tsunami-campaign/state/. A worker lease older than60minutes is not automatically safe to reclaim: inspect processes/commit progress first.

Heavy analysis/build jobs on this host are limited to two at once using file locks /home/felix/work/tsunami-campaign/locks/heavy-1.lock and heavy-2.lock; check MemAvailable and use bounded512MiB Ghidra heaps. Avoid repeated wholebinary reimports; inspect auxiliary unstripped kernels first and batch/use persistent REA where supported. More reasoning workers do not justify concurrent heavy jobs.

Durable campaign instructions: /home/felix/work/tsunami-campaign/ORCHESTRATOR.md. Use GitHub issues/PRs as externally verifiable task records. Never mark all done from a count of branches or PRs alone.
