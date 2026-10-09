#!/usr/bin/env python3
"""Isolate shared-memory address caching on unchanged scalar benchmark fixtures."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tempfile

from run import CASES, benchmark, compile_case, loaded_libraries, measure_pairs, run, sha

VARIANTS = ['lookup-all', 'lookup-pc', 'cached', 'python']


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
        cached = binaries / 'cached' / case
        inputs = compile_case(args, case, cached)
        workers[case] = {'cached': cached}
        for variant, level in [('lookup-pc', 1), ('lookup-all', 2)]:
            binary = binaries / variant / case
            run(['cc', '-O3', '-std=c11', '-Wall', '-Wextra', '-Werror',
                 f'-DBENCH_KEY_LOOKUP={level}', '-I' + str(args.include), cached.with_suffix('.c'),
                 args.frontend, '-Wl,-rpath,' + str(args.frontend.parent), '-o', binary], env, args.cpu)
            shutil.copyfile(cached.with_suffix('.schema.json'), binary.with_suffix('.schema.json'))
            workers[case][variant] = binary
        metadata['inputs'][case] = dict(inputs, scales=scales[case],
                                      workers={k: sha(p) for k, p in workers[case].items()})
    with tempfile.TemporaryDirectory(prefix='key-cache-libraries-') as td:
        loaded, _ = run([workers['iops']['cached'], 'shm://' + td + '/store', 'init', 1],
                        dict(env, LD_DEBUG='libs'), args.cpu)
    metadata['loaded'] = loaded_libraries(loaded, args.frontend)

    rows = measure_pairs(args, scales, VARIANTS, workers)
    summary = {}
    for case, points in scales.items():
        for scale in points:
            samples = [r for r in rows if r['case'] == case and r['scale'] == scale]
            timings = {v: [r['kernel_ns'] for r in samples if r['variant'] == v] for v in VARIANTS}
            medians = {v: statistics.median(values) / 1e3 for v, values in timings.items()}
            ratios = {}
            for variant in VARIANTS[:2]:
                paired = [lookup / cached for lookup, cached in zip(timings[variant], timings['cached'])]
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
