#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Pedalboard retains audio/state coverage; pluginval owns only Linux GUI testing.
# pluginval does not require an EWMH window manager or Pedalboard show_editor.
set -euo pipefail
python tests/foundation/host_smoke.py "$1" evidence
python tests/foundation/linux_editor_capture.py "$1" evidence "$2"
