"""Builds the Shihab Styler store from this repository (runs on Windows, in CI).

For every mods/<id>.wh.cpp with its store/<id>.json, the mod is compiled for each architecture it runs on, against
the Shihab Styler engine's import library, and the store is written to the output folder:

    catalog.json                      the list the app reads (format 1, see the app's Core/Store/Catalog.cs)
    mods/<id>/<version>.wh.cpp        the source, as built
    mods/<id>/<version>_<arch>.dll    one DLL per architecture folder (32, 64, arm64)

catalog.json is signed afterwards, on the publisher's computer (scripts/Sign-Catalog.ps1): the app uses no
unsigned catalog.

Usage:
    python scripts/build_store.py --compiler <dir with bin/clang++.exe> --engine <unpacked engine package>
                                  --engine-version 2.0.0-shihab.1 --out dist
"""

import argparse
import datetime
import hashlib
import json
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

TARGETS = {
    '32': 'i686-w64-mingw32',
    '64': 'x86_64-w64-mingw32',
    'arm64': 'aarch64-w64-mingw32',
}

CATEGORIES = {'taskbar', 'start', 'explorer', 'desktop', 'windows', 'other'}


def read_metadata(source: str) -> dict:
    """The // ==WindhawkMod== block: single keys as strings, include/exclude/architecture as lists."""
    m = re.search(r'^//[ \t]+==WindhawkMod==[ \t]*$(.+?)^//[ \t]+==/WindhawkMod==[ \t]*$', source, re.M | re.S)
    if not m:
        raise ValueError('no metadata block')
    meta: dict = {'include': [], 'exclude': [], 'architecture': []}
    for line in m.group(1).splitlines():
        line = line.rstrip()
        if not line:
            continue
        lm = re.match(r'^//[ \t]+@(_?[a-zA-Z]+)(:[a-zA-Z-]+)?[ \t]+(.*)$', line)
        if not lm:
            raise ValueError(f'bad metadata line: {line}')
        key, localized, value = lm.group(1), lm.group(2), lm.group(3)
        if key in meta and isinstance(meta[key], list):
            meta[key].append(value)
        elif not localized:  # localized names are the app's business
            meta.setdefault(key, value)
    for key in ('id', 'version'):
        if key not in meta:
            raise ValueError(f'missing @{key}')
    return meta


def arch_folders(architectures: list) -> list:
    """The DLLs a mod needs, whatever the computer: x86-64 also runs on ARM64 through its own arm64 build."""
    if not architectures:
        return ['32', '64', 'arm64']
    need = []
    for a in architectures:
        need += {'x86': ['32'], 'x86-64': ['64', 'arm64'], 'amd64': ['64'], 'arm64': ['arm64']}[a]
    return list(dict.fromkeys(need))


def last_change(path: Path) -> str:
    """When the mod's source last changed in git (the checkout's file times are all the same)."""
    out = subprocess.run(['git', 'log', '-1', '--format=%cI', '--', str(path)], cwd=ROOT, capture_output=True, text=True)
    return out.stdout.strip() or datetime.datetime.now(datetime.timezone.utc).isoformat()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_mod(compiler: Path, engine: Path, meta: dict, source: Path, arch: str, out: Path) -> bool:
    lib = engine / 'engine' / arch / 'shihab-styler-engine.lib'
    options = shlex.split(meta.get('compilerOptions', ''))
    # Same flags as the engine's own tools use for a Windows 10+ mod (see the engine repository's README)
    args = [
        str(compiler / 'bin' / 'clang++.exe'),
        '-std=c++23', '-O2', '-shared', '-s',
        '-DUNICODE', '-D_UNICODE',
        '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008',
        '-D__USE_MINGW_ANSI_STDIO=0',
        '-DWH_MOD', f'-DWH_MOD_ID=L"{meta["id"]}"', f'-DWH_MOD_VERSION=L"{meta["version"]}"',
        str(lib),
        '-x', 'c++', str(source),
        '-include', 'windhawk_api.h',
        '-target', TARGETS[arch],
        '-Wl,--export-all-symbols',
        '-o', str(out),
        *options,
    ]
    print(f'  {arch}: {TARGETS[arch]}', flush=True)
    return subprocess.call(args, cwd=compiler) == 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--engine-version', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()

    if args.out.exists():
        shutil.rmtree(args.out)
    entries = []
    failed = []
    for source in sorted((ROOT / 'mods').glob('*.wh.cpp')):
        mod_id = source.name[:-len('.wh.cpp')]
        info_path = ROOT / 'store' / f'{mod_id}.json'
        if not info_path.exists():
            print(f'{mod_id}: no store/{mod_id}.json, skipped')
            continue
        info = json.loads(info_path.read_text(encoding='utf-8'))
        text = source.read_text(encoding='utf-8')
        meta = read_metadata(text)
        if meta['id'] != mod_id:
            raise SystemExit(f'{source.name}: @id is {meta["id"]}')
        if info.get('category') not in CATEGORIES:
            raise SystemExit(f'{info_path.name}: unknown category {info.get("category")}')
        version = meta['version']
        if not re.fullmatch(r'[0-9A-Za-z.+-]+', version):
            raise SystemExit(f'{source.name}: bad version {version}')
        print(f'{mod_id} {version}', flush=True)

        folder = args.out / 'mods' / mod_id
        folder.mkdir(parents=True)
        built_source = folder / f'{version}.wh.cpp'
        shutil.copyfile(source, built_source)
        binaries = {}
        for arch in arch_folders(meta['architecture']):
            dll = folder / f'{version}_{arch}.dll'
            if not compile_mod(args.compiler, args.engine, meta, built_source, arch, dll):
                failed.append(f'{mod_id} ({arch})')
                continue
            binaries[arch] = {'path': dll.relative_to(args.out).as_posix(), 'sha256': sha256(dll), 'size': dll.stat().st_size}

        entries.append({
            'id': mod_id,
            'version': version,
            'name': info['name'],
            'description': info['description'],
            'author': info.get('author', meta.get('author', '')),
            'category': info['category'],
            'preview': info.get('preview'),
            'basedOn': info.get('basedOn'),
            'license': info.get('license', meta.get('license')),
            'source': {'path': built_source.relative_to(args.out).as_posix(), 'sha256': sha256(built_source),
                       'size': built_source.stat().st_size},
            'binaries': binaries,
            'minEngine': args.engine_version,
            'updated': last_change(source),
        })

    if failed:
        print('Failed to compile: ' + ', '.join(failed))
        return 1
    catalog = {
        'format': 1,
        'updated': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'mods': entries,
    }
    (args.out / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    print(f'{len(entries)} mods in the catalog')
    return 0


if __name__ == '__main__':
    sys.exit(main())
