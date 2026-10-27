#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Release helper: target discovery, installable packages, corresponding source and verification.

Stdlib only. Every check fails closed with ReleaseError; nothing here builds,
signs or publishes. Preview packages are experimental and unsigned.
"""
import argparse
import gzip
import hashlib
import io
import json
import os
import re
import stat
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

PLATFORMS = {
    'linux-x64': {'vst3_dir': 'x86_64-linux', 'vst3_ext': '.so'},
    'windows-x64': {'vst3_dir': 'x86_64-win', 'vst3_ext': '.vst3'},
}
REQUIRED_PROJECT_PATHS = (
    'CMakeLists.txt', 'LICENSE', 'LICENSES/', 'THIRD_PARTY_NOTICES.md', 'dependencies.lock.json', 'targets.json',
    'tools/release.py', '.github/workflows/release.yml', 'tests/release/',
)
LICENSE_FILES = ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'dependencies.lock.json', 'docs/LICENSING.md')
PACKAGE_TOP = {'VST3', 'CLAP', 'licenses', 'MANIFEST.json', 'SHA256SUMS', 'INSTALL.md'}
PRIVATE_SEGMENTS = {'references', 'vendor-reference', 'ns1-research', 'tsunami-reference', 'local', '.env'}
PRIVATE_SUFFIXES = ('.wtar', '.aa', '.bundle')
LINKER_BYPRODUCTS = ('.pdb', '.ilk', '.exp', '.lib')
FIXTURE_IDS = {'fixture'}
SOURCE_PREFIX = '${TSUNAMI_PLUGIN_SOURCE_ROOT}/plugins/'
EPOCH = (1980, 1, 1, 0, 0, 0)
STATUS = 'experimental unsigned preview build; not verified as equivalent to any original product'


class ReleaseError(Exception):
    pass


def fail(message):
    raise ReleaseError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def lock_digest(path):
    """Line-ending independent digest of the dependency lock content."""
    return hashlib.sha256(json.dumps(json.loads(Path(path).read_text(encoding='utf-8')), sort_keys=True,
                                     separators=(',', ':')).encode()).hexdigest()


def check_private(rel):
    parts = PurePosixPath(rel).parts
    lowered = [p.lower() for p in parts]
    if any(p in PRIVATE_SEGMENTS for p in lowered) or any(p.endswith(PRIVATE_SUFFIXES) for p in lowered):
        fail(f'private/vendor path refused: {rel}')


# ---------------------------------------------------------------- discovery

def _strip_cmake_comments(text):
    out, i, quoted = [], 0, False
    while i < len(text):
        c = text[i]
        if quoted:
            out.append(c)
            if c == '\\' and i + 1 < len(text):
                out.append(text[i + 1])
                i += 1
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
            out.append(c)
        elif c == '#':
            bracket = re.match(r'#\[(=*)\[', text[i:])
            if bracket:
                end = text.find(']' + bracket.group(1) + ']', i)
                i = len(text) if end < 0 else end + len(bracket.group(1)) + 2
                continue
            end = text.find('\n', i)
            i = len(text) if end < 0 else end
            continue
        else:
            out.append(c)
        i += 1
    return ''.join(out)


def _call_args(text, start):
    args, i, current, quoted, depth = [], start, None, False, 0
    while i < len(text):
        c = text[i]
        if quoted:
            if c == '"':
                quoted = False
                args.append(current)
                current = None
            else:
                current += c
        elif c == '"':
            if current is not None:
                fail('malformed tsunami_plugin argument quoting')
            quoted, current = True, ''
        elif c == '(':
            depth += 1
            current = (current or '') + c
        elif c == ')' and depth:
            depth -= 1
            current += c
        elif c == ')':
            if current is not None:
                args.append(current)
            return args
        elif c.isspace():
            if current is not None:
                args.append(current)
                current = None
        else:
            current = (current or '') + c
        i += 1
    fail('unterminated tsunami_plugin(...) registration')


def parse_registrations(cmake_text):
    text = _strip_cmake_comments(cmake_text)
    return [_call_args(text, m.end()) for m in re.finditer(r'(?<![\w(])tsunami_plugin\s*\(', text)]


def discover(root):
    root = Path(root)
    targets = json.loads((root / 'targets.json').read_text(encoding='utf-8'))['targets']
    by_id = {t['id']: t for t in targets}
    if len(by_id) != len(targets):
        fail('duplicate ids in targets.json')
    included, fixtures, seen = [], [], {k: set() for k in ('id', 'target', 'product', 'code')}
    for args in parse_registrations((root / 'CMakeLists.txt').read_text(encoding='utf-8')):
        if len(args) != 6:
            fail(f'malformed tsunami_plugin registration (expected 6 arguments): {args}')
        target, source, pid, product, code, core = args
        if pid in FIXTURE_IDS:
            fixtures.append(pid)
            continue
        if pid not in by_id:
            fail(f'registered id {pid!r} is not an exact target in targets.json')
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', target):
            fail(f'invalid CMake target name {target!r}')
        if source != SOURCE_PREFIX + pid:
            fail(f'{pid}: source must be {SOURCE_PREFIX}{pid}, got {source!r}')
        if product != by_id[pid]['product_name'] or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 -]*', product):
            fail(f'{pid}: product {product!r} does not match targets.json {by_id[pid]["product_name"]!r}')
        if not re.fullmatch(r'[A-Za-z0-9]{4}', code):
            fail(f'{pid}: plugin code must be 4 alphanumerics, got {code!r}')
        if not re.fullmatch(rf'tsunami::{re.escape(pid)}::[A-Za-z_][A-Za-z0-9_:]*', core):
            fail(f'{pid}: core class {core!r} outside tsunami::{pid}::')
        for key, value in (('id', pid), ('target', target), ('product', product), ('code', code)):
            if value in seen[key]:
                fail(f'duplicate tsunami_plugin {key} {value!r}')
            seen[key].add(value)
        for name in ('Processor.hpp', 'parameters.json'):
            if not (root / 'plugins' / pid / name).is_file():
                fail(f'{pid}: registered but plugins/{pid}/{name} is missing')
        included.append({'id': pid, 'target': target, 'product': product, 'artifact': product.replace(' ', '-'),
                         'code': code, 'core': core,
                         'requested_name': by_id[pid]['requested_name']})
    if not included:
        fail('no registered target adapters discovered; refusing to build an empty release')
    included.sort(key=lambda p: p['id'])
    names = {p['id'] for p in included}
    unavailable = []
    for t in targets:
        if t['id'] in names:
            continue
        if (root / 'plugins' / t['id']).is_dir():
            reason = (f'plugins/{t["id"]} source is present but not registered via tsunami_plugin(...) in the root '
                      'CMakeLists.txt at this commit (partial/core-only); no native module is built')
        else:
            reason = ('not registered via tsunami_plugin(...) in the root CMakeLists.txt at this commit; no integrated '
                      'native adapter exists on this branch')
        unavailable.append({'id': t['id'], 'requested_name': t['requested_name'], 'product': t['product_name'],
                            'targets_json_status': t.get('status'), 'reason': reason})
    return {'schema': 1, 'included': included, 'unavailable': unavailable, 'excluded_fixtures': fixtures,
            'note': 'Included means a native adapter is registered and built by this workflow; it is not a claim of '
                    'verified equivalence or completion.'}


def derive_version(run_number, sha):
    if not re.fullmatch(r'[1-9][0-9]{0,9}', str(run_number)):
        fail(f'invalid run number {run_number!r}')
    if not re.fullmatch(r'[0-9a-fA-F]{40}', str(sha)):
        fail(f'invalid commit sha {sha!r}')
    return f'preview-{run_number}-{sha[:7].lower()}'


def _check_version(version):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.-]{0,63}', version):
        fail(f'invalid version {version!r}')


# ---------------------------------------------------------------- dependencies and source

def _git(cwd, *args):
    result = subprocess.run(['git', *args], cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        fail(f'git {" ".join(args)} failed in {cwd}: {result.stderr.strip()}')
    return result.stdout


def _no_symlinks(base):
    if base.is_symlink():
        fail(f'symlink refused: {base}')
    for dirpath, dirnames, filenames in os.walk(base):
        for name in dirnames + filenames:
            if (Path(dirpath) / name).is_symlink():
                fail(f'symlink refused: {Path(dirpath) / name}')


def _tree_digest(base):
    digest = hashlib.sha256()
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames.sort()
        for name in sorted(filenames):
            path = Path(dirpath) / name
            digest.update(path.relative_to(base).as_posix().encode() + b'\0' + sha256(path).encode() + b'\n')
    return digest.hexdigest()


def verify_dependencies(root, deps_dir):
    """Check fetched sources exactly match dependencies.lock.json; return observed identity."""
    deps_dir = Path(deps_dir)
    lock = json.loads((Path(root) / 'dependencies.lock.json').read_text(encoding='utf-8'))
    identity, top = {}, set()
    for dep in lock['dependencies']:
        sub = dep.get('source_subdir')
        if not sub:
            fail(f'lock entry {dep.get("name")} lacks source_subdir')
        path = deps_dir / sub
        if not path.is_dir() or not any(p.name != '.git' for p in path.iterdir()):
            fail(f'dependency source missing or empty: {path}')
        if (path / '.git').exists():
            head = _git(path, 'rev-parse', 'HEAD').strip()
            if head != dep.get('commit'):
                fail(f'{dep["name"]}: fetched commit {head} != locked {dep.get("commit")}')
            identity[dep['name']] = head
        elif dep['name'] == 'JUCE':
            cmake = (path / 'CMakeLists.txt').read_text(encoding='utf-8', errors='replace')
            if not re.search(rf'project\(JUCE VERSION {re.escape(dep["version"])}[ \t\r\n]', cmake):
                fail(f'JUCE source is not version {dep["version"]}')
            identity[dep['name']] = dep['version']
            # URL-archive extraction has no line-ending conversion, so this digest is platform independent.
            identity[dep['name'] + '-tree-sha256'] = _tree_digest(path)
        else:
            fail(f'{dep["name"]}: cannot verify source identity (no git metadata)')
        if '/' not in sub:
            top.add(sub)
    for child in deps_dir.iterdir():
        if child.name.endswith('-src') and child.name not in top:
            fail(f'unlocked fetched dependency source {child.name}; add it to dependencies.lock.json')
    for sub in top:
        _no_symlinks(deps_dir / sub)
    return identity


class _DeterministicTar:
    def __init__(self, fileobj, base):
        self.tar = tarfile.open(fileobj=fileobj, mode='w', format=tarfile.PAX_FORMAT)
        self.base = base
        self.names = set()

    def add(self, rel, data, executable=False):
        name = f'{self.base}/{rel}'
        if name in self.names:
            fail(f'duplicate source member {name}')
        self.names.add(name)
        info = tarfile.TarInfo(name)
        info.size, info.mtime, info.mode = len(data), 0, 0o755 if executable else 0o644
        info.uid = info.gid = 0
        info.uname = info.gname = ''
        self.tar.addfile(info, io.BytesIO(data))


def _building_md(version, commit, included):
    ids = ', '.join(p['id'] for p in included) if included else '<id>'
    return f"""# Rebuilding Tsunami {version} from this Corresponding Source

Project commit: `{commit}`. `project/` is `git archive` of exactly that commit; `dependencies/` holds every
fetched dependency source tree (with all upstream license/copyright notices) pinned in
`project/dependencies.lock.json`. `SOURCE-MANIFEST.json` records the identities that were verified.

Requirements: CMake >= 3.22, a C++17 compiler (MSVC 2022 x64 on Windows, GCC/Clang on Linux), Python 3,
and on Linux the X11/ALSA/freetype/fontconfig development packages listed in
`project/.github/workflows/release.yml`. No network access is needed.

```
cmake -S project -B build -DCMAKE_BUILD_TYPE=Release -DTSUNAMI_BUILD_FIXTURE=OFF \\
  -DTSUNAMI_PLUGIN_SOURCE_ROOT="$PWD/project" -DTSUNAMI_PLUGIN_ID=<id> \\
  -DTSUNAMI_JUCE_SOURCE="$PWD/dependencies/juce-src" -DTSUNAMI_CLAP_SOURCE="$PWD/dependencies/clap_juce-src"
cmake --build build --config Release --parallel 2
```

Built ids in this release: {ids}. Modules appear under `build/<Target>_artefacts/Release/VST3` and `.../CLAP`.
Binaries were produced by GitHub Actions; byte-identical rebuilds are not claimed.
"""


def stage_source(root, deps_dir, commit, version, out, included=None):
    root, out = Path(root), Path(out)
    _check_version(version)
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        fail(f'invalid commit {commit!r}')
    head = _git(root, 'rev-parse', 'HEAD').strip()
    if head != commit:
        fail(f'checkout HEAD {head} != requested commit {commit}')
    if _git(root, 'status', '--porcelain').strip():
        fail('working tree is dirty; corresponding source would not match the build')
    identity = verify_dependencies(root, deps_dir)
    project = tarfile.open(fileobj=io.BytesIO(subprocess.run(['git', 'archive', '--format=tar', commit], cwd=root,
                                                              check=True, capture_output=True).stdout))
    members = [m for m in project.getmembers() if m.isfile() or m.isdir() or m.issym() or m.islnk()]
    files = {}
    for m in members:
        if m.issym() or m.islnk():
            fail(f'symlink refused in project source: {m.name}')
        if m.isfile():
            check_private(m.name)
            files[m.name] = (project.extractfile(m).read(), bool(m.mode & 0o111))
    for rel in REQUIRED_PROJECT_PATHS:
        if not (any(n.startswith(rel) for n in files) if rel.endswith('/') else rel in files):
            fail(f'required project path missing from source: {rel}')
    base = f'Tsunami-source-{version}'
    inner = {'schema': 1, 'kind': 'corresponding-source', 'version': version, 'commit': commit,
             'lock_sha256': lock_digest(root / 'dependencies.lock.json'), 'dependencies': identity,
             'project_files': len(files)}
    buffer = io.BytesIO()
    tar = _DeterministicTar(buffer, base)
    for name in sorted(files):
        tar.add('project/' + name, *files[name])
    deps_dir = Path(deps_dir)
    dep_files = 0
    lock = json.loads((root / 'dependencies.lock.json').read_text(encoding='utf-8'))
    for sub in sorted(d['source_subdir'] for d in lock['dependencies'] if '/' not in d['source_subdir']):
        for dirpath, dirnames, filenames in os.walk(deps_dir / sub):
            dirnames[:] = sorted(d for d in dirnames if d != '.git')
            for name in sorted(filenames):
                if name == '.git':
                    continue
                path = Path(dirpath) / name
                rel = path.relative_to(deps_dir).as_posix()
                tar.add('dependencies/' + rel, path.read_bytes(), os.access(path, os.X_OK))
                dep_files += 1
    inner['dependency_files'] = dep_files
    tar.add('BUILDING.md', _building_md(version, commit, included).encode())
    tar.add('SOURCE-MANIFEST.json', (json.dumps(inner, indent=2, sort_keys=True) + '\n').encode())
    tar.tar.close()
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f'{base}.tar.gz'
    with open(archive, 'wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as gz:
        gz.write(buffer.getvalue())
    manifest = {**inner, 'archive': archive.name, 'archive_sha256': sha256(archive)}
    path = out / f'{base}.manifest.json'
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    return path


# ---------------------------------------------------------------- packages

def _load_source_manifest(path):
    path = Path(path)
    manifest = json.loads(path.read_text(encoding='utf-8'))
    if manifest.get('kind') != 'corresponding-source':
        fail('not a corresponding-source manifest')
    archive = path.parent / manifest['archive']
    if PurePosixPath(manifest['archive']).name != manifest['archive'] or not archive.is_file():
        fail(f'corresponding source archive missing: {archive}')
    if sha256(archive) != manifest['archive_sha256']:
        fail('corresponding source archive hash mismatch')
    return manifest


def package_stem(product, version, platform):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 -]*', product):
        fail(f'invalid product name {product!r}')
    return f'{product.replace(" ", "-")}-{version}-{platform}'


def stage_package(root, plugin_id, product, platform, artefacts, version, commit, deps_identity, source_manifest, out):
    root, artefacts, out = Path(root), Path(artefacts), Path(out)
    _check_version(version)
    if platform not in PLATFORMS:
        fail(f'unsupported platform {platform}')
    source = _load_source_manifest(source_manifest)
    if source['commit'] != commit:
        fail(f'source commit {source["commit"]} != build commit {commit}')
    if source['lock_sha256'] != lock_digest(root / 'dependencies.lock.json'):
        fail('dependency lock differs between build and corresponding source')
    if source['dependencies'] != deps_identity:
        fail(f'build dependencies {deps_identity} != source dependencies {source["dependencies"]}')
    arch = PLATFORMS[platform]
    bundle = artefacts / 'VST3' / f'{product}.vst3'
    module = bundle / 'Contents' / arch['vst3_dir'] / (product + arch['vst3_ext'])
    clap = artefacts / 'CLAP' / f'{product}.clap'
    for path, label in ((module, 'VST3 module'), (clap, 'CLAP module')):
        if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
            fail(f'{label} missing, empty or not a regular file: {path}')
    _no_symlinks(bundle)
    stem = package_stem(product, version, platform)
    entries, omitted = [], []
    for dirpath, dirnames, filenames in os.walk(bundle):
        dirnames.sort()
        for name in sorted(filenames):
            path = Path(dirpath) / name
            rel = 'VST3/' + path.relative_to(bundle.parent).as_posix()
            check_private(rel)
            if name.lower().endswith(LINKER_BYPRODUCTS):
                omitted.append(rel)
                continue
            entries.append((rel, path.read_bytes(), path == module))
    entries.append((f'CLAP/{product}.clap', clap.read_bytes(), True))
    for rel in LICENSE_FILES:
        entries.append(('licenses/' + Path(rel).name, (root / rel).read_bytes(), False))
    for path in sorted((root / 'LICENSES').iterdir()):
        entries.append(('licenses/' + path.name, path.read_bytes(), False))
    entries.append(('INSTALL.md', (root / 'docs' / 'INSTALL.md').read_bytes(), False))
    manifest = {
        'schema': 1, 'kind': 'tsunami-plugin-package', 'id': plugin_id, 'product': product, 'platform': platform,
        'version': version, 'commit': commit, 'status': STATUS, 'signed': False,
        'formats': {'vst3': f'VST3/{product}.vst3', 'clap': f'CLAP/{product}.clap'},
        'vst3_module': f'VST3/{product}.vst3/Contents/{arch["vst3_dir"]}/{product}{arch["vst3_ext"]}',
        'vst3_module_sha256': sha256(module), 'clap_sha256': sha256(clap),
        'lock_sha256': source['lock_sha256'], 'dependencies': deps_identity,
        'omitted_build_byproducts': omitted,
        'corresponding_source': {'archive': source['archive'], 'archive_sha256': source['archive_sha256'],
                                 'manifest': Path(source_manifest).name,
                                 'note': 'Complete Corresponding Source for this binary (project + every fetched '
                                         'dependency) is the companion archive with this exact sha256.'},
        'license': 'Tsunami source GPL-3.0-or-later; linked with JUCE under AGPLv3 (see licenses/)',
    }
    entries.append(('MANIFEST.json', (json.dumps(manifest, indent=2) + '\n').encode(), False))
    entries.sort(key=lambda e: e[0])
    sums = ''.join(f'{hashlib.sha256(data).hexdigest()}  {rel}\n' for rel, data, _ in entries)
    entries.append(('SHA256SUMS', sums.encode(), False))
    out.mkdir(parents=True, exist_ok=True)
    zpath = out / f'{stem}.zip'
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for rel, data, executable in entries:
            info = zipfile.ZipInfo(f'{stem}/{rel}', EPOCH)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = ((0o100755 if executable else 0o100644) << 16)
            z.writestr(info, data)
    return zpath


def _safe_name(name):
    if not name or '\\' in name or ':' in name or name.startswith('/') or '\0' in name:
        fail(f'unsafe archive member name {name!r}')
    parts = name.rstrip('/').split('/')
    if any(p in ('', '.', '..') for p in parts):
        fail(f'unsafe archive member name {name!r}')
    return parts


def verify_package(zip_path, source_manifest, dest):
    """Verify a downloaded package against its manifest, SHA256SUMS and companion source; extract safely."""
    source = _load_source_manifest(source_manifest)
    dest = Path(dest)
    with zipfile.ZipFile(zip_path) as z:
        infos = z.infolist()
        names = [i.filename for i in infos]
        if len(set(names)) != len(names):
            fail('duplicate archive members')
        tops = set()
        for info in infos:
            parts = _safe_name(info.filename)
            if stat.S_ISLNK(info.external_attr >> 16):
                fail(f'symlink member refused: {info.filename}')
            tops.add(parts[0])
        if len(tops) != 1:
            fail(f'package must have exactly one top-level directory, got {sorted(tops)}')
        stem = tops.pop()
        files = {i.filename[len(stem) + 1:]: i for i in infos if not i.is_dir()}
        try:
            manifest = json.loads(z.read(files['MANIFEST.json']))
            sums_text = z.read(files['SHA256SUMS']).decode()
        except KeyError as missing:
            fail(f'package lacks {missing}')
        product, platform = manifest['product'], manifest['platform']
        if manifest.get('kind') != 'tsunami-plugin-package' or platform not in PLATFORMS:
            fail('not a tsunami plugin package manifest')
        if stem != package_stem(product, manifest['version'], platform):
            fail(f'package directory {stem!r} does not match manifest identity')
        for rel in files:
            parts = rel.split('/')
            if parts[0] not in PACKAGE_TOP or (parts[0] in {'MANIFEST.json', 'SHA256SUMS', 'INSTALL.md'} and len(parts) != 1):
                fail(f'unexpected package content {rel}')
            if parts[0] == 'VST3' and (len(parts) < 3 or parts[1] != f'{product}.vst3'):
                fail(f'unexpected VST3 content {rel}')
            if parts[0] == 'CLAP' and rel != f'CLAP/{product}.clap':
                fail(f'unexpected CLAP content {rel}')
            if parts[0] == 'licenses' and len(parts) != 2:
                fail(f'unexpected licenses content {rel}')
            check_private(rel)
        listed = {}
        for line in sums_text.splitlines():
            digest, _, rel = line.partition('  ')
            if not re.fullmatch(r'[0-9a-f]{64}', digest) or rel in listed:
                fail(f'malformed SHA256SUMS line {line!r}')
            listed[rel] = digest
        if set(listed) != set(files) - {'SHA256SUMS'}:
            fail(f'SHA256SUMS does not cover exactly the package files: {sorted(set(listed) ^ (set(files) - {"SHA256SUMS"}))}')
        for rel, digest in listed.items():
            if hashlib.sha256(z.read(files[rel])).hexdigest() != digest:
                fail(f'hash mismatch for {rel}')
        for key in ('vst3_module', 'formats'):
            if key not in manifest:
                fail(f'manifest lacks {key}')
        module = manifest['vst3_module']
        if module not in files or files[module].file_size == 0 or listed[module] != manifest['vst3_module_sha256']:
            fail('VST3 module missing, empty or not the manifest module')
        clap = manifest['formats']['clap']
        if clap not in files or files[clap].file_size == 0 or listed[clap] != manifest['clap_sha256']:
            fail('CLAP module missing, empty or not the manifest module')
        for rel in ('licenses/LICENSE', 'licenses/THIRD_PARTY_NOTICES.md', 'licenses/dependencies.lock.json'):
            if rel not in files:
                fail(f'package lacks {rel}')
        if manifest['commit'] != source['commit'] or manifest['lock_sha256'] != source['lock_sha256']:
            fail('package commit/lock does not match corresponding source')
        if manifest['dependencies'] != source['dependencies']:
            fail('package dependency identity does not match corresponding source')
        if manifest['corresponding_source']['archive_sha256'] != source['archive_sha256']:
            fail('package references a different corresponding source archive')
        for rel, info in files.items():
            target = dest / stem / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(z.read(info))
            if (info.external_attr >> 16) & 0o111:
                target.chmod(0o755)
    return dest / stem


# ---------------------------------------------------------------- CLI

def _github_output(**values):
    path = os.environ.get('GITHUB_OUTPUT')
    if path:
        with open(path, 'a', encoding='utf-8') as handle:
            for key, value in values.items():
                handle.write(f'{key}={value}\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('discover')
    p.add_argument('--root', type=Path, default=Path('.'))
    p.add_argument('--out-dir', type=Path, required=True)
    p = sub.add_parser('version')
    p.add_argument('--run-number', required=True)
    p.add_argument('--sha', required=True)
    p = sub.add_parser('stage-source')
    p.add_argument('--root', type=Path, default=Path('.'))
    p.add_argument('--deps', type=Path, required=True)
    p.add_argument('--commit', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--out', type=Path, required=True)
    p = sub.add_parser('stage-package')
    p.add_argument('--root', type=Path, default=Path('.'))
    p.add_argument('--id', required=True)
    p.add_argument('--platform', choices=sorted(PLATFORMS), required=True)
    p.add_argument('--deps', type=Path, required=True)
    p.add_argument('--commit', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--source-manifest', type=Path, required=True)
    p.add_argument('--build', type=Path, required=True, help='CMake build directory')
    p.add_argument('--out', type=Path, required=True)
    p = sub.add_parser('verify')
    p.add_argument('--zip', type=Path, required=True)
    p.add_argument('--source-manifest', type=Path, required=True)
    p.add_argument('--dest', type=Path, required=True)
    a = parser.parse_args(argv)
    try:
        if a.command == 'discover':
            result = discover(a.root)
            a.out_dir.mkdir(parents=True, exist_ok=True)
            (a.out_dir / 'discovery.json').write_text(json.dumps(result, indent=2) + '\n')
            (a.out_dir / 'unavailable.json').write_text(json.dumps(result['unavailable'], indent=2) + '\n')
            _github_output(matrix=json.dumps(result['included'], separators=(',', ':')),
                           source_id=result['included'][0]['id'])
            for p in result['included']:
                print(f'INCLUDED {p["id"]}: {p["product"]} ({p["target"]})')
            for u in result['unavailable']:
                print(f'UNAVAILABLE {u["id"]}: {u["reason"]}')
        elif a.command == 'version':
            version = derive_version(a.run_number, a.sha)
            _github_output(version=version)
            print(version)
        elif a.command == 'stage-source':
            path = stage_source(a.root, a.deps, a.commit, a.version, a.out, discover(a.root)['included'])
            print(path)
        elif a.command == 'stage-package':
            plugin = next((p for p in discover(a.root)['included'] if p['id'] == a.id), None)
            if plugin is None:
                fail(f'{a.id} is not a discovered registered target')
            identity = verify_dependencies(a.root, a.deps)
            print(stage_package(a.root, a.id, plugin['product'], a.platform,
                                a.build / f'{plugin["target"]}_artefacts' / 'Release', a.version, a.commit,
                                identity, a.source_manifest, a.out))
        elif a.command == 'verify':
            print(verify_package(a.zip, a.source_manifest, a.dest))
    except ReleaseError as error:
        print(f'release error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
