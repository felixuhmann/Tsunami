# SPDX-License-Identifier: GPL-3.0-or-later
"""Regressions for release discovery, packaging, source bundling and verification."""
import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import release  # noqa: E402

COMMIT = 'a' * 40
S1_CALL = 'tsunami_plugin(TsunamiStereo "${TSUNAMI_PLUGIN_SOURCE_ROOT}/plugins/s1" s1 "Tsunami Stereo" TsS1 "tsunami::s1::Processor")'
FIXTURE_CALL = 'tsunami_plugin(TsunamiFixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/foundation/fixture" fixture "Tsunami Infrastructure Fixture" TsFx "tsunami::fixture::Processor")'


def git(cwd, *args):
    return subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True, text=True,
                          env={**os.environ, 'GIT_AUTHOR_NAME': 't', 'GIT_AUTHOR_EMAIL': 't@t', 'GIT_COMMITTER_NAME': 't',
                               'GIT_COMMITTER_EMAIL': 't@t', 'GIT_CONFIG_GLOBAL': os.devnull}).stdout.strip()


class Tmp(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()


class DiscoveryTest(Tmp):
    def make(self, calls, plugins=('s1',), files=('Processor.hpp', 'parameters.json')):
        root = Path(tempfile.mkdtemp(dir=self.tmp))
        (root / 'targets.json').write_text((ROOT / 'targets.json').read_text())
        (root / 'CMakeLists.txt').write_text(
            'function(tsunami_plugin target source id product code core)\nendfunction()\n'
            f'if(TSUNAMI_BUILD_FIXTURE)\n  {FIXTURE_CALL}\nendif()\n' + '\n'.join(calls) + '\n')
        for plugin in plugins:
            for name in files:
                path = root / 'plugins' / plugin / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('x')
        return release.discover(root)

    def test_actual_repository_includes_only_registered_s1_and_reports_every_target(self):
        result = release.discover(ROOT)
        targets = json.loads((ROOT / 'targets.json').read_text())['targets']
        self.assertEqual([p['id'] for p in result['included']], ['s1'])
        s1 = result['included'][0]
        self.assertEqual((s1['target'], s1['product'], s1['code']), ('TsunamiStereo', 'Tsunami Stereo', 'TsS1'))
        self.assertEqual(s1['artifact'], 'Tsunami-Stereo')
        reported = [p['id'] for p in result['included']] + [p['id'] for p in result['unavailable']]
        self.assertEqual(sorted(reported), sorted(t['id'] for t in targets))
        self.assertEqual(len(reported), 12)
        self.assertIn('ns1', reported)
        self.assertTrue(all(u['reason'] for u in result['unavailable']))
        self.assertEqual(result['excluded_fixtures'], ['fixture'])

    def test_comment_and_definition_are_not_registrations(self):
        result = self.make(['# ' + S1_CALL.replace('s1', 'rvox'), S1_CALL])
        self.assertEqual([p['id'] for p in result['included']], ['s1'])

    def test_future_rvox_registration_is_discovered(self):
        rvox = 'tsunami_plugin(TsunamiVoice "${TSUNAMI_PLUGIN_SOURCE_ROOT}/plugins/rvox" rvox "Tsunami Voice" TsRV "tsunami::rvox::Processor")'
        result = self.make([S1_CALL, rvox], plugins=('s1', 'rvox'))
        self.assertEqual([p['id'] for p in result['included']], ['rvox', 's1'])

    def test_present_but_unregistered_plugin_is_unavailable_with_reason(self):
        result = self.make([S1_CALL], plugins=('s1', 'rcomp'))
        rcomp = next(u for u in result['unavailable'] if u['id'] == 'rcomp')
        self.assertIn('not registered', rcomp['reason'])

    def test_malformed_registrations_fail(self):
        bad = [
            'tsunami_plugin(TsunamiStereo "${TSUNAMI_PLUGIN_SOURCE_ROOT}/plugins/s1" s1 "Tsunami Stereo" TsS1)',
            S1_CALL.replace('plugins/s1"', 'plugins/other"'),
            S1_CALL.replace(' s1 "', ' nope "'),
            S1_CALL.replace('"Tsunami Stereo"', '"Wrong Name"'),
            S1_CALL.replace('TsS1', 'TooLong'),
            S1_CALL.replace('TsunamiStereo', '../evil'),
            'tsunami_plugin(TsunamiStereo "${TSUNAMI_PLUGIN_SOURCE_ROOT}/plugins/s1" s1 "Tsunami Stereo" TsS1 "tsunami::s1::Processor"',
        ]
        for call in bad:
            with self.subTest(call=call), self.assertRaises(release.ReleaseError):
                self.make([call])

    def test_duplicate_registrations_fail(self):
        with self.assertRaises(release.ReleaseError):
            self.make([S1_CALL, S1_CALL])
        with self.assertRaises(release.ReleaseError):
            self.make([S1_CALL, S1_CALL.replace('TsunamiStereo', 'TsunamiStereo2').replace('TsS1', 'TsS2')])

    def test_registered_without_core_or_metadata_fails(self):
        for files in (('Processor.hpp',), ('parameters.json',)):
            with self.subTest(files=files), self.assertRaises(release.ReleaseError):
                self.make([S1_CALL], files=files)

    def test_no_registered_target_fails_closed(self):
        with self.assertRaises(release.ReleaseError):
            self.make([])


class VersionTest(unittest.TestCase):
    def test_derived_and_validated(self):
        self.assertEqual(release.derive_version('17', 'ABCDEF0123' + '0' * 30), 'preview-17-abcdef0')
        for run, sha in (('1;rm', COMMIT), ('', COMMIT), ('1', 'xyz'), ('1', 'a' * 39), ('01x', COMMIT)):
            with self.subTest(run=run, sha=sha), self.assertRaises(release.ReleaseError):
                release.derive_version(run, sha)


def fake_artefacts(base, product='Tsunami Stereo', platform='linux-x64'):
    arch = release.PLATFORMS[platform]
    binary = base / 'VST3' / f'{product}.vst3' / 'Contents' / arch['vst3_dir'] / (product + arch['vst3_ext'])
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b'\x7fELF module')
    (base / 'VST3' / f'{product}.vst3' / 'Contents' / 'Resources').mkdir()
    (base / 'VST3' / f'{product}.vst3' / 'Contents' / 'Resources' / 'moduleinfo.json').write_text('{}')
    (base / 'CLAP').mkdir()
    (base / 'CLAP' / f'{product}.clap').write_bytes(b'\x7fELF clap')
    return base


def fake_source_manifest(tmp, commit=COMMIT, deps=None):
    archive = tmp / 'tsunami-source.tar.gz'
    archive.write_bytes(b'source bytes')
    manifest = {'schema': 1, 'kind': 'corresponding-source', 'commit': commit,
                'lock_sha256': release.lock_digest(ROOT / 'dependencies.lock.json'),
                'dependencies': deps or {'JUCE': '9.0.4'}, 'archive': archive.name,
                'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest()}
    path = tmp / 'tsunami-source.manifest.json'
    path.write_text(json.dumps(manifest))
    return path


class PackageTest(Tmp):
    def stage(self, artefacts=None, platform='linux-x64', deps=None, source=None):
        artefacts = artefacts or fake_artefacts(Path(tempfile.mkdtemp(dir=self.tmp)) / 'art', platform=platform)
        source = source or fake_source_manifest(self.tmp)
        return release.stage_package(
            root=ROOT, plugin_id='s1', product='Tsunami Stereo', platform=platform, artefacts=artefacts,
            version='preview-1-aaaaaaa', commit=COMMIT, deps_identity=deps or {'JUCE': '9.0.4'},
            source_manifest=source, out=self.tmp / 'dist')

    def test_stages_whole_bundle_clap_licenses_manifest_and_checksums(self):
        zpath = self.stage()
        self.assertEqual(zpath.name, 'Tsunami-Stereo-preview-1-aaaaaaa-linux-x64.zip')
        with zipfile.ZipFile(zpath) as z:
            names = set(z.namelist())
            prefix = 'Tsunami-Stereo-preview-1-aaaaaaa-linux-x64/'
            for name in ('VST3/Tsunami Stereo.vst3/Contents/x86_64-linux/Tsunami Stereo.so',
                         'VST3/Tsunami Stereo.vst3/Contents/Resources/moduleinfo.json',
                         'CLAP/Tsunami Stereo.clap', 'licenses/LICENSE', 'licenses/AGPL-3.0.txt',
                         'licenses/THIRD_PARTY_NOTICES.md', 'licenses/dependencies.lock.json',
                         'MANIFEST.json', 'SHA256SUMS', 'INSTALL.md'):
                self.assertIn(prefix + name, names)
            manifest = json.loads(z.read(prefix + 'MANIFEST.json'))
            self.assertEqual((manifest['commit'], manifest['platform'], manifest['id']), (COMMIT, 'linux-x64', 's1'))
            self.assertEqual(manifest['corresponding_source']['archive'], 'tsunami-source.tar.gz')
            sums = z.read(prefix + 'SHA256SUMS').decode().splitlines()
            listed = {line.split('  ', 1)[1]: line.split('  ', 1)[0] for line in sums}
            for name in names:
                if name.endswith('/') or name.endswith('SHA256SUMS'):
                    continue
                self.assertEqual(listed[name[len(prefix):]], hashlib.sha256(z.read(name)).hexdigest())
        self.assertTrue(release.verify_package(zpath, fake_source_manifest(self.tmp), self.tmp / 'x').is_dir())

    def test_windows_layout(self):
        zpath = self.stage(platform='windows-x64')
        with zipfile.ZipFile(zpath) as z:
            self.assertTrue(any(n.endswith('Contents/x86_64-win/Tsunami Stereo.vst3') for n in z.namelist()))

    def test_missing_empty_or_symlinked_modules_fail(self):
        def missing_vst3(a):
            (a / 'VST3/Tsunami Stereo.vst3/Contents/x86_64-linux/Tsunami Stereo.so').unlink()

        def empty_vst3(a):
            (a / 'VST3/Tsunami Stereo.vst3/Contents/x86_64-linux/Tsunami Stereo.so').write_bytes(b'')

        def missing_clap(a):
            (a / 'CLAP/Tsunami Stereo.clap').unlink()

        def empty_clap(a):
            (a / 'CLAP/Tsunami Stereo.clap').write_bytes(b'')

        def symlink(a):
            os.symlink('/etc/passwd', a / 'VST3/Tsunami Stereo.vst3/Contents/Resources/link')

        def private(a):
            (a / 'VST3/Tsunami Stereo.vst3/Contents/Resources/x.wtar').write_bytes(b'x')

        for i, mutate in enumerate((missing_vst3, empty_vst3, missing_clap, empty_clap, symlink, private)):
            with self.subTest(mutate=mutate.__name__):
                art = fake_artefacts(self.tmp / f'a{i}')
                mutate(art)
                with self.assertRaises(release.ReleaseError):
                    self.stage(artefacts=art)

    def test_dependency_or_commit_mismatch_with_source_fails(self):
        with self.assertRaises(release.ReleaseError):
            self.stage(deps={'JUCE': '9.0.3'})
        with self.assertRaises(release.ReleaseError):
            self.stage(source=fake_source_manifest(self.tmp, commit='b' * 40))


class VerifyTest(Tmp):
    def setUp(self):
        super().setUp()
        self.source = fake_source_manifest(self.tmp)
        self.zip = release.stage_package(
            root=ROOT, plugin_id='s1', product='Tsunami Stereo', platform='linux-x64',
            artefacts=fake_artefacts(self.tmp / 'art'), version='preview-1-aaaaaaa', commit=COMMIT,
            deps_identity={'JUCE': '9.0.4'}, source_manifest=self.source, out=self.tmp / 'dist')
        with zipfile.ZipFile(self.zip) as z:
            self.members = [(i, z.read(i)) for i in z.infolist()]

    def rewrite(self, mutate):
        out = self.tmp / 'bad.zip'
        with zipfile.ZipFile(out, 'w') as z:
            for info, data in mutate(list(self.members)):
                z.writestr(info, data)
        return out

    def assert_rejected(self, path, source=None):
        with self.assertRaises(release.ReleaseError):
            release.verify_package(path, source or self.source, self.tmp / 'out')

    def test_tampered_binary_rejected(self):
        self.assert_rejected(self.rewrite(lambda m: [(i, d + b'!' if i.filename.endswith('.so') else d) for i, d in m]))

    def test_traversal_absolute_and_backslash_rejected(self):
        for name in ('../evil', '/abs', 'p/..\\evil', 'C:/x'):
            with self.subTest(name=name):
                self.assert_rejected(self.rewrite(lambda m: m + [(zipfile.ZipInfo(name), b'x')]))

    def test_symlink_member_rejected(self):
        def add(m):
            info = zipfile.ZipInfo(m[0][0].filename.split('/')[0] + '/VST3/link')
            info.external_attr = (0o120777 << 16)
            return m + [(info, b'/etc/passwd')]
        self.assert_rejected(self.rewrite(add))

    def test_unexpected_content_rejected(self):
        prefix = self.members[0][0].filename.split('/')[0]
        self.assert_rejected(self.rewrite(lambda m: m + [(zipfile.ZipInfo(prefix + '/extra.txt'), b'x')]))
        self.assert_rejected(self.rewrite(lambda m: m + [(zipfile.ZipInfo('Other/MANIFEST.json'), b'x')]))

    def test_source_identity_and_archive_hash_checked(self):
        self.assert_rejected(self.zip, fake_source_manifest(self.tmp / '..' / self.tmp.name, commit='b' * 40))
        good = fake_source_manifest(self.tmp)
        (self.tmp / 'tsunami-source.tar.gz').write_bytes(b'tampered')
        self.assert_rejected(self.zip, good)


class SourceTest(Tmp):
    def setUp(self):
        super().setUp()
        # Isolate from runner-wide git config (e.g. Windows core.autocrlf) for release._git too.
        for key, value in (('GIT_CONFIG_NOSYSTEM', '1'), ('GIT_CONFIG_GLOBAL', os.devnull)):
            self.addCleanup(lambda k=key, v=os.environ.get(key): os.environ.pop(k) if v is None else os.environ.__setitem__(k, v))
            os.environ[key] = value

    def make_repo(self, extra=None):
        repo = self.tmp / 'repo'
        for rel in release.REQUIRED_PROJECT_PATHS:
            path = repo / rel
            if rel.endswith('/'):
                path.mkdir(parents=True, exist_ok=True)
                (path / 'keep.py').write_text('x')
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('x')
        lock = {'dependencies': [
            {'name': 'JUCE', 'commit': 'c' * 40, 'version': '9.0.4', 'source_subdir': 'juce-src'},
            {'name': 'clap-juce-extensions', 'commit': None, 'source_subdir': 'clap_juce-src'},
            {'name': 'clap', 'commit': None, 'source_subdir': 'clap_juce-src/clap-libs/clap'}]}
        for rel, data in (extra or {}).items():
            (repo / rel).parent.mkdir(parents=True, exist_ok=True)
            (repo / rel).write_text(data)
        deps = self.tmp / 'build' / '_deps'
        (deps / 'juce-src').mkdir(parents=True)
        (deps / 'juce-src' / 'CMakeLists.txt').write_text('project(JUCE VERSION 9.0.4 LANGUAGES C CXX)\n')
        (deps / 'juce-src' / 'JUCE.spdx.json').write_text('{}')
        (deps / 'juce-build').mkdir()
        (deps / 'juce-build' / 'junk.o').write_text('x')
        for sub in ('clap_juce-src/clap-libs/clap', 'clap_juce-src'):
            path = deps / sub
            path.mkdir(parents=True, exist_ok=True)
            (path / 'LICENSE.md').write_text('MIT ' + sub)
        clap = deps / 'clap_juce-src/clap-libs/clap'
        git(clap, 'init', '-q')
        git(clap, 'add', '.')
        git(clap, 'commit', '-qm', 'clap')
        top = deps / 'clap_juce-src'
        git(top, 'init', '-q')
        (top / '.gitignore').write_text('clap-libs/\n')
        git(top, 'add', '.')
        git(top, 'commit', '-qm', 'top')
        lock['dependencies'][1]['commit'] = git(top, 'rev-parse', 'HEAD')
        lock['dependencies'][2]['commit'] = git(clap, 'rev-parse', 'HEAD')
        (repo / 'dependencies.lock.json').write_text(json.dumps(lock))
        git(repo, 'init', '-q')
        git(repo, 'add', '.')
        git(repo, 'commit', '-qm', 'init')
        return repo, deps, git(repo, 'rev-parse', 'HEAD')

    def test_bundles_exact_project_and_all_dependency_sources(self):
        repo, deps, commit = self.make_repo()
        manifest_path = release.stage_source(repo, deps, commit, 'preview-1-' + commit[:7], self.tmp / 'dist')
        manifest = json.loads(manifest_path.read_text())
        archive = manifest_path.parent / manifest['archive']
        self.assertEqual(manifest['archive_sha256'], hashlib.sha256(archive.read_bytes()).hexdigest())
        self.assertEqual(manifest['commit'], commit)
        self.assertEqual(manifest['dependencies']['JUCE'], '9.0.4')
        with tarfile.open(archive) as t:
            names = t.getnames()
            self.assertTrue(all(not m.issym() and not m.islnk() for m in t.getmembers()))
            inner = json.loads(t.extractfile(next(n for n in names if n.endswith('/SOURCE-MANIFEST.json'))).read())
        self.assertEqual(inner['commit'], commit)
        base = names[0].split('/')[0]
        for rel in ('project/CMakeLists.txt', 'project/.github/workflows/release.yml', 'project/tools/release.py',
                    'project/tests/release/keep.py', 'dependencies/juce-src/JUCE.spdx.json',
                    'dependencies/clap_juce-src/LICENSE.md', 'dependencies/clap_juce-src/clap-libs/clap/LICENSE.md',
                    'BUILDING.md'):
            self.assertIn(f'{base}/{rel}', names)
        self.assertFalse(any('/.git/' in n or n.endswith('/.git') or 'juce-build' in n for n in names))
        # Deterministic: same inputs give identical bytes.
        again = release.stage_source(repo, deps, commit, 'preview-1-' + commit[:7], self.tmp / 'dist2')
        self.assertEqual(json.loads(again.read_text())['archive_sha256'], manifest['archive_sha256'])

    def test_missing_wrong_or_unlocked_dependency_fails(self):
        repo, deps, commit = self.make_repo()
        (deps / 'juce-src' / 'CMakeLists.txt').write_text('project(JUCE VERSION 8.0.0)\n')
        with self.assertRaises(release.ReleaseError):
            release.stage_source(repo, deps, commit, 'v', self.tmp / 'd1')
        (deps / 'juce-src' / 'CMakeLists.txt').write_text('project(JUCE VERSION 9.0.4 LANGUAGES C CXX)\n')
        (deps / 'mystery-src').mkdir()
        (deps / 'mystery-src' / 'x').write_text('x')
        with self.assertRaises(release.ReleaseError):
            release.stage_source(repo, deps, commit, 'v', self.tmp / 'd2')
        (deps / 'mystery-src' / 'x').unlink()
        (deps / 'mystery-src').rmdir()
        git(deps / 'clap_juce-src', 'commit', '-q', '--allow-empty', '-m', 'drift')
        with self.assertRaises(release.ReleaseError):
            release.stage_source(repo, deps, commit, 'v', self.tmp / 'd3')

    def test_commit_mismatch_dirty_tree_and_private_paths_fail(self):
        repo, deps, commit = self.make_repo()
        with self.assertRaises(release.ReleaseError):
            release.stage_source(repo, deps, 'b' * 40, 'v', self.tmp / 'd1')
        (repo / 'CMakeLists.txt').write_text('dirty')
        with self.assertRaises(release.ReleaseError):
            release.stage_source(repo, deps, commit, 'v', self.tmp / 'd2')
        repo2 = self.tmp / 'second'
        self.tmp, old = repo2, self.tmp
        self.tmp.mkdir()
        repo, deps, commit = self.make_repo(extra={'references/original.wtar': 'vendor'})
        self.tmp = old
        with self.assertRaises(release.ReleaseError):
            release.stage_source(repo, deps, commit, 'v', self.tmp / 'd3')

    def test_juce_tree_content_is_bound(self):
        repo, deps, commit = self.make_repo()
        manifest = json.loads(release.stage_source(repo, deps, commit, 'v', self.tmp / 'd').read_text())
        self.assertEqual(manifest['dependencies'], release.verify_dependencies(repo, deps))
        self.assertRegex(manifest['dependencies']['JUCE-tree-sha256'], '^[0-9a-f]{64}$')
        (deps / 'juce-src' / 'JUCE.spdx.json').write_text('{"edited": true}')
        self.assertNotEqual(manifest['dependencies'], release.verify_dependencies(repo, deps))

    def test_symlink_in_dependency_fails(self):
        repo, deps, commit = self.make_repo()
        os.symlink('/etc/passwd', deps / 'juce-src' / 'link')
        with self.assertRaises(release.ReleaseError):
            release.stage_source(repo, deps, commit, 'v', self.tmp / 'd')


if __name__ == '__main__':
    unittest.main()
