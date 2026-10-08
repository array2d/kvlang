#!/usr/bin/env python3
"""Experimental scalar RWIR compiler with all execution state in SHM KVSpace."""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from fast import MODES

STRATEGIES = ['decoded', 'partial', 'guarded', 'copy-patch', 'copy-patch-guarded', 'address-cache', *MODES]


class KV:
    def __init__(self, frontend, dsn):
        self.lib = c.CDLL(str(frontend))
        self.lib.kvspaceConnect.argtypes = [c.c_char_p]
        self.lib.kvspaceConnect.restype = c.c_void_p
        self.lib.kvspaceGet.argtypes = [c.c_void_p, c.c_char_p, c.c_int,
                                       c.POINTER(c.c_void_p), c.POINTER(c.c_uint32)]
        self.lib.kvspaceClose.argtypes = [c.c_void_p]
        self.handle = self.lib.kvspaceConnect(dsn.encode())
        if not self.handle:
            raise RuntimeError('cannot open layout store')

    def get(self, key):
        p, n = c.c_void_p(), c.c_uint32()
        if self.lib.kvspaceGet(self.handle, key.encode(), 0, c.byref(p), c.byref(n)):
            raise RuntimeError('cannot read ' + key)
        if not n.value:
            return None
        value = c.string_at(p, n.value)
        head = 1 << value[0]
        return value[18:head].rstrip(b'\0').decode(), value[head:]

    def close(self):
        self.lib.kvspaceClose(self.handle)


def instruction_code(fn, row, instruction, functions, bindings=None):
    op, reads, writes, call = instruction
    code = []
    bindings = bindings or {}
    binary = {'bitand': '&', 'bitor': '|', 'bitxor': '^', '&': '&', '|': '|', '^': '^',
              'eq': '==', 'neq': '!=', 'lt': '<', 'le': '<=', 'gt': '>', 'ge': '>='}
    arithmetic = {'+': 'add', 'add': 'add', '-': 'sub', 'sub': 'sub', '*': 'mul', 'mul': 'mul'}
    comparisons = {'eq', 'neq', 'lt', 'le', 'gt', 'ge'}
    for i, (mode, value) in enumerate(reads):
        expr = f'load_constant(constants, {value})' if mode == 'constant' else f'load_cell(state, base + {2 + 2 * fn["slots"][value]})'
        expr = bindings.get('reads', {}).get(i, expr)
        code.append(f'cell_t a{i} = {expr}; if (!a{i}.tag) return fail(h, "undefined operand");')
    dest = 'base + ' + str(2 + 2 * fn['slots'][writes[0][1]]) if writes else None
    dest = bindings.get('dest', dest)
    prefix, sep, bare = op.rpartition('·')
    if not sep:
        bare = op
    elif prefix != 'int64':
        raise ValueError('unsupported opcode type: ' + op)
    next_row = bindings.get('next_row', str(row + 1))
    target_depth, target_row = 'depth', next_row
    if call:
        child = functions[op]
        if len(reads) != child['nr'] or not dest:
            raise ValueError('call arity mismatch')
        code += [f'if (a{i}.tag != 1) return fail(h, "expected int64");' for i in range(len(reads))]
        code += ['if (depth + 1 >= DEPTH) return fail(h, "stack overflow");',
                 'size_t next = (depth + 1) * STRIDE;',
                 'for (size_t j = 2; j < STRIDE; j += 2) stage(journal, &count, next + j, 0);',
                 f'stage(journal, &count, next, {bindings.get("callee", child["id"])});',
                 f'stage(journal, &count, next + 1, {next_row});',
                 f'stage(journal, &count, next + {2 + 2 * child["nr"]}, 3);',
                 f'stage(journal, &count, next + {3 + 2 * child["nr"]}, {dest});']
        for i, (mode, value) in enumerate(reads):
            reference = f'a{i}.value' if mode == 'constant' else 'base + ' + str(2 + 2 * fn['slots'][value])
            reference = bindings.get('references', {}).get(i, reference)
            tag = bindings.get('tags', {}).get(i, 1 if mode == 'constant' else 3)
            code += [f'stage(journal, &count, next + {2 + 2 * i}, {tag});',
                     f'stage(journal, &count, next + {3 + 2 * i}, {reference});']
        target_depth, target_row = 'depth + 1', '1'
    elif bare == 'return' and not reads and not writes:
        target_depth, target_row = 'depth ? depth - 1 : 0', 'depth ? state[base + 1] : 0'
    elif bare in ('goto', 'br') and not writes:
        if len(reads) != (1 if bare == 'goto' else 3):
            raise ValueError('branch arity mismatch')
        if bare == 'goto':
            code.append('if (a0.tag != 1 || !a0.value) return fail(h, "invalid branch");')
            target_row = 'a0.value'
        else:
            code.append('if (a0.tag != 2 || a1.tag != 1 || a2.tag != 1 || !a1.value || !a2.value) return fail(h, "invalid branch");')
            target_row = 'a0.value ? a1.value : a2.value'
    else:
        if not dest:
            raise ValueError('missing output')
        if bare == '=' and len(reads) == 1:
            expression, tag = 'a0.value', 'a0.tag'
        elif bare in (*binary, *arithmetic, 'shl', 'shr') and len(reads) == 2:
            code.append('if (a0.tag != 1 || a1.tag != 1) return fail(h, "expected int64");')
            tag = '2' if bare in comparisons else '1'
            if bare in arithmetic:
                code += ['int64_t value;', f'if (__builtin_{arithmetic[bare]}_overflow((int64_t)a0.value, (int64_t)a1.value, &value)) return fail(h, "integer overflow");']
                expression = '(uint64_t)value'
            elif bare in ('shl', 'shr'):
                code.append('if (a1.value >= 64) return fail(h, "invalid shift");')
                expression = 'a0.value << a1.value' if bare == 'shl' else '(uint64_t)((int64_t)a0.value >> a1.value)'
            else:
                expression = f'((int64_t)a0.value {binary[bare]} (int64_t)a1.value)'
        else:
            raise ValueError('unsupported opcode: ' + op)
        code.append(f'if (store_cell(state, journal, &count, {dest}, (cell_t){{{expression}, {tag}}})) return fail(h, "invalid output");')
    tail = bindings.get('tail', 'break;')
    code.append(f'if (commit(h, state, journal, pc, count, {target_depth}, {target_row})) return -1;\n{tail}')
    return code


def generate(kv, entry):
    functions, constants = {}, []
    entry = entry.removeprefix('/lib/')

    def literal(value, tag):
        cell = (tag, value)
        if cell not in constants:
            constants.append(cell)
        return ('constant', constants.index(cell))

    def operand(v):
        if not v:
            raise ValueError('missing operand')
        kind, body = v
        if kind == 'rwir':
            name, _, ty = body[5:].partition(b'\0')
            if ty and ty not in (b'int64', b'bool'):
                raise ValueError('unsupported scalar type: ' + ty.decode())
            return ('slot', name.decode())
        if kind == 'int64' and len(body) == 8:
            return literal(struct.unpack('<Q', body)[0], 1)
        if kind == 'bool' and body in (b'\0', b'\1'):
            return literal(body[0], 2)
        raise ValueError('unsupported operand: ' + kind)

    def load(name):
        if name in functions:
            return
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name):
            raise ValueError('unsupported function name: ' + name)
        base = '/lib/' + name
        sig = kv.get(base + '/[0,0]')
        if not sig or sig[0] != 'rwfunc' or len(sig[1]) != 5:
            raise ValueError('missing function: ' + name)
        nr, nw, dynamic = struct.unpack('<HHB', sig[1])
        if dynamic or nw != 1:
            raise ValueError('prototype requires one output and fixed arity')
        slots = {f'*[0,-{i + 1}]': i for i in range(nr)}
        slots['*[0,1]'] = nr
        for i in [*range(-nr, 0), 1]:
            definition = kv.get(base + f'.[0,{i}]')
            if not definition or definition[1].split(b'\0')[-1] != b'int64':
                raise ValueError('prototype requires int64 parameters and result')
        fn = dict(id=len(functions), nr=nr, slots=slots, instructions=[])
        functions[name] = fn
        row = 1
        while (v := kv.get(base + f'/[{row},0]')):
            if v[0] not in ('rwir', 'rwfunc') or len(v[1]) < 5:
                raise ValueError('invalid instruction')
            nr_i, nw_i, dyn_i = struct.unpack('<HHB', v[1][:5])
            if dyn_i or nw_i > 1:
                raise ValueError('unsupported instruction arity')
            op = v[1][5:].decode()
            call = v[0] == 'rwfunc'
            if call:
                op = op.removeprefix('/lib/')
            reads = [operand(kv.get(base + f'/[{row},-{j}]')) for j in range(1, nr_i + 1)]
            writes = [operand(kv.get(base + f'/[{row},{j}]')) for j in range(1, nw_i + 1)]
            if any(mode != 'slot' for mode, _ in writes):
                raise ValueError('literal output')
            for mode, slot in reads + writes:
                if mode == 'slot' and slot not in slots:
                    if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', slot):
                        raise ValueError('unsupported address: ' + slot)
                    slots[slot] = len(slots)
            fn['instructions'].append((op, reads, writes, call))
            if call:
                load(op)
            row += 1
        if not fn['instructions'] or row > 10000:
            raise ValueError('invalid instruction count')

    load(entry)
    stride = 2 + 2 * max(len(f['slots']) for f in functions.values())
    code = []
    for fn in functions.values():
        code.append(f'case {fn["id"]}: switch (row) {{')
        for row, (op, reads, writes, call) in enumerate(fn['instructions'], 1):
            code.append(f'case {row}: {{')
            code += instruction_code(fn, row, (op, reads, writes, call), functions)
            code.append('}')
        code.append('default: return fail(h, "invalid PC row"); } break;')
    schema = dict(entry=entry, stride=stride, functions=functions, constants=constants)
    template = Path(__file__).with_name('worker.c.in').read_text()
    source = template.replace('@STRIDE@', str(stride)).replace('@ARGS@', str(functions[entry]['nr'])).replace('@RESULT@', str(2 + 2 * functions[entry]['nr'])).replace('@CONSTANTS@', ','.join('UINT64_C(' + str(v) + ')' for cell in constants for v in cell) or 'UINT64_C(1),UINT64_C(0)').replace('@DISPATCH@', '\n'.join(code))
    schema['program_sha256'] = hashlib.sha256(source.encode()).hexdigest()
    return source.replace('@PROGRAM@', schema['program_sha256']), schema


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for flag in ['kvlang', 'frontend', 'backend', 'include', 'source', 'entry', 'output']:
        ap.add_argument('--' + flag, required=True)
    ap.add_argument('--strategy', choices=STRATEGIES)
    args = ap.parse_args()
    os.environ['KVSPACE_BACKEND_PATH'] = args.backend
    with tempfile.TemporaryDirectory(prefix='kvlang-native-layout-') as td:
        source = Path(td) / 'source.kv'
        source.write_text(Path(args.source).read_text().replace('__SCALE__', '1'))
        dsn = 'shm://' + td + '/store'
        layout = subprocess.run([args.kvlang, 'layout', str(source)], env=dict(os.environ, KVSPACE=dsn, LOG_LEVEL='warn'), capture_output=True, text=True)
        if layout.returncode:
            raise ValueError('layout failed: ' + layout.stderr)
        kv = KV(args.frontend, dsn)
        try:
            text, schema = generate(kv, args.entry)
        finally:
            kv.close()
    out = Path(args.output).resolve()
    if args.strategy:
        from strategies import generate as generate_strategy
        text, _ = generate_strategy(text, schema, args.strategy)
    out.with_suffix('.c').write_text(text)
    out.with_suffix('.schema.json').write_text(json.dumps(schema, indent=2) + '\n')
    frontend = Path(args.frontend).resolve()
    subprocess.run(['cc', '-O3', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + args.include, str(out.with_suffix('.c')), str(frontend), '-Wl,-rpath,' + str(frontend.parent), '-o', str(out)], check=True)


if __name__ == '__main__':
    main()
