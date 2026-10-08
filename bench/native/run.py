#!/usr/bin/env python3
"""Compare scalar native workers, the interpreter, and unchanged Python fixtures."""
import argparse
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
    run(['python3', Path(__file__).with_name('compile.py'), '--kvlang', args.kvlang,
         '--frontend', args.frontend, '--backend', args.backend, '--include', args.include,
         '--source', source, '--entry', entry, '--output', binary], os.environ, args.cpu)


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
        directory = ROOT / 'benchmark/cases' / case
        kv_source = directory / (case + '.kv')
        py_source = directory / (case + '.py')
        body = '1 << n -> sh\nsh - 1 -> all\nnq(0, 0, 0, all) -> r' if case == 'nqueens' else f'{case}(n) -> r'
        wrapper = '\nrwfunc bench_native(n:int64) -> (r:int64) {\n' + body + '\n}\n'
        source = binaries / (case + '.kv')
        source.write_text(kv_source.read_text() + wrapper)
        binary = binaries / case
        compile_worker(args, source, 'bench_native', binary)
        workers[case] = binary
        metadata['inputs'][case] = {'scales': CASES[case], 'kv_sha256': sha(kv_source),
                                    'python_sha256': sha(py_source), 'wrapper': wrapper,
                                    'worker_sha256': sha(binary), 'generated_c_sha256': sha(binary.with_suffix('.c')),
                                    'schema': json.loads(binary.with_suffix('.schema.json').read_text())}
    for variant, frontend in [('interpreter', args.baseline_frontend), ('release', args.frontend)]:
        with tempfile.TemporaryDirectory(prefix='native-libs-') as td:
            env = dict(os.environ, KVSPACE='shm://' + td + '/store', LD_PRELOAD=str(frontend), LD_DEBUG='libs')
            result, _ = run([args.kvlang, '-c', 'println("ready")'], env, args.cpu)
        paths = re.findall(r'calling init:\s*(/\S+)', result.stderr)
        metadata[variant + '_loaded'] = {p: sha(p) for p in paths if 'libkv' in Path(p).name}
        if not any(Path(p).resolve() == frontend.resolve() for p in paths):
            raise RuntimeError('frontend was not loaded: ' + variant)
    with tempfile.TemporaryDirectory(prefix='native-worker-libs-') as td:
        result, _ = run([workers['iops'], 'shm://' + td + '/store', 'init', 1],
                        dict(os.environ, LD_DEBUG='libs'), args.cpu)
    paths = re.findall(r'calling init:\s*(/\S+)', result.stderr)
    metadata['native_loaded'] = {p: sha(p) for p in paths if 'libkv' in Path(p).name}
    if not any(Path(p).resolve() == args.frontend.resolve() for p in paths):
        raise RuntimeError('native frontend was not loaded')
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
                        ns = re.search(r'__bench_ns:\s*(\d+)', result.stdout)
                        value = re.search(r'(?:iops a|fib|queens|result) =\s*(-?\d+)', result.stdout)
                        if not ns or not value or int(ns[1]) <= 0:
                            raise RuntimeError('invalid output: ' + result.stdout)
                        value = int(value[1])
                        if expected is not None and value != expected:
                            raise RuntimeError(f'result mismatch: {case}/{scale}/{name}: {value} != {expected}')
                        expected = value
                        row = dict(case=case, scale=scale, pair=pair, variant=name, kernel_ns=int(ns[1]), wall_ns=wall, result=value)
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
