#!/usr/bin/env python3
"""Verify recovery, live edits, and reference fallbacks across execution methods."""
import argparse
from contextlib import closing
import ctypes as c
import json
import os
from pathlib import Path
import sys
import tempfile

from run import compile_worker, final_state_digest, output, run
from strategies import VARIANTS
from fast import MODES
from validate import Store


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['kvlang', 'frontend', 'backend', 'include', 'binaries', 'output']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--cpu', type=int, default=24)
    ap.add_argument('--variants', nargs='+', choices=[*VARIANTS[:-1], *MODES], default=VARIANTS[:-1])
    args = ap.parse_args()
    os.environ.update(KVSPACE_BACKEND_PATH=str(args.backend), LOG_LEVEL='warn')
    env = dict(os.environ)
    variants = args.variants
    if 'aot' not in variants or len(set(variants)) != len(variants):
        ap.error('--variants must include aot and contain no duplicates')
    report = {'suites': {}, 'cross_method_restarts': 0, 'live_edit_groups': 0,
              'prepared_call_replays': 0, 'address_cache_groups': 0}
    for variant in variants:
        command = [sys.executable, Path(__file__).with_name('validate.py')]
        for name in ['kvlang', 'frontend', 'backend', 'include']:
            command += ['--' + name, getattr(args, name)]
        command += ['--binaries', args.binaries / variant, '--cpu', args.cpu]
        if variant != 'aot':
            command += ['--strategy', variant]
        result, _ = run(command, env, args.cpu)
        report['suites'][variant] = json.loads(result.stdout.splitlines()[-1])
        print(variant, report['suites'][variant], flush=True)

    with tempfile.TemporaryDirectory(prefix='strategy-recovery-') as td:
        dsn = 'shm://' + td + '/store'

        def worker(variant, case, action, *values, expected=0):
            return run([args.binaries / variant / case, dsn, action, *values], env, args.cpu, expected)[0]

        worker('aot', 'fib', 'init', 10)
        worker('aot', 'fib', 'run')
        reference = final_state_digest(args.frontend, dsn)
        for start in variants:
            for finish in variants:
                worker(start, 'fib', 'init', 10)
                worker(start, 'fib', 'run', 100, expected=75)
                result = worker(finish, 'fib', 'run')
                if output(result)[1] != 55 or final_state_digest(args.frontend, dsn) != reference:
                    raise AssertionError(f'cross-method mismatch: {start}/{finish}')
                report['cross_method_restarts'] += 1

        schema = json.loads((args.binaries / 'aot/fib.schema.json').read_text())
        child = schema['stride']
        patches = [(child + j, 0) for j in range(2, child, 2)]
        patches += [(child, schema['functions']['fib']['id']), (child + 1, 2),
                    (child + 4, 3), (child + 5, 4), (child + 2, 3), (child + 3, 2)]
        for variant in variants:
            worker('aot', 'fib', 'init', 10)
            with closing(Store(args.frontend, dsn)) as store:
                frames, journal = store.view('frames'), store.view('journal')
                journal[1:3] = [1, 1]
                for i, (index, value) in enumerate(patches):
                    journal[3 + 2 * i:5 + 2 * i] = [index, value]
                    if i % 2 == 0:
                        frames[index] = value
                store.view('pc', c.c_uint8)[-7] = ord('x')
                journal[0] = len(patches) + 1
            if output(worker(variant, 'fib', 'run'))[1] != 55 or final_state_digest(args.frontend, dsn) != reference:
                raise AssertionError('partial call replay mismatch: ' + variant)
            report['prepared_call_replays'] += 1

        for fault in ['read_reference', 'write_reference', 'cycle', 'invalid_write', 'constant', 'pc', 'argument']:
            final = set()
            for variant in variants:
                worker('aot', 'iops', 'init', 6)
                worker('aot', 'iops', 'run', 1, expected=75)
                with closing(Store(args.frontend, dsn)) as store:
                    frames = store.view('frames')
                    schema = json.loads((args.binaries / 'aot/iops.schema.json').read_text())
                    child = schema['stride']
                    spare = (256 - 1) * child + 2
                    # Modify references only after a worker has stopped.
                    if fault == 'read_reference':
                        frames[spare:spare + 2] = [1, 4]
                        frames[2:4] = [3, spare]
                    elif fault == 'write_reference':
                        target = child + 2 + 2 * schema['functions']['iops']['slots']['i']
                        frames[spare:spare + 2] = [0, 0]
                        frames[target:target + 2] = [3, spare]
                    elif fault == 'cycle':
                        frames[2:4] = [3, 2]
                    elif fault == 'invalid_write':
                        target = child + 2 + 2 * schema['functions']['iops']['slots']['i']
                        frames[target:target + 2] = [3, (1 << 64) - 1]
                    elif fault == 'constant':
                        index = schema['constants'].index([1, 0])
                        store.view('constants')[index * 2 + 1] = 5
                    elif fault == 'pc':
                        store.view('pc', c.c_uint8)[-7:-3] = b'0009'
                    else:
                        frames[3] = 4
                invalid = fault in ('cycle', 'invalid_write', 'pc')
                result = worker(variant, 'iops', 'run', expected=4 if invalid else 0)
                if not invalid:
                    expected = 4 if fault in ('read_reference', 'argument') else 11 if fault == 'constant' else 6
                    if output(result)[1] != expected:
                        raise AssertionError(f'live edit mismatch: {variant}/{fault}')
                final.add(final_state_digest(args.frontend, dsn))
            if len(final) != 1:
                raise AssertionError('live edit KV state mismatch: ' + fault)
            report['live_edit_groups'] += 1

        source = Path(td) / 'aliases.kv'
        source.write_text('''rwfunc update(n:int64) -> (r:int64) {
    n + 1 -> r
    n + 2 -> r
}
rwfunc changed(n:int64) -> (r:int64) {
    10 -> x
    20 -> y
    update(x) -> x
    update(y) -> y
    x + y -> r
}
rwfunc descend(n:int64, k:int64) -> (r:int64) {
    if (k == 0) { n -> r } else { descend(n, k - 1) -> r }
}
rwfunc deep(n:int64) -> (r:int64) { descend(n, 12) -> r }
rwfunc leaf(n:int64) -> (r:int64) { n + 1 -> r }
rwfunc relay(n:int64, flag:int64) -> (r:int64) {
    if (flag == 1) { leaf(n) -> r } else { n + 2 -> r }
}
rwfunc forward(n:int64) -> (r:int64) {
    10 -> x
    20 -> y
    relay(x, 1) -> a
    relay(y, 0) -> b
    n + a + b -> r
}
''')
        for case, expected in [('changed', 36), ('deep', 7), ('forward', 53)]:
            states = set()
            for variant in variants:
                args.strategy = None if variant == 'aot' else variant
                binary = args.binaries / variant / case
                compile_worker(args, source, case, binary)
                worker(variant, case, 'init', 7)
                if case == 'forward':
                    schema = json.loads(binary.with_suffix('.schema.json').read_text())
                    with closing(Store(args.frontend, dsn)) as store:
                        store.view('frames')[2:4] = [3, 2 * schema['stride'] + 2]
                if output(worker(variant, case, 'run'))[1] != expected:
                    raise AssertionError('address-cache mismatch: ' + variant + '/' + case)
                states.add(final_state_digest(args.frontend, dsn))
            if len(states) != 1:
                raise AssertionError('address-cache KV state mismatch: ' + case)
            report['address_cache_groups'] += 1
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
