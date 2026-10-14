"""Exercise the real C++ service and supplied QEMU on disposable owned disks."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / 'build/core/Release/qemu-helper-cli.exe'
RUNTIME = ROOT / '.tools/runtime-11.1.2'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=RUNTIME)
    parser.add_argument('--cli', type=Path, default=CLI)
    parser.add_argument('--legacy-runtime', type=Path, help='Also restore a snapshot made by the previous QEMU 9.2.2 release')
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='helper-integration-') as temp:
        work = Path(temp) / '中文 空格,迁移测试'
        work.mkdir()
        shutil.copytree(options.runtime, work / 'bin')
        shutil.copy2(ROOT / 'guest-tools.iso', work / '中文,工具.iso')
        shutil.copy2(ROOT / 'guest-tools.iso', work / 'guest-tools.iso')
        env = dict(os.environ, PATH=os.environ['SystemRoot'] + '\\System32;' + os.environ['SystemRoot'])
        running = []

        def cli(*args, fail=False):
            p = subprocess.run([str(options.cli.resolve()), str(work), *map(str, args)], capture_output=True,
                               encoding='utf-8', env=env, creationflags=subprocess.CREATE_NO_WINDOW,
                               timeout=75)
            if fail:
                assert p.returncode != 0, (args, p.stdout)
                return p.stderr
            assert p.returncode == 0, (args, p.stdout, p.stderr)
            return json.loads(p.stdout)

        def qmp(vm, cmd, arguments=None):
            r = json.loads((work / 'machines' / vm['id'] / 'runtime.json').read_text(encoding='utf-8'))
            with socket.create_connection(('127.0.0.1', r['port']), timeout=3) as s, s.makefile('rwb', buffering=0) as stream:
                json.loads(stream.readline())
                def execute(command, args):
                    stream.write(json.dumps({'execute': command, 'arguments': args, 'id': 1}).encode() + b'\n')
                    while True:
                        response = json.loads(stream.readline())
                        if 'event' in response:
                            continue
                        assert 'error' not in response, response
                        return response['return']
                execute('qmp_capabilities', {})
                return execute(cmd, arguments or {})

        def stop(vm):
            cli('stop', vm['id'])
            for _ in range(50):
                if cli('status', vm['id'])['state'] == 'stopped':
                    break
                time.sleep(.1)
            assert cli('status', vm['id'])['state'] == 'stopped'
            running.remove(vm)

        def disk_bytes(vm):
            target = work / 'readback.raw'
            subprocess.run([str(work / 'bin/qemu-img.exe'), 'convert', '-f', 'qcow2', '-O', 'raw',
                            str(work / 'machines' / vm['id'] / 'disk.qcow2'), str(target)],
                           env=env, creationflags=subprocess.CREATE_NO_WINDOW, check=True, capture_output=True)
            with target.open('rb') as f:
                return f.read(512)

        try:
            checks = cli('diagnose')
            assert all(c['ok'] for c in checks if c['required'])
            raw = work / 'source.raw'
            raw.write_bytes(bytes([0x19]) * 1024 * 1024)
            vm = cli('import', '中文 XP, "test"', raw)
            assert raw.read_bytes() == bytes([0x19]) * 1024 * 1024
            assert disk_bytes(vm) == bytes([0x19]) * 512
            legacy_snapshot = None
            if options.legacy_runtime:
                old_img = options.legacy_runtime.resolve() / 'qemu-img.exe'
                def legacy_image(*args):
                    result = subprocess.run([str(old_img), *map(str, args)], capture_output=True,
                        env=env, creationflags=subprocess.CREATE_NO_WINDOW, timeout=30, check=True)
                    return result.stdout.decode('utf-8', errors='replace')
                assert '9.2.2' in legacy_image('--version'), 'Not the prior release runtime'
                old_disk = work / 'legacy-9.2.2.qcow2'
                legacy_image('convert', '-f', 'raw', '-O', 'qcow2', raw, old_disk)
                legacy_image('snapshot', '-c', 'legacy-9.2.2-baseline', old_disk)
                shutil.copy2(old_disk, work / 'machines' / vm['id'] / 'disk.qcow2')
                legacy_snapshot = next(s for s in cli('snapshots', vm['id']) if s['name'] == 'legacy-9.2.2-baseline')
                (work / 'workspace.json').write_text(json.dumps({'schema': 1, 'settings': {
                    'qemu': 'bin/qemu-system-i386w.exe', 'toolsIso': 'bin/qemu-3dfx-tools.iso'}}), encoding='utf-8')
                migrated = cli('diagnose')
                assert all(c['ok'] for c in migrated if c['required'] or c['title'] == '客体工具 ISO')
                cli('iso', vm['id'], 'bin/qemu-3dfx-tools.iso')
                assert cli('list')['machines'][0]['iso'] == 'guest-tools.iso'
            vm2 = cli('create', '第二台 XP', 1)
            cli('mouse', vm2['id'], 'ps2')
            assert vm['mac'] != vm2['mac']
            cli('create', 'bad size', 0, fail=True)
            cli('snapshot-create', vm['id'], '初始 状态')
            snap = next(s for s in cli('snapshots', vm['id']) if s['name'] == '初始 状态')
            cli('snapshot-create', vm['id'], '初始 状态', fail=True)
            cli('snapshot-create', vm['id'], '123', fail=True)
            cli('iso', vm['id'], '中文,工具.iso')
            profile = work / 'machines' / vm['id'] / 'machine.json'
            assert json.loads(profile.read_text(encoding='utf-8'))['machine']['iso'] == '中文,工具.iso'
            running.append(vm); r1 = cli('start', vm['id'], '--headless')
            running.append(vm2); r2 = cli('start', vm2['id'], '--headless')
            assert r1['port'] != r2['port']
            assert '11.1.2' in checks[0]['detail'], 'Tests are using a stale QEMU runtime'
            assert (work / 'machines' / vm['id'] / 'OpenGLid.ini').read_bytes() == (work / 'bin/OpenGLid.ini').read_bytes()
            def devices(machine):
                return {d['name'] for d in qmp(machine, 'qom-list', {'path': '/machine/peripheral'})}

            def check_mouse(machine, mode):
                current = devices(machine)
                assert 'usb' in current, 'USB controller must remain available in PS/2 mode'
                assert ('tablet' in current) == (mode == 'usb'), (mode, current)
                saved = json.loads((work / 'machines' / machine['id'] / 'machine.json').read_text(encoding='utf-8'))
                assert saved['machine']['usb'] == (mode == 'usb')

            check_mouse(vm, 'usb'); check_mouse(vm2, 'ps2')
            # Include a VM originally booted with PS/2, repeated requests, and
            # paused guests; inspect real QOM devices rather than just settings.
            for target in (vm, vm2):
                for mode in ('usb', 'usb', 'ps2', 'ps2', 'usb', 'ps2'):
                    cli('mouse', target['id'], mode)
                    check_mouse(target, mode)
            assert any('PS/2' in mouse['name'] for mouse in qmp(vm, 'query-mice'))
            cli('mouse', vm['id'], 'usb')
            check_mouse(vm2, 'ps2')
            assert cli('status', vm['id'])['state'] == 'running'
            cli('start', vm['id'], '--headless', fail=True)
            cli('snapshots', vm['id'], fail=True)
            cli('snapshot-create', vm['id'], 'unsafe', fail=True)
            cli('snapshot-restore', vm['id'], snap['id'], fail=True)
            cli('snapshot-delete', vm['id'], snap['id'], fail=True)
            cli('export', vm['id'], work / 'unsafe.qcow2', fail=True)
            qmp(vm, 'stop')
            assert cli('status', vm['id'])['state'] == 'running', 'Paused is not powered off'
            cli('snapshot-create', vm['id'], 'paused-is-not-off', fail=True)
            cli('mouse', vm['id'], 'ps2'); check_mouse(vm, 'ps2')
            cli('mouse', vm['id'], 'usb'); check_mouse(vm, 'usb')
            qmp(vm, 'cont')
            cli('iso', vm['id'])
            cli('iso', vm['id'], '中文,工具.iso')
            blocks = qmp(vm, 'query-block')
            assert any('media/iso-' in b.get('inserted', {}).get('file', '') for b in blocks)
            # Write a real sector through QEMU itself; verify disk content after restore.
            result = qmp(vm, 'human-monitor-command', {'command-line': 'qemu-io osdisk "write -P 0x5a 0 512"'})
            assert 'failed' not in result.lower() and 'error' not in result.lower(), result
            stop(vm); stop(vm2)
            assert disk_bytes(vm) == bytes([0x5a]) * 512
            restored = cli('snapshot-restore', vm['id'], (legacy_snapshot or snap)['id'])
            assert disk_bytes(vm) == bytes([0x19]) * 512
            safety = next(s for s in cli('snapshots', vm['id']) if s['name'] == restored['safetySnapshot'])
            cli('snapshot-restore', vm['id'], safety['id'])
            assert disk_bytes(vm) == bytes([0x5a]) * 512
            cli('snapshot-delete', vm['id'], snap['id'])
            cli('snapshot-delete', vm['id'], 'missing', fail=True)
            cli('export', vm['id'], work / 'backup.qcow2')
            cli('export', vm['id'], work / 'backup.qcow2', fail=True)
            # A corrupt runtime record must block image mutation, never assume shutdown.
            runtime_file = work / 'machines' / vm['id'] / 'runtime.json'
            prior = runtime_file.read_bytes(); runtime_file.write_text('{invalid')
            assert cli('status', vm['id'])['state'] == 'unknown'
            cli('snapshot-create', vm['id'], 'unsafe-record', fail=True)
            cli('mouse', vm['id'], 'ps2', fail=True)
            runtime_file.write_bytes(prior)
            old = work; moved = Path(temp) / '已搬迁 workspace,2'
            # Windows can briefly retain executable mappings after process exit.
            for attempt in range(50):
                try:
                    old.rename(moved)
                    break
                except PermissionError:
                    if attempt == 49:
                        raise
                    time.sleep(.1)
            work = moved
            assert len(cli('list')['machines']) == 2
            assert len(cli('snapshots', vm['id'])) >= 2
            # Relocation also changes exe identity; exited process is still recognized.
            running.append(vm); cli('start', vm['id'], '--headless'); stop(vm)
            cli('remove', vm2['id'])
            assert len(cli('list')['machines']) == 1
            assert (work / 'machines' / vm2['id'] / 'disk.qcow2').exists()
            print('PASS: real QEMU create/import, UTF-8 + spaces + commas, two VMs, duplicate-start guard,')
            print('      live USB/PS2 switching (including PS2 boot/paused/repeated), QMP ISO swap/eject,')
            print('      paused/running snapshot guards, sector restore + safety snapshot,')
            print('      delete/export/no-overwrite, corrupt-state guard, relocation and non-destructive removal')
            if legacy_snapshot: print('      QEMU 9.2.2 image/snapshot sector restore and legacy package-path migration')
        finally:
            for vm in running[:]:
                try:
                    stop(vm)
                except Exception as e:
                    print('Test VM cleanup failed:', e)


if __name__ == '__main__':
    main()
