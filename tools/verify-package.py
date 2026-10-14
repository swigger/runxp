"""Publisher-side checks for the portable layout, EXE icons, and ZIP contents."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import zipfile


def icon_count(path):
    # Read resources as data; do not execute the image or run its initializers.
    win = ctypes.WinDLL('kernel32', use_last_error=True)
    win.LoadLibraryExW.argtypes = (ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32)
    win.LoadLibraryExW.restype = ctypes.c_void_p
    win.FindResourceW.argtypes = (ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
    win.FindResourceW.restype = ctypes.c_void_p
    win.LoadResource.argtypes = (ctypes.c_void_p, ctypes.c_void_p)
    win.LoadResource.restype = ctypes.c_void_p
    win.LockResource.argtypes = (ctypes.c_void_p,)
    win.LockResource.restype = ctypes.c_void_p
    win.FreeLibrary.argtypes = (ctypes.c_void_p,)
    module = win.LoadLibraryExW(str(path), None, 0x22)
    assert module, f'Cannot read PE resources: {path}'
    try:
        group = win.FindResourceW(module, 101, 14)
        assert group, f'Missing app icon: {path}'
        data = win.LockResource(win.LoadResource(module, group))
        assert data
        reserved, kind, count = struct.unpack('<HHH', ctypes.string_at(data, 6))
        assert reserved == 0 and kind == 1
        return count
    finally:
        win.FreeLibrary(module)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    expected = {'qemu-3dfx-winhelper.exe', 'bin', 'machines', '使用说明.txt'}
    if (root / 'guest-tools.iso').exists(): expected.add('guest-tools.iso')
    assert {p.name for p in root.iterdir()} == expected, 'Unexpected root contents'
    launcher = root / 'qemu-3dfx-winhelper.exe'
    gui = root / 'bin/winui/qemu-3dfx-winhelper-ui.exe'
    spec = importlib.util.spec_from_file_location('runtime_audit', Path(__file__).with_name('audit-runtime.py'))
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    assert set(map(str.lower, audit.imports(launcher))) <= {'kernel32.dll', 'shell32.dll', 'user32.dll'}, 'Launcher needs a non-system runtime'
    assert icon_count(launcher) == 9 and icon_count(gui) == 9, 'Missing multi-resolution icons'
    for name in ('Microsoft.UI.Xaml.dll', 'Microsoft.WindowsAppRuntime.dll', 'VCRUNTIME140.dll', 'MSVCP140.dll'):
        assert (gui.parent / name).is_file(), f'Missing app-local runtime: {name}'
    manifest = json.loads((root / 'bin/runtime-manifest.json').read_text(encoding='utf-8'))
    assert manifest['executable'] == 'qemu-system-x86_64w.exe' and manifest['hostArchitecture'] == 'x64'
    assert not (root / 'bin/qemu-system-i386w.exe').exists(), 'Old QEMU bundled with new release'
    for entry in manifest['files']:
        assert hashlib.sha256((root / 'bin' / entry['path']).read_bytes()).hexdigest() == entry['sha256'], entry['path']
    assert not any(root.rglob('*.pdb')) and not any(root.rglob('*.ilk')), 'Development files included'
    if args.archive:
        with zipfile.ZipFile(args.archive) as archive:
            assert archive.testzip() is None, 'Corrupt ZIP entry'
            assert {n.split('/')[0] for n in archive.namelist()} == expected
            for entry in archive.infolist():
                if not entry.is_dir():
                    assert archive.read(entry) == (root / entry.filename).read_bytes(), entry.filename
    print('PASS: clean package root, system-only launcher, two 9-size EXE icons, app-local WinUI/CRT, QEMU hashes and ZIP contents')


if __name__ == '__main__':
    main()
