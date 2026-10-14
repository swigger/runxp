"""Audit PE imports and stage the small runtime used by the XP preset.

No third-party Python modules. Does not delete or modify the source runtime.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess


def pe_machine(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0': raise ValueError(f'Not a PE image: {path}')
    return struct.unpack_from('<H', data, pe + 4)[0]


def imports(path):
    data = path.read_bytes()
    u16 = lambda p: struct.unpack_from('<H', data, p)[0]
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    pe = u32(0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'Not a PE image: {path}')
    optional = pe + 24
    is64 = u16(optional) == 0x20b
    directories = optional + (112 if is64 else 96)
    section_start = optional + u16(pe + 20)
    sections = []
    for i in range(u16(pe + 6)):
        p = section_start + i * 40
        sections.append((u32(p + 12), max(u32(p + 8), u32(p + 16)), u32(p + 20)))

    def offset(rva):
        for base, size, raw in sections:
            if base <= rva < base + size:
                return raw + rva - base
        raise ValueError(f'Invalid RVA in {path}: {rva}')

    def name(rva):
        p = offset(rva)
        return data[p:data.index(b'\0', p)].decode('ascii')

    result = set()
    rva = u32(directories + 8)
    if rva:
        p = offset(rva)
        while any(data[p:p + 20]):
            result.add(name(u32(p + 12)))
            p += 20
    # Delay-load import descriptors.
    if u32(optional + (108 if is64 else 92)) > 13:
        rva = u32(directories + 13 * 8)
        if rva:
            p = offset(rva)
            while any(data[p:p + 32]):
                if u32(p) & 1:
                    result.add(name(u32(p + 4)))
                p += 32
    return sorted(result, key=str.lower)


def audit(source):
    system = Path(os.environ['SystemRoot']) / 'System32'
    local = {p.name.lower(): p for p in source.iterdir() if p.is_file()}
    executable = 'qemu-system-x86_64w.exe'
    queue = [executable, 'qemu-img.exe', 'glide2x.dll', 'glide3x.dll']
    graph, files, missing, system_imports = {}, set(), set(), set()
    while queue:
        item = queue.pop(0).lower()
        if item in files:
            continue
        if item not in local:
            missing.add(item)
            continue
        files.add(item)
        if pe_machine(local[item]) != 0x8664:
            missing.add('x64 binary required: ' + local[item].name)
        dependencies = imports(local[item])
        graph[local[item].name] = dependencies
        for dep in dependencies:
            key = dep.lower()
            if key in local:
                queue.append(key)
            elif key.startswith(('api-ms-win-', 'ext-ms-win-')) or (system / dep).exists():
                system_imports.add(dep)
            else:
                missing.add(dep)
    # PC/i440FX + Cirrus; the network ROM is explicitly disabled by the preset.
    firmware = ['bios-256k.bin', 'vgabios-cirrus.bin', 'kvmvapic.bin']
    for name in firmware:
        if not (source / 'share' / name).is_file():
            missing.add('share/' + name)
    included = [local[f] for f in sorted(files)]
    included += [source / 'share' / f for f in firmware if (source / 'share' / f).exists()]
    if (source / 'OpenGLid.ini').is_file(): included.append(source / 'OpenGLid.ini')
    else: missing.add('OpenGLid.ini')
    versions = {}
    if not missing:
        env = dict(os.environ, PATH=str(system) + ';' + os.environ['SystemRoot'])
        for name in (executable, 'qemu-img.exe'):
            result = subprocess.run([str(source / name), '--version'], cwd=source, env=env,
                capture_output=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
            versions[name] = (result.stdout + result.stderr).decode('utf-8', errors='replace').strip()
            if result.returncode: missing.add(f'{name} --version failed ({result.returncode})')
    return {
        'executable': executable,
        'hostArchitecture': 'x64',
        'versions': versions,
        'preset': 'pc-i440fx-9.2 / Cirrus / AC97 / PCnet without PXE / SDL / TCG',
        'files': [{'path': p.relative_to(source).as_posix(), 'size': p.stat().st_size,
                   'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in included],
        'missing': sorted(missing),
        'imports': graph,
        'systemImports': sorted(system_imports),
        'excluded': sorted(p.name for p in source.iterdir()
                           if p.is_file() and p not in included),
        'notes': ['glide2x.dll and glide3x.dll are dynamically loaded; explicitly included.',
                  'OpenGLid.ini is included; the helper seeds a copy in each VM working directory.',
                  'Graphics drivers and Windows system DLLs are supplied by the host OS.',
                  'This firmware list is only for the fixed XP preset; not other machines or VGA types.',
                  'PE analysis does not replace a boot or in-guest 3D test.'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'runtime-binary')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--stage', type=Path, help='Create a NEW minimal runtime directory')
    args = parser.parse_args()
    report = audit(args.source.resolve())
    if args.stage:
        if report['missing']:
            raise SystemExit('Missing: ' + ', '.join(report['missing']))
        args.stage.mkdir(parents=True, exist_ok=False)
        for item in report['files']:
            target = args.stage / item['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(args.source / item['path'], target)
    result = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result + '\n', encoding='utf-8')
    print(result)
    raise SystemExit(1 if report['missing'] else 0)


if __name__ == '__main__':
    main()
