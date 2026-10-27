#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Test-only display orchestration around the mature Pedalboard host.
set -euo pipefail
openbox > evidence/window-manager.log 2>&1 &
wm=$!
trap 'kill "$wm" 2>/dev/null || true' EXIT
ready=0
for ((attempt=0; attempt<40; ++attempt)); do
    if [[ "$(xprop -root _NET_SUPPORTING_WM_CHECK)" == *"window id # 0x"* ]]; then
        ready=1
        break
    fi
    sleep 0.1
done
[[ "$ready" == 1 ]] || { printf 'Window manager did not publish EWMH readiness\n' >&2; exit 1; }
python tests/foundation/host_smoke.py "$1" evidence --capture
