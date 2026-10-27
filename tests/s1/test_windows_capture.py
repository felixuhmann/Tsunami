# SPDX-License-Identifier: GPL-3.0-or-later
"""Portable tests of the Windows editor-window selection policy using SYNTHETIC window
records. Not evidence of native capture; native Windows CI is the authoritative proof."""
import unittest
from windows_editor_capture import select_editor_window


def record(hwnd, pid=7, visible=True, child=False, client=(1048, 665)):
    return dict(hwnd=hwnd, pid=pid, visible=visible, cls='JUCE_x', child=child, client=client, rect=(0, 0, 0, 0))


class Selection(unittest.TestCase):
    def test_picks_visible_body_window_of_own_process(self):
        chosen = select_editor_window([record(1, pid=8), record(2, visible=False), record(3, client=(1024, 665)), record(4)], 7)
        self.assertEqual(chosen['hwnd'], 4)

    def test_prefers_top_level_over_child_of_same_size(self):
        self.assertEqual(select_editor_window([record(5, child=True), record(6)], 7)['hwnd'], 6)

    def test_child_only_is_accepted(self):
        self.assertEqual(select_editor_window([record(5, child=True), record(6, client=(1064, 704))], 7)['hwnd'], 5)

    def test_fails_closed_without_editor(self):
        with self.assertRaises(LookupError):
            select_editor_window([record(1, pid=8), record(2, client=(1024, 665))], 7)

    def test_ambiguous_same_level_fails(self):
        with self.assertRaises(LookupError):
            select_editor_window([record(1), record(2)], 7)


if __name__ == '__main__':
    unittest.main()
