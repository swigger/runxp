"""Boot a disposable XP preset with only the staged runtime and system PATH."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--iso', type=Path)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    runtime = args.runtime.resolve()
    env = dict(os.environ, PATH=os.environ['SystemRoot'] + '\\System32;' + os.environ['SystemRoot'])
    # SDL's dummy driver exercises backend initialization without opening a window.
    env['SDL_VIDEODRIVER'] = 'dummy'
    env['SDL_AUDIODRIVER'] = 'dummy'
    with tempfile.TemporaryDirectory(prefix='qemu-helper-smoke-') as tmp:
        work = Path(tmp)
        image = work / 'test.qcow2'
        def img(*argv):
            return subprocess.run([str(runtime / 'qemu-img.exe'), *argv, str(image)],
                                  cwd=work, env=env, capture_output=True, text=True,
                                  creationflags=subprocess.CREATE_NO_WINDOW, check=True).stdout
        subprocess.run([str(runtime / 'qemu-img.exe'), 'create', '-f', 'qcow2', str(image), '64M'],
                       cwd=work, env=env, check=True, capture_output=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        argv = [str(runtime / 'qemu-system-x86_64w.exe'), '-L', str(runtime / 'share'),
                '-machine', 'pc-i440fx-9.2,hpet=off', '-cpu', 'Nehalem,-lm', '-smp', '2',
                '-m', '256', '-accel', 'tcg,thread=multi', '-vga', 'cirrus',
                '-display', 'sdl', '-monitor', 'none', '-serial', 'none',
                '-drive', f'file={image.as_posix()},format=qcow2,if=ide,index=0',
                '-device', 'piix3-usb-uhci,id=usb,addr=0x3',
                '-device', 'usb-tablet,bus=usb.0',
                '-audiodev', 'sdl,id=snd0', '-device', 'AC97,audiodev=snd0,addr=0x5',
                '-netdev', 'user,id=net0', '-device', 'pcnet,netdev=net0,addr=0x4,romfile=',
                '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off', '-S']
        if args.iso:
            argv += ['-cdrom', str(args.iso.resolve()), '-boot', 'order=c,once=d']
        else:
            argv += ['-drive', 'if=ide,index=2,media=cdrom,id=cdrom']
        with (work / 'stderr.log').open('w+b') as log:
            proc = subprocess.Popen(argv, cwd=work, env=env, stdout=log, stderr=log,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                channel = None
                for _ in range(80):
                    if proc.poll() is not None:
                        raise RuntimeError('QEMU exited: ' + (work / 'stderr.log').read_text(errors='replace'))
                    try:
                        channel = socket.create_connection(('127.0.0.1', port), timeout=1)
                        break
                    except OSError:
                        time.sleep(.1)
                if channel is None:
                    raise RuntimeError('QMP startup timed out')
                with channel, channel.makefile('rwb', buffering=0) as stream:
                    greeting = json.loads(stream.readline())
                    def execute(command, arguments=None):
                        req = {'execute': command}
                        if arguments is not None:
                            req['arguments'] = arguments
                        stream.write(json.dumps(req).encode() + b'\n')
                        while True:
                            response = json.loads(stream.readline())
                            if 'error' in response:
                                raise RuntimeError(response['error'])
                            if 'return' in response:
                                return response['return']
                    execute('qmp_capabilities')
                    execute('cont')
                    time.sleep(4)
                    status = execute('query-status')
                    if args.output:
                        args.output.parent.mkdir(parents=True, exist_ok=True)
                        execute('screendump', {'filename': str(args.output.resolve()), 'format': 'png'})
                    execute('quit')
                    proc.wait(timeout=8)
                    print(json.dumps({'version': greeting['QMP']['version'], 'status': status,
                                      'exitCode': proc.returncode}, indent=2))
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=8)
        img('snapshot', '-c', 'smoke-test')
        snapshots = json.loads(img('info', '--output=json')).get('snapshots', [])
        assert any(s['name'] == 'smoke-test' for s in snapshots)
        img('snapshot', '-a', 'smoke-test')
        img('snapshot', '-d', 'smoke-test')
        assert not json.loads(img('info', '--output=json')).get('snapshots', [])
        print('PASS: minimal runtime, SDL/AC97/PCnet/USB startup and offline snapshot CRUD')


if __name__ == '__main__':
    main()
