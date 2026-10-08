#!/usr/bin/env python3
"""Compare shared-semantics scalar execution and code-generation strategies."""
import argparse
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile

from compile import STRATEGIES, instruction_code
from run import CASES, benchmark, compile_case, loaded_libraries, measure_pairs, run, sha

VARIANTS = ['aot', *STRATEGIES, 'python']
FLAGS = ['-O3', '-std=c11', '-Wall', '-Wextra', '-Werror']


def generate(source, schema, variant):
    guarded = variant in ('guarded', 'copy-patch-guarded')
    variant = 'copy-patch' if variant == 'copy-patch-guarded' else variant
    shapes, descriptors, positions = {}, [], {}
    for fn in schema['functions'].values():
        positions[fn['id']] = (len(descriptors), len(fn['instructions']))
        for row, instruction in enumerate(fn['instructions'], 1):
            op, reads, writes, call = instruction
            shape = (op if not call else 'call', len(reads), len(writes), call)
            if shape not in shapes:
                shapes[shape] = (len(shapes), fn, row, instruction)
            opcode = shapes[shape][0]
            operands = [(index if mode == 'constant' else 2 + 2 * fn['slots'][index],
                         int(mode == 'constant')) for mode, index in reads]
            dest = 2 + 2 * fn['slots'][writes[0][1]] if writes else 0
            callee = schema['functions'][op]['id'] if call else 0
            descriptors.append(dict(opcode=opcode, row=row, callee=callee, dest=dest, reads=operands))
    initializers, operations = [], []
    for inst in descriptors:
        operands = ', '.join(f'{{{index}, {literal}}}' for index, literal in inst['reads']) or '{0, 0}'
        initializers.append('{' + ', '.join(str(inst[k]) for k in ['opcode', 'row', 'callee', 'dest']) + ', {' + operands + '}}')
    for opcode, fn, row, instruction in shapes.values():
        bindings = dict(reads={i: f'read_operand(ctx, &inst->reads[{i}])' for i in range(len(instruction[1]))},
                        dest='base + inst->dest' if instruction[2] else None,
                        next_row='inst->row + 1', callee='inst->callee', tail='return 0;',
                        references={i: f'(inst->reads[{i}].literal ? a{i}.value : base + inst->reads[{i}].index)'
                                    for i in range(len(instruction[1]))},
                        tags={i: f'(inst->reads[{i}].literal ? 1 : 3)' for i in range(len(instruction[1]))})
        body = '\n'.join(instruction_code(fn, row, instruction, schema['functions'], bindings))
        body = body.replace('size_t next = (depth + 1) * STRIDE;',
                            'size_t next = (depth + 1) * STRIDE;\ninvalidate_frame(next);')
        operations.append(f'case {opcode}: {{\n{body.replace("store_cell(", "write_slot(")}\n}}')
    backend = ''
    if variant == 'copy-patch':
        source = '#define _DEFAULT_SOURCE\n' + source
        helpers = [f'static __attribute__((noinline)) int opcode_{i}(context_t *ctx, const instruction_t *inst) {{ return evaluate(ctx, inst, {i}); }}'
                   for i in range(len(shapes))]
        backend = '\n'.join(helpers) + '''
typedef int (*opcode_fn)(context_t *, const instruction_t *);
typedef int (*entry_fn)(context_t *);
static const opcode_fn handlers[] = {''' + ', '.join(f'opcode_{i}' for i in range(len(shapes))) + '''};
static uint8_t *jit_code;
static size_t jit_size;
static entry_fn jit_entries[sizeof(instructions) / sizeof(*instructions)];

static int jit_prepare(void) {
    static const uint8_t stencil[] = {
        0xf3, 0x0f, 0x1e, 0xfa,
        0x48, 0xbe, 0, 0, 0, 0, 0, 0, 0, 0,
        0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,
        0xff, 0xe0
    };
    _Static_assert(sizeof(entry_fn) == sizeof(uintptr_t), "x86-64 function address required");
    jit_size = sizeof(jit_entries) / sizeof(*jit_entries) * 32;
    jit_code = mmap(NULL, jit_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (jit_code == MAP_FAILED) { jit_code = NULL; return -1; }
    for (size_t i = 0; i < sizeof(jit_entries) / sizeof(*jit_entries); i++) {
        uint8_t *entry = jit_code + i * 32;
        uintptr_t descriptor = (uintptr_t)&instructions[i];
        uintptr_t handler = (uintptr_t)handlers[instructions[i].opcode];
        memcpy(entry, stencil, sizeof(stencil));
        memcpy(entry + 6, &descriptor, sizeof(descriptor));
        memcpy(entry + 16, &handler, sizeof(handler));
        memcpy(&jit_entries[i], &entry, sizeof(entry));
    }
    if (mprotect(jit_code, jit_size, PROT_READ | PROT_EXEC)) return -1;
    return 0;
}
'''
    context = 'context_t ctx = {h, state, constants, journal, pc, base, depth};\n'
    if variant in ('partial', 'guarded', 'address-cache'):
        dispatch = 'switch (state[base]) {\n'
        for fid, (offset, length) in positions.items():
            dispatch += f'case {fid}: switch (row) {{\n'
            for i in range(length):
                inst = descriptors[offset + i]
                dispatch += f'case {i + 1}: if (evaluate(&ctx, &instructions[{offset + i}], {inst["opcode"]})) return -1; break;\n'
            dispatch += 'default: return fail(h, "invalid PC row"); } break;\n'
        dispatch += 'default: return fail(h, "invalid function");\n}'
    else:
        dispatch = 'uint64_t fid = state[base];\nsize_t offset;\nswitch (fid) {\n'
        for fid, (offset, length) in positions.items():
            dispatch += f'case {fid}: if (row > {length}) return fail(h, "invalid PC row"); offset = {offset}; break;\n'
        dispatch += 'default: return fail(h, "invalid function");\n}\n'
        index = 'offset + row - 1'
        if variant == 'copy-patch':
            dispatch += f'if (jit_entries[{index}](&ctx)) return -1;'
        else:
            dispatch += f'const instruction_t *inst = &instructions[{index}];\nif (evaluate(&ctx, inst, inst->opcode)) return -1;'
    template = Path(__file__).with_suffix('.c.in').read_text()
    template = template.replace('@MAX_READS@', str(max(1, max(len(d['reads']) for d in descriptors))))
    template = template.replace('@INSTRUCTIONS@', ',\n'.join(initializers)).replace('@OPERATIONS@', '\n'.join(operations)).replace('@BACKEND@', backend)
    start = source.index('        switch (state[base]) {\n')
    end = source.index('        if (budget &&', start)
    source = source[:start] + context + dispatch + '\n' + source[end:]
    source = source.replace('row = digits(pc, ROW_POS, 4), count = 0;', 'row = digits(pc, ROW_POS, 4);')
    source = source.replace('static int execute(', template + '\nstatic int execute(', 1)
    if guarded:
        source = '#define STRATEGY_GUARDED\n' + source
    if variant == 'address-cache':
        source = '#define STRATEGY_ADDRESS_CACHE\n' + source
    if variant == 'copy-patch':
        source = source.replace('    uint64_t start = now();\n    int rc = execute(',
                                '    uint64_t jit_start = now();\n    if (jit_prepare()) { result = 3; goto done; }\n'
                                '    printf("__jit_ns: %llu\\n", (unsigned long long)(now() - jit_start));\n'
                                '    uint64_t start = now();\n    int rc = execute(')
        source = source.replace('done:\n    kvspaceClose(h);', 'done:\n    if (jit_code) munmap(jit_code, jit_size);\n    kvspaceClose(h);')
    return source, dict(instructions=len(descriptors), opcode_shapes=len(shapes), jit_bytes=32 * len(descriptors) if variant == 'copy-patch' else 0)


def build(args, aot, binary, variant):
    schema = json.loads(aot.with_suffix('.schema.json').read_text())
    source, details = generate(aot.with_suffix('.c').read_text(), schema, variant)
    binary.with_suffix('.c').write_text(source)
    binary.with_suffix('.schema.json').write_text(json.dumps(schema, indent=2) + '\n')
    _, wall = run(['cc', *FLAGS, '-I' + str(args.include), binary.with_suffix('.c'),
                   args.frontend, '-Wl,-rpath,' + str(args.frontend.parent), '-o', binary], os.environ, args.cpu)
    size = subprocess.check_output(['size', str(binary)], text=True).splitlines()[1].split()
    return dict(details, compile_ns=wall, text_bytes=int(size[0]), worker_sha256=sha(binary), generated_c_sha256=sha(binary.with_suffix('.c')))


def codegen_evidence(binaries, metadata):
    evidence = {}
    for case in CASES:
        evidence[case] = {}
        for variant in VARIANTS[:-1]:
            binary = binaries / variant / case
            symbols = subprocess.check_output(['nm', '-S', str(binary)], text=True).splitlines()
            table = any(line.split()[-1] == 'instructions' for line in symbols)
            helpers = sum(line.split()[-1].startswith('opcode_') for line in symbols)
            if variant in ('partial', 'guarded', 'address-cache') and table:
                raise RuntimeError('compiler retained constant descriptors: ' + str(binary))
            if variant.startswith('copy-patch') and helpers != metadata['inputs'][case]['variants'][variant]['opcode_shapes']:
                raise RuntimeError('missing opcode-specific helpers: ' + str(binary))
            evidence[case][variant] = dict(instruction_table_retained=table, opcode_helpers=helpers, worker_sha256=sha(binary))
    return evidence


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ['kvlang', 'frontend', 'backend', 'include', 'output']:
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--cpu', type=int, default=24)
    ap.add_argument('--pairs', type=int, default=9)
    args = ap.parse_args()
    if args.pairs < 1:
        ap.error('--pairs must be positive')
    if platform.machine() != 'x86_64':
        ap.error('copy-and-patch stencils require Linux x86-64')
    os.environ.update(KVSPACE_BACKEND_PATH=str(args.backend), LOG_LEVEL='warn')
    env = dict(os.environ)
    binaries = args.output / 'binaries'
    for variant in VARIANTS[:-1]:
        (binaries / variant).mkdir(parents=True, exist_ok=True)
    metadata = dict(cpu=args.cpu, cpu_model=benchmark.cpu_model(), python=platform.python_version(),
                    compiler=subprocess.check_output(['cc', '--version'], text=True).splitlines()[0],
                    flags=FLAGS, pairs=args.pairs, warmups=1, inputs={},
                    timing='execute includes redo; JIT preparation separate; wall includes init and run; build timings are one observation',
                    libraries={str(p): sha(p) for p in [args.frontend, args.backend / 'libkvspace-c.so.1']},
                    kvlang=dict(path=str(args.kvlang), sha256=sha(args.kvlang)))
    workers = {}
    scales = {case: [*benchmark.SWEEP[case], CASES[case][-1]] for case in CASES}
    for case in CASES:
        aot = binaries / 'aot' / case
        inputs = compile_case(args, case, aot)
        workers[case] = {'aot': aot}
        variants = {}
        for variant in VARIANTS[1:-1]:
            binary = binaries / variant / case
            variants[variant] = build(args, aot, binary, variant)
            workers[case][variant] = binary
        metadata['inputs'][case] = dict(inputs, scales=scales[case], variants=variants, aot_sha256=sha(aot))
        metadata['inputs'][case]['aot_text_bytes'] = int(subprocess.check_output(['size', str(aot)], text=True).splitlines()[1].split()[0])
    with tempfile.TemporaryDirectory(prefix='strategies-libraries-') as td:
        loaded, _ = run([workers['iops']['partial'], 'shm://' + td + '/store', 'init', 1],
                        dict(env, LD_DEBUG='libs'), args.cpu)
    metadata['loaded'] = loaded_libraries(loaded, args.frontend)

    rows = measure_pairs(args, scales, VARIANTS, workers, jit=True)
    summary = {}
    for case, points in scales.items():
        for scale in points:
            samples = [r for r in rows if r['case'] == case and r['scale'] == scale]
            medians = {v: statistics.median(r['kernel_ns'] for r in samples if r['variant'] == v) / 1e3 for v in VARIANTS}
            paired = {}
            aot = [r['kernel_ns'] for r in samples if r['variant'] == 'aot']
            for variant in VARIANTS[1:-1]:
                times = [r['kernel_ns'] for r in samples if r['variant'] == variant]
                ratios = [base / value for base, value in zip(aot, times)]
                paired[variant] = dict(median=statistics.median(ratios), minimum=min(ratios), maximum=max(ratios))
            summary[f'{case}({scale})'] = dict(medians_us=medians,
                wall_medians_ms={v: statistics.median(r['wall_ns'] for r in samples if r['variant'] == v) / 1e6 for v in VARIANTS},
                paired_aot_speedups=paired,
                jit_medians_us={v: statistics.median(r['jit_ns'] for r in samples if r['variant'] == v) / 1e3
                                for v in ['copy-patch', 'copy-patch-guarded']})
    metadata.update(samples=len(rows), equal_final_state_groups=len(rows) // len(VARIANTS))
    metadata['generator_sha256'] = {name: sha(Path(__file__).with_name(name))
        for name in ['compile.py', 'strategies.py', 'strategies.c.in', 'worker.c.in', 'run.py']}
    (args.output / 'codegen.json').write_text(json.dumps(codegen_evidence(binaries, metadata), indent=2) + '\n')
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (args.output / 'medians-us.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
