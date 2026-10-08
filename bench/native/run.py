#!/usr/bin/env python3
"""Compare scalar native workers, the interpreter, and unchanged Python fixtures."""
import argparse
from contextlib import closing
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import tempfile
import time

from compile import KV

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('benchmark', ROOT / 'benchmark/run.py')
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)
CASES = {'iops': (2000, 200000), 'fib': (10, 18), 'nqueens': (6, 8)}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, env, cpu, expected=0):
    start = time.perf_counter_ns()
    result = subprocess.run(['taskset', '-c', str(cpu), *map(str, command)], env=env,
                            capture_output=True, text=True, timeout=120)
    if result.returncode != expected:
        raise RuntimeError(f'{command}: exit={result.returncode}\n{result.stderr}\n{result.stdout}')
    return result, time.perf_counter_ns() - start


def compile_worker(args, source, entry, binary):
    command = ['python3', Path(__file__).with_name('compile.py'), '--kvlang', args.kvlang,
         '--frontend', args.frontend, '--backend', args.backend, '--include', args.include,
         '--source', source, '--entry', entry, '--output', binary]
    if getattr(args, 'strategy', None):
        command += ['--strategy', args.strategy]
    run(command, os.environ, args.cpu)


def compile_case(args, case, binary):
    directory = ROOT / 'benchmark/cases' / case
    kv_source, py_source = directory / (case + '.kv'), directory / (case + '.py')
    body = '1 << n -> sh\nsh - 1 -> all\nnq(0, 0, 0, all) -> r' if case == 'nqueens' else f'{case}(n) -> r'
    wrapper = '\nrwfunc bench_native(n:int64) -> (r:int64) {\n' + body + '\n}\n'
    source = binary.with_suffix('.kv')
    source.write_text(kv_source.read_text() + wrapper)
    compile_worker(args, source, 'bench_native', binary)
    return dict(kv_sha256=sha(kv_source), python_sha256=sha(py_source), wrapper=wrapper,
                generated_c_sha256=sha(binary.with_suffix('.c')))


def output(result):
    ns = re.search(r'__bench_ns:\s*(\d+)', result.stdout)
    value = re.search(r'(?:iops a|fib|queens|result) =\s*(-?\d+)', result.stdout)
    if not ns or not value or int(ns[1]) <= 0:
        raise RuntimeError('invalid output: ' + result.stdout)
    return int(ns[1]), int(value[1])


def loaded_libraries(result, frontend):
    paths = re.findall(r'calling init:\s*(/\S+)', result.stderr)
    if not any(Path(p).resolve() == frontend.resolve() for p in paths):
        raise RuntimeError('frontend was not loaded: ' + str(frontend))
    return {p: sha(p) for p in paths if 'libkv' in Path(p).name}


def final_state_digest(frontend, dsn):
    members = ['pc', 'status', 'frames', 'constants', 'journal', 'program']
    with closing(KV(frontend, dsn)) as store:
        return hashlib.sha256(b''.join(store.get('/vthread/native/‥' + m)[1] for m in members)).hexdigest()


def measure_pairs(args, scales, variants, workers, jit=False):

    def sample(case, scale, variant):
        with tempfile.TemporaryDirectory(prefix='native-paired-') as td:
            env = dict(os.environ, BENCH_SCALE=str(scale))
            if variant == 'python':
                result, wall = run(['python3', ROOT / 'benchmark/cases' / case / (case + '.py')], env, args.cpu)
                digest = ''
            else:
                dsn = 'shm://' + td + '/store'
                worker = workers[case][variant]
                _, init_wall = run([worker, dsn, 'init', scale], env, args.cpu)
                result, wall = run([worker, dsn, 'run'], env, args.cpu)
                wall += init_wall
                digest = final_state_digest(args.frontend, dsn)
            ns, value = output(result)
            data = dict(kernel_ns=ns, wall_ns=wall, result=value, state_sha256=digest)
            if jit:
                match = re.search(r'__jit_ns:\s*(\d+)', result.stdout)
                data['jit_ns'] = int(match[1]) if match else 0
            return data

    fields = ['case', 'scale', 'pair', 'variant', 'kernel_ns', 'wall_ns', 'result', 'state_sha256']
    if jit:
        fields.append('jit_ns')
    rows = []
    with (args.output / 'samples.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        for case, points in scales.items():
            for scale in points:
                for variant in variants:
                    sample(case, scale, variant)
                for pair in range(args.pairs):
                    order = variants if pair % 2 == 0 else variants[::-1]
                    samples = {v: sample(case, scale, v) for v in order}
                    if len({s['result'] for s in samples.values()}) != 1:
                        raise RuntimeError('output mismatch')
                    if len({s['state_sha256'] for v, s in samples.items() if v != 'python'}) != 1:
                        raise RuntimeError('final KV state mismatch')
                    for variant, data in samples.items():
                        row = dict(case=case, scale=scale, pair=pair, variant=variant, **data)
                        rows.append(row)
                        writer.writerow(row)
                    f.flush()
                    print(f'{case}({scale}) pair {pair + 1}/{args.pairs}: output and KV state equal', flush=True)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['kvlang', 'frontend', 'baseline-frontend', 'backend', 'include', 'output']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--cpu', type=int, default=24)
    ap.add_argument('--pairs', type=int, default=9)
    args = ap.parse_args()
    if args.pairs < 1:
        ap.error('--pairs must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    binaries = args.output / 'binaries'
    binaries.mkdir(exist_ok=True)
    os.environ.update(KVSPACE_BACKEND_PATH=str(args.backend), LOG_LEVEL='warn')
    metadata = {'cpu': args.cpu, 'cpu_model': benchmark.cpu_model(),
                'python': platform.python_version(), 'pairs': args.pairs,
                'compiler': subprocess.check_output(['cc', '--version'], text=True).splitlines()[0],
                'worker_flags': '-O3 -std=c11 -Wall -Wextra -Werror',
                'timing': 'fresh init each sample; native execute includes every redo commit, excludes init/attach/schema/result/status; wall includes init and run; interpreter/Python use fixture timers',
                'inputs': {}, 'libraries': {str(p): sha(p) for p in [args.frontend, args.baseline_frontend, args.backend / 'libkvspace-c.so.1']},
                'kvlang': {'path': str(args.kvlang), 'sha256': sha(args.kvlang)}}
    workers = {}
    for case in CASES:
        binary = binaries / case
        metadata['inputs'][case] = dict(compile_case(args, case, binary), scales=CASES[case],
                                        worker_sha256=sha(binary),
                                        schema=json.loads(binary.with_suffix('.schema.json').read_text()))
        workers[case] = binary
    for variant, frontend in [('interpreter', args.baseline_frontend), ('release', args.frontend)]:
        with tempfile.TemporaryDirectory(prefix='native-libs-') as td:
            env = dict(os.environ, KVSPACE='shm://' + td + '/store', LD_PRELOAD=str(frontend), LD_DEBUG='libs')
            result, _ = run([args.kvlang, '-c', 'println("ready")'], env, args.cpu)
        metadata[variant + '_loaded'] = loaded_libraries(result, frontend)
    with tempfile.TemporaryDirectory(prefix='native-worker-libs-') as td:
        result, _ = run([workers['iops'], 'shm://' + td + '/store', 'init', 1],
                        dict(os.environ, LD_DEBUG='libs'), args.cpu)
    metadata['native_loaded'] = loaded_libraries(result, args.frontend)
    rows = []
    with (args.output / 'samples.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['case', 'scale', 'pair', 'variant', 'kernel_ns', 'wall_ns', 'result'], lineterminator='\n')
        writer.writeheader()
        for case, scales in CASES.items():
            for scale in scales:
                for pair in range(args.pairs):
                    order = ['interpreter', 'release', 'native', 'python']
                    if pair % 2:
                        order.reverse()
                    expected = None
                    for name in order:
                        with tempfile.TemporaryDirectory(prefix='native-paired-') as td:
                            env = dict(os.environ, BENCH_SCALE=str(scale))
                            if name == 'python':
                                command = ['python3', ROOT / 'benchmark/cases' / case / (case + '.py')]
                                result, wall = run(command, env, args.cpu)
                            elif name == 'native':
                                dsn = 'shm://' + td + '/store'
                                _, init_wall = run([workers[case], dsn, 'init', scale], env, args.cpu)
                                result, wall = run([workers[case], dsn, 'run'], env, args.cpu)
                                wall += init_wall
                            else:
                                source = Path(td) / (case + '.kv')
                                source.write_text((ROOT / 'benchmark/cases' / case / (case + '.kv')).read_text().replace('__SCALE__', str(scale)))
                                env.update(KVSPACE='shm://' + td + '/store', LD_PRELOAD=str(args.frontend if name == 'release' else args.baseline_frontend))
                                result, wall = run([args.kvlang, source], env, args.cpu)
                        kernel_ns, value = output(result)
                        if expected is not None and value != expected:
                            raise RuntimeError(f'result mismatch: {case}/{scale}/{name}: {value} != {expected}')
                        expected = value
                        row = dict(case=case, scale=scale, pair=pair, variant=name, kernel_ns=kernel_ns, wall_ns=wall, result=value)
                        rows.append(row)
                        writer.writerow(row)
                        f.flush()
                    print(f'{case}({scale}) pair {pair + 1}/{args.pairs}: equal', flush=True)
    summary = {}
    for case, scales in CASES.items():
        for scale in scales:
            medians = {name: statistics.median(r['kernel_ns'] for r in rows if r['case'] == case and r['scale'] == scale and r['variant'] == name) / 1e3 for name in ['interpreter', 'release', 'native', 'python']}
            summary[f'{case}({scale})'] = medians
            print(case, scale, json.dumps(medians), flush=True)
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (args.output / 'medians-us.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
