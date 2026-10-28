# SPDX-License-Identifier: GPL-3.0-or-later
"""Configuration regressions for automatic main-branch preview releases."""
import re
import os
import subprocess
import shutil
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / '.github/workflows/release.yml'


def git_bash(git):
    # PATH may expose bin/git.exe, cmd/git.exe, or the real ucrt64/bin/git.exe.
    # Find the installation's existing Bash rather than assuming a fixed depth.
    for parent in Path(git).resolve().parents:
        candidate = parent / 'bin' / 'bash.exe'
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f'No Git Bash found beside Git executable: {git}')


class ReleaseWorkflowTest(unittest.TestCase):
    def test_git_bash_resolution_handles_runner_git_layouts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bash = root / 'bin' / 'bash.exe'
            bash.parent.mkdir()
            bash.touch()
            for layout in ('bin', 'cmd', 'ucrt64/bin', 'mingw64/bin'):
                with self.subTest(layout=layout):
                    git = root / layout / 'git.exe'
                    git.parent.mkdir(parents=True, exist_ok=True)
                    git.touch()
                    self.assertEqual(git_bash(git), bash.resolve())

    def test_every_main_push_runs_without_path_filters_or_coalescing(self):
        workflow = WORKFLOW.read_text()
        push = re.search(r'^  push:\n(?P<body>(?:^    .*\n)+)', workflow, re.MULTILINE)
        if push is None:
            self.fail('Release workflow must run on main pushes')
        self.assertEqual(push['body'].strip(), 'branches: [main]')
        match = re.search(r'^  group: (.+)$', workflow, re.MULTILINE)
        if match is None:
            self.fail('Release workflow needs an explicit concurrency group')
        group = match.group(1)
        self.assertIn('github.run_id', group, 'Every push needs a distinct concurrency group')
        self.assertIn('pull_request', group, 'PR runs may still cancel superseded PR runs')

    def test_publication_is_main_only_after_all_package_gates(self):
        workflow = WORKFLOW.read_text()
        publication = workflow.split('  publish-preview:\n', 1)[1]
        self.assertIn("    if: github.ref == 'refs/heads/main' && (github.event_name == 'push' || (github.event_name == 'workflow_dispatch' && inputs.publish_preview == true))\n", publication)
        self.assertIn('    needs: [discover, source, build, downloaded-smoke]\n', publication)
        self.assertIn('      contents: write\n', publication)

    def test_push_publishes_prerelease_but_manual_opt_in_remains_draft(self):
        publication = WORKFLOW.read_text().split('  publish-preview:\n', 1)[1]
        script = textwrap.dedent(publication.split('        run: |\n', 1)[1])
        bash = shutil.which('bash')
        if os.name == 'nt':
            # A bare bash resolves to Windows' WSL launcher, even though the
            # workflow itself runs under Git Bash. WSL has no installed distro
            # on windows-2022. Use the Bash next to the actual Git installation.
            git = shutil.which('git')
            if git is None:
                self.fail('Git for Windows is required for the publication regression')
            bash = str(git_bash(git))
        if bash is None or not Path(bash).is_file():
            self.fail(f'Publication regression needs a real Bash executable: {bash}')
        for event in ('push', 'workflow_dispatch'):
            with self.subTest(event=event), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                assets = root / 'assets'
                assets.mkdir()
                version = 'preview-123-aaaaaaa'
                for name in (f'Tsunami-source-{version}.tar.gz',
                             f'Tsunami-source-{version}.manifest.json',
                             f'Tsunami-Stereo-{version}-linux-x64.zip'):
                    (assets / name).write_bytes(b'workflow test fixture')
                (assets / 'unavailable.json').write_text('[]')
                arguments = root / 'gh-arguments'
                env = {**os.environ, 'VERSION': version, 'COMMIT': 'a' * 40,
                       'GITHUB_REPOSITORY': 'example/fixture', 'EVENT_NAME': event,
                       'GH_ARGUMENTS': arguments.as_posix()}
                # Exercise the real publication shell with a recording gh function;
                # no GitHub release or network call is made by this regression.
                recording_gh = 'gh() { printf "%s\\0" "$@" > "$GH_ARGUMENTS"; };\n'
                result = subprocess.run([bash, '-euo', 'pipefail', '-c', recording_gh + script],
                                        cwd=root, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0,
                                 f'Publication shell failed on {os.name}:\n'
                                 f'stdout:\n{result.stdout}\nstderr:\n{result.stderr}')
                args = arguments.read_bytes().decode().split('\0')[:-1]
                self.assertEqual(args[:3], ['release', 'create', version])
                self.assertIn('--prerelease', args)
                self.assertIn('--latest=false', args)
                self.assertEqual('--draft' in args, event == 'workflow_dispatch')
                self.assertEqual(args[args.index('--target') + 1], 'a' * 40)
                self.assertIn('SHA256SUMS', args)
                self.assertIn(f'Tsunami-source-{version}.tar.gz', args)
                self.assertIn('unavailable.json', args)


if __name__ == '__main__':
    unittest.main()
