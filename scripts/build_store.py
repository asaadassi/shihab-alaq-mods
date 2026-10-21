"""Builds the Shihab Alaq store from this repository (runs on Windows, in CI).

For every mods/<id>.wh.cpp with its store/<id>.json, the mod is compiled for each architecture it runs on, against
the Shihab Alaq engine's import library, and the store is written to the output folder:

    catalog.json                      the list the app reads (format 1, see the app's Core/Store/Catalog.cs), with
                                      the groups of store/groups.json the app shows mods in
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
import os
import re
import shlex
import shutil
import struct
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


def read_groups() -> list:
    """store/groups.json: the features the app shows, each holding the mods whose store entry names it."""
    groups = json.loads((ROOT / 'store' / 'groups.json').read_text(encoding='utf-8'))
    seen = set()
    for g in groups:
        if not re.fullmatch(r'[a-z0-9-]+', g.get('id', '')) or g['id'] in seen:
            raise SystemExit(f'groups.json: bad or repeated id {g.get("id")}')
        if g.get('category') not in CATEGORIES:
            raise SystemExit(f'groups.json: {g["id"]}: unknown category {g.get("category")}')
        for field in ('name', 'tagline'):
            if not g.get(field, {}).get('ar') or not g.get(field, {}).get('en'):
                raise SystemExit(f'groups.json: {g["id"]}: needs an Arabic and an English {field}')
        if not re.fullmatch(r'[0-9A-F]{4}', g.get('icon', '')):
            raise SystemExit(f'groups.json: {g["id"]}: icon must be a Segoe Fluent Icons code point such as E790')
        seen.add(g['id'])
    return groups


def last_change(path: Path) -> str:
    """When the mod's source last changed in git (the checkout's file times are all the same)."""
    out = subprocess.run(['git', 'log', '-1', '--format=%cI', '--', str(path)], cwd=ROOT, capture_output=True, text=True)
    return out.stdout.strip() or datetime.datetime.now(datetime.timezone.utc).isoformat()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_mod(compiler: Path, engine: Path, meta: dict, source: Path, arch: str, out: Path) -> bool:
    lib = engine / 'engine' / arch / 'shihab-alaq-engine.lib'
    options = shlex.split(meta.get('compilerOptions', ''))
    # Same flags as the engine's own tools use for a Windows 10+ mod (see the engine repository's README)
    args = [
        str(compiler / 'bin' / 'clang++.exe'),
        '-std=c++23', '-O2', '-shared', '-s',
        # The C++ runtime inside each DLL: the engine ships no libc++.dll or libunwind.dll next to the mods, and
        # Windows refuses to load a mod that needs them (error 126)
        '-static-libstdc++', '-static',
        '-DUNICODE', '-D_UNICODE',
        '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008',
        '-D__USE_MINGW_ANSI_STDIO=0',
        '-DWH_MOD', f'-DWH_MOD_ID=L"{meta["id"]}"', f'-DWH_MOD_VERSION=L"{meta["version"]}"',
        str(lib),
        '-x', 'c++', str(source),
        '-include', 'windhawk_api.h',
        '-target', TARGETS[arch],
        '-Wl,--export-all-symbols',
        # Same source, same bytes: no build time in the DLL, so a rebuild doesn't change a published file's SHA-256
        '-Wl,--no-insert-timestamp',
        '-o', str(out),
        *options,
    ]
    print(f'  {arch}: {TARGETS[arch]}', flush=True)
    result = subprocess.run(args, cwd=compiler, capture_output=True, text=True, errors='replace')
    output = (result.stdout + result.stderr).strip()
    if output:
        print(output, flush=True)
    if result.returncode != 0:
        # A GitHub annotation, so the reason shows on the run's page without opening the log
        lines = [l for l in output.splitlines() if 'error' in l.lower()] or output.splitlines()
        annotate(f'{meta["id"]} ({arch})', '\n'.join(lines[:15]) or f'exit code {result.returncode}')
        return False
    # A mod that needs a DLL the computer doesn't have never loads: refuse it here instead
    if missing := foreign_imports(out):
        annotate(f'{meta["id"]} ({arch})', 'imports DLLs that are not part of Windows: ' + ', '.join(missing))
        return False
    return True


# DLLs a mod may import besides Windows' own
ALLOWED_NON_SYSTEM = {'shihab-alaq-engine.dll'}


def dll_imports(path: Path) -> list:
    """The DLL names a PE file imports, read from its import directory."""
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    sections = struct.unpack_from('<H', data, pe + 6)[0]
    optional = pe + 24
    pe32plus = struct.unpack_from('<H', data, optional)[0] == 0x20B
    import_rva = struct.unpack_from('<I', data, optional + (112 if pe32plus else 96) + 8)[0]
    table = optional + struct.unpack_from('<H', data, pe + 20)[0]

    def offset(rva: int) -> int:
        for i in range(sections):
            size, va, _, raw = struct.unpack_from('<IIII', data, table + i * 40 + 8)
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError(f'{path.name}: RVA {rva:#x} is outside its sections')

    names = []
    at = offset(import_rva) if import_rva else None
    while at is not None:
        name_rva = struct.unpack_from('<I', data, at + 12)[0]
        if not name_rva:
            break
        start = offset(name_rva)
        names.append(data[start:data.index(b'\0', start)].decode('ascii'))
        at += 20
    return names


def foreign_imports(dll: Path) -> list:
    """Imports Windows can't find for a mod: anything not in System32, not an API set and not the engine."""
    system32 = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32'
    return [n for n in dll_imports(dll)
            if n.lower() not in ALLOWED_NON_SYSTEM and not n.lower().startswith(('api-ms-', 'ext-ms-'))
            and not (system32 / n).exists()]


def annotate(title: str, message: str) -> None:
    message = message.replace('%', '%25').replace('\r', '').replace('\n', '%0A')
    print(f'::error title={title}::{message}', flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--engine-version', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    # The compiler runs in its own folder: every path it gets is absolute
    args.out, args.compiler, args.engine = args.out.resolve(), args.compiler.resolve(), args.engine.resolve()

    if args.out.exists():
        shutil.rmtree(args.out)
    groups = read_groups()
    group_ids = {g['id'] for g in groups}
    entries = []
    failed = []
    if not (args.compiler / 'bin' / 'clang++.exe').exists():
        annotate('toolchain', f'no clang++.exe under {args.compiler}')
        return 1
    for source in sorted((ROOT / 'mods').glob('*.wh.cpp')):
        mod_id = source.name[:-len('.wh.cpp')]
        info_path = ROOT / 'store' / f'{mod_id}.json'
        if not info_path.exists():
            print(f'{mod_id}: no store/{mod_id}.json, skipped')
            continue
        info = json.loads(info_path.read_text(encoding='utf-8'))
        text = source.read_text(encoding='utf-8')
        try:
            meta = read_metadata(text)
        except ValueError as e:
            annotate(mod_id, str(e))
            failed.append(mod_id)
            continue
        if meta['id'] != mod_id:
            raise SystemExit(f'{source.name}: @id is {meta["id"]}')
        if info.get('category') not in CATEGORIES:
            raise SystemExit(f'{info_path.name}: unknown category {info.get("category")}')
        if info.get('group') not in group_ids:
            raise SystemExit(f'{info_path.name}: unknown group {info.get("group")} (store/groups.json)')
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
            # Plain words for the feature page: one line, and what it does and lets you choose
            'summary': info.get('summary'),
            'highlights': info.get('highlights'),
            'author': info.get('author', meta.get('author', '')),
            'category': info['category'],
            'preview': info.get('preview'),
            'basedOn': info.get('basedOn'),
            'license': info.get('license', meta.get('license')),
            # Mods that change the same thing; the app warns before both run together
            'conflicts': list(info.get('conflicts', [])),
            'group': info['group'],
            'order': int(info.get('order', 0)),
            'source': {'path': built_source.relative_to(args.out).as_posix(), 'sha256': sha256(built_source),
                       'size': built_source.stat().st_size},
            'binaries': binaries,
            'minEngine': args.engine_version,
            'updated': last_change(source),
        })

    # The mods of an exclusive group are choices of one feature: each conflicts with the others
    for g in groups:
        if g.get('exclusive'):
            members = [e for e in entries if e['group'] == g['id']]
            for e in members:
                e['conflicts'] += [o['id'] for o in members if o is not e and o['id'] not in e['conflicts']]
    ids = {e['id'] for e in entries}
    for e in entries:
        unknown = [c for c in e['conflicts'] if c not in ids or c == e['id']]
        if unknown:
            raise SystemExit(f'store/{e["id"]}.json: bad conflicts {unknown}')

    if failed:
        print('Failed to compile: ' + ', '.join(failed))
        return 1
    catalog = {
        'format': 1,
        'updated': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'groups': [g for g in groups if any(e['group'] == g['id'] for e in entries)],
        'mods': entries,
    }
    (args.out / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    print(f'{len(entries)} mods in the catalog')
    return 0


if __name__ == '__main__':
    sys.exit(main())
