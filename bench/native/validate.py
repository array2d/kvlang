#!/usr/bin/env python3
"""Validate SHM state visibility, rejection, and recovery across worker processes."""
import argparse
from contextlib import closing
import ctypes as c
import json
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile
import time

from compile import KV, STRATEGIES
from run import compile_worker, run

ROOT = '/vthread/native/'


class Store(KV):
    def view(self, member, element=c.c_uint64):
        key = ROOT + '‥' + member
        _, raw = self.get(key)
        p = c.c_void_p()
        self.lib.kvspaceWriteInPlace.argtypes = [c.c_void_p, c.c_char_p, c.c_int, c.c_int32,
                                               c.POINTER(c.c_void_p), c.c_void_p, c.c_uint32]
        err = c.create_string_buffer(256)
        rc = self.lib.kvspaceWriteInPlace(self.handle, key.encode(), 0, len(raw), c.byref(p), err, len(err))
        if rc or not p.value:
            raise RuntimeError('cannot map ' + key)
        return (element * (len(raw) // c.sizeof(element))).from_address(p.value)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['kvlang', 'frontend', 'backend', 'include', 'binaries']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--cpu', type=int, default=24)
    ap.add_argument('--sigkill-restarts', type=int, default=24)
    ap.add_argument('--strategy', choices=STRATEGIES)
    args = ap.parse_args()
    if args.sigkill_restarts < 0:
        ap.error('--sigkill-restarts must be nonnegative')
    os.environ.update(KVSPACE_BACKEND_PATH=str(args.backend), LOG_LEVEL='warn')
    env, checks = dict(os.environ), 0
    with tempfile.TemporaryDirectory(prefix='native-validation-') as td:
        dsn = 'shm://' + td + '/store'

        def init(case, n):
            run([args.binaries / case, dsn, 'init', n], env, args.cpu)

        def finish(case, expected, exit_code=0, budget=None):
            nonlocal checks
            command = [args.binaries / case, dsn, 'run']
            if budget is not None:
                command.append(budget)
            result, _ = run(command, env, args.cpu, exit_code)
            if exit_code == 0:
                match = re.search(r'result = (-?\d+)', result.stdout)
                if not match or int(match[1]) != expected:
                    raise AssertionError(result.stdout)
            checks += 1

        for n in [0, 1, 2, 31, 2000]:
            init('iops', n)
            finish('iops', n)
        for n, expected in [(-3, -3), (0, 0), (1, 1), (2, 1), (5, 5), (10, 55), (18, 2584)]:
            init('fib', n)
            finish('fib', expected)
        for n, expected in enumerate([1, 1, 0, 0, 2, 10, 4, 40, 92]):
            init('nqueens', n)
            finish('nqueens', expected)
        for budget in range(1, 33):
            init('fib', 10)
            finish('fib', None, 75, budget)
            finish('fib', 55)
        init('fib', 260)
        finish('fib', None, 4)
        for fault in ['argument', 'constant', 'status', 'pc', 'reference', 'tag', 'path', 'journal_path', 'program', 'journal']:
            init('iops', 6)
            finish('iops', None, 75, 1)
            with closing(Store(args.frontend, dsn)) as store:
                frames = store.view('frames')
                if fault == 'argument':
                    frames[3] = 4
                elif fault == 'constant':
                    schema = json.loads((args.binaries / 'iops.schema.json').read_text())
                    index = schema['constants'].index([1, 0])
                    store.view('constants')[index * 2 + 1] = 5
                elif fault == 'status':
                    store.view('status', c.c_uint8)[:] = b'paused!'
                elif fault == 'pc':
                    store.view('pc', c.c_uint8)[-7:-3] = b'0009'
                elif fault == 'reference':
                    frames[2], frames[3] = 3, (1 << 64) - 1
                elif fault == 'tag':
                    frames[2] = 9
                elif fault == 'path':
                    store.view('pc', c.c_uint8)[0] = ord('x')
                elif fault == 'journal_path':
                    store.view('pc', c.c_uint8)[-10] = ord('x')
                    store.view('journal')[:3] = [1, 1, 1]
                elif fault == 'program':
                    store.view('program', c.c_uint8)[0] ^= 1
                elif fault == 'journal':
                    store.view('journal')[0] = (1 << 64) - 1
            if fault in ['argument', 'constant']:
                finish('iops', 4 if fault == 'argument' else 11)
            elif fault == 'status':
                finish('iops', None, 1)
                with closing(Store(args.frontend, dsn)) as store:
                    store.view('status', c.c_uint8)[:] = b'running'
                finish('iops', 6)
            else:
                finish('iops', None, 4)
        init('iops', 6)
        for _ in range(16):
            finish('iops', None, 75, 1)
            with closing(Store(args.frontend, dsn)) as store:
                pc = bytes(store.view('pc', c.c_uint8)).decode()
            if pc.endswith('/[0006,0]'):
                break
        else:
            raise AssertionError('missing increment PC')
        with closing(Store(args.frontend, dsn)) as store:
            frames, journal = store.view('frames'), store.view('journal')
            # Simulate death after partial application of a prepared increment.
            journal[1:7] = [1, 7, 5, 1, 4, 1]
            frames[4], frames[5] = 1, 999
            store.view('pc', c.c_uint8)[-7] = ord('x')
            journal[0] = 3
        finish('iops', 6)
        rng = random.Random(20261008)
        for _ in range(args.sigkill_restarts):
            init('iops', 2000000)
            proc = subprocess.Popen(['taskset', '-c', str(args.cpu), str(args.binaries / 'iops'), dsn, 'run'], env=env, stdout=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 5
                while True:
                    stat = Path(f'/proc/{proc.pid}/stat').read_text().split()
                    if int(stat[13]) + int(stat[14]) > 0:
                        break
                    if proc.poll() is not None or time.monotonic() > deadline:
                        raise AssertionError('worker did not execute')
                    time.sleep(0.0001)
                time.sleep(rng.uniform(0.0001, 0.005))
                proc.kill()
                if proc.wait() != -9:
                    raise AssertionError('worker completed before kill')
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                proc.stdout.close()
            finish('iops', 2000000)
        init('iops', 10000000)
        proc = subprocess.Popen([str(args.binaries / 'iops'), dsn, 'run', '10000000'], env=env, stdout=subprocess.PIPE)
        try:
            time.sleep(0.02)
            finish('iops', None, 6)
            run([args.binaries / 'iops', dsn, 'init', 1], env, args.cpu, 6)
        finally:
            try:
                if proc.wait(timeout=20) != 75:
                    raise AssertionError('lock holder did not stop at instruction boundary')
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                proc.stdout.close()
        source = Path(td) / 'types.kv'
        source.write_text('''rwfunc negative(n:int64) -> (r:int64) {
    n >> 1 -> r
}
rwfunc inc(n:int64) -> (r:int64) {
    n + 1 -> r
}
rwfunc literal(n:int64) -> (r:int64) {
    inc(1) -> r
}
rwfunc alias(n:int64) -> (r:int64) {
    inc(n) -> r
}
rwfunc boolean(n:int64) -> (r:int64) {
    if (true) {
        n -> r
    } else {
        0 -> r
    }
}
''')
        for name, n, expected in [('negative', -4, -2), ('literal', 9, 2), ('alias', 9, 10), ('boolean', 7, 7), ('inc', (1 << 63) - 1, None)]:
            binary = args.binaries / name
            compile_worker(args, source, name, binary)
            init(name, n)
            finish(name, expected, 4 if expected is None else 0)
        for body, message in [('n = n + 1\nn -> r', 'read-only'), ('n ÷ 2 -> r', 'unsupported opcode')]:
            source.write_text('rwfunc rejected(n:int64) -> (r:int64) {\n' + body + '\n}\n')
            try:
                compile_worker(args, source, 'rejected', args.binaries / 'rejected')
            except RuntimeError as error:
                if message not in str(error):
                    raise
            else:
                raise AssertionError('unsupported source accepted')
            checks += 1
        init('iops', 6)
        finish('iops', None, 75, 1)
        with closing(Store(args.frontend, dsn)) as store:
            store.view('status', c.c_uint8)[:] = b'initial'
            store.view('frames')[3] = 100
        finish('iops', None, 1)
        init('iops', 2)
        finish('iops', 2)
        print(json.dumps({'checks': checks, 'boundary_restarts': 32, 'sigkill_restarts': args.sigkill_restarts,
                          'prepared_partial_commit_replay': 'passed', 'visibility_and_rejections': 'passed'}))


if __name__ == '__main__':
    main()
