#!/usr/bin/env python3
"""Isolate shared-memory address caching on unchanged scalar benchmark fixtures."""
import argparse
from contextlib import closing
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import tempfile

from run import ROOT, CASES, benchmark, compile_worker, run, sha
from validate import Store

VARIANTS = ['lookup-all', 'lookup-pc', 'cached', 'python']
MEMBERS = ['pc', 'status', 'frames', 'constants', 'journal', 'program']


def output(result):
    ns = re.search(r'__bench_ns:\s*(\d+)', result.stdout)
    value = re.search(r'(?:iops a|fib|queens|result) =\s*(-?\d+)', result.stdout)
    if not ns or not value or int(ns[1]) <= 0:
        raise RuntimeError('invalid output: ' + result.stdout)
    return int(ns[1]), int(value[1])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['kvlang', 'frontend', 'backend', 'include', 'output']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--cpu', type=int, default=24)
    ap.add_argument('--pairs', type=int, default=9)
    args = ap.parse_args()
    if args.pairs < 1:
        ap.error('--pairs must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    binaries = args.output / 'binaries'
    for variant in VARIANTS[:-1]:
        (binaries / variant).mkdir(parents=True, exist_ok=True)
    os.environ.update(KVSPACE_BACKEND_PATH=str(args.backend), LOG_LEVEL='warn')
    env = dict(os.environ)
    metadata = dict(cpu=args.cpu, cpu_model=benchmark.cpu_model(), python=platform.python_version(),
                    compiler=subprocess.check_output(['cc', '--version'], text=True).splitlines()[0],
                    pairs=args.pairs, inputs={}, warmups=1,
                    timing='execute only, including redo; excludes init/attach/schema/report; wall includes init and run',
                    conditions='Linux x86-64 SHM, fixed value shapes, exclusive worker; each instruction reads live KV values',
                    lookup_all='one kvspaceGet per PC/status/frames/constants/journal key per instruction',
                    lookup_pc='one kvspaceGet for PC per instruction; other addresses cached',
                    cached='all five body addresses attached once per process; no KV value cache',
                    libraries={str(p): sha(p) for p in [args.frontend, args.backend / 'libkvspace-c.so.1']},
                    kvlang=dict(path=str(args.kvlang), sha256=sha(args.kvlang)))
    scales = {case: [*benchmark.SWEEP[case], CASES[case][-1]] for case in CASES}
    workers = {}
    for case in scales:
        directory = ROOT / 'benchmark/cases' / case
        kv_source, py_source = directory / (case + '.kv'), directory / (case + '.py')
        body = '1 << n -> sh\nsh - 1 -> all\nnq(0, 0, 0, all) -> r' if case == 'nqueens' else f'{case}(n) -> r'
        wrapper = '\nrwfunc bench_native(n:int64) -> (r:int64) {\n' + body + '\n}\n'
        source = binaries / 'cached' / (case + '.kv')
        source.write_text(kv_source.read_text() + wrapper)
        cached = binaries / 'cached' / case
        compile_worker(args, source, 'bench_native', cached)
        workers[case] = {'cached': cached}
        for variant, level in [('lookup-pc', 1), ('lookup-all', 2)]:
            binary = binaries / variant / case
            run(['cc', '-O3', '-std=c11', '-Wall', '-Wextra', '-Werror',
                 f'-DBENCH_KEY_LOOKUP={level}', '-I' + str(args.include), cached.with_suffix('.c'),
                 args.frontend, '-Wl,-rpath,' + str(args.frontend.parent), '-o', binary], env, args.cpu)
            shutil.copyfile(cached.with_suffix('.schema.json'), binary.with_suffix('.schema.json'))
            workers[case][variant] = binary
        metadata['inputs'][case] = dict(scales=scales[case], kv_sha256=sha(kv_source),
                                      python_sha256=sha(py_source), wrapper=wrapper,
                                      generated_c_sha256=sha(cached.with_suffix('.c')),
                                      workers={k: sha(p) for k, p in workers[case].items()})
    with tempfile.TemporaryDirectory(prefix='key-cache-libraries-') as td:
        loaded, _ = run([workers['iops']['cached'], 'shm://' + td + '/store', 'init', 1],
                        dict(env, LD_DEBUG='libs'), args.cpu)
    paths = re.findall(r'calling init:\s*(/\S+)', loaded.stderr)
    metadata['loaded'] = {p: sha(p) for p in paths if 'libkv' in Path(p).name}
    if not any(Path(p).resolve() == args.frontend.resolve() for p in paths):
        raise RuntimeError('requested frontend not loaded')

    def sample(case, scale, variant):
        with tempfile.TemporaryDirectory(prefix='key-cache-sample-') as td:
            dsn = 'shm://' + td + '/store'
            if variant == 'python':
                result, wall = run(['python3', ROOT / 'benchmark/cases' / case / (case + '.py')],
                                   dict(env, BENCH_SCALE=str(scale)), args.cpu)
                digest = ''
            else:
                worker = workers[case][variant]
                _, init_wall = run([worker, dsn, 'init', scale], env, args.cpu)
                result, wall = run([worker, dsn, 'run'], env, args.cpu)
                wall += init_wall
                with closing(Store(args.frontend, dsn)) as store:
                    digest = hashlib.sha256(b''.join(store.get('/vthread/native/‥' + m)[1]
                                                    for m in MEMBERS)).hexdigest()
            ns, value = output(result)
            return dict(kernel_ns=ns, wall_ns=wall, result=value, state_sha256=digest)

    rows = []
    with (args.output / 'samples.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['case', 'scale', 'pair', 'variant', 'kernel_ns',
                                              'wall_ns', 'result', 'state_sha256'], lineterminator='\n')
        writer.writeheader()
        for case, points in scales.items():
            for scale in points:
                for variant in VARIANTS:
                    sample(case, scale, variant)
                for pair in range(args.pairs):
                    order = VARIANTS if pair % 2 == 0 else VARIANTS[::-1]
                    samples = {v: sample(case, scale, v) for v in order}
                    if len({s['result'] for s in samples.values()}) != 1:
                        raise RuntimeError('output mismatch')
                    if len({samples[v]['state_sha256'] for v in VARIANTS[:-1]}) != 1:
                        raise RuntimeError('final KV state mismatch')
                    for variant, data in samples.items():
                        row = dict(case=case, scale=scale, pair=pair, variant=variant, **data)
                        rows.append(row)
                        writer.writerow(row)
                    f.flush()
                    print(f'{case}({scale}) pair {pair + 1}/{args.pairs}: output and KV state equal', flush=True)
    summary = {}
    for case, points in scales.items():
        for scale in points:
            samples = [r for r in rows if r['case'] == case and r['scale'] == scale]
            medians = {v: statistics.median(r['kernel_ns'] for r in samples if r['variant'] == v) / 1e3
                       for v in VARIANTS}
            ratios = {}
            for variant in VARIANTS[:2]:
                paired = [next(r['kernel_ns'] for r in samples if r['pair'] == p and r['variant'] == variant) /
                          next(r['kernel_ns'] for r in samples if r['pair'] == p and r['variant'] == 'cached')
                          for p in range(args.pairs)]
                ratios[variant] = dict(median=statistics.median(paired), minimum=min(paired), maximum=max(paired))
            summary[f'{case}({scale})'] = dict(medians_us=medians, paired_speedups=ratios)
    metadata['samples'] = len(rows)
    metadata['output_checks'] = len(rows)
    metadata['equal_final_state_groups'] = len(rows) // len(VARIANTS)
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (args.output / 'medians-us.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
