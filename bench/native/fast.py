"""Reduce per-instruction PC codec and redo overhead without caching values."""
from pathlib import Path
import re

MODES = ['fast-commit', 'fast-pc', 'fast', 'fast-slots', 'fast-address', 'fast-loop']


def generate(source, mode, schema):
    loops = set()
    if mode == 'fast-loop':
        for fn in schema['functions'].values():
            for row, (op, reads, _, call) in enumerate(fn['instructions'], 1):
                bare = op.rsplit('·', 1)[-1]
                targets = reads if bare == 'goto' else reads[1:] if bare == 'br' else []
                if not call and any(m == 'constant' and 0 < schema['constants'][v][1] <= row for m, v in targets):
                    loops.add(fn['id'])
    if mode in ('fast-slots', 'fast-address', 'fast-loop'):
        template = Path(__file__).with_name('strategies.c.in').read_text()
        cache = mode == 'fast-address' or bool(loops)
        helper_start = '#ifdef STRATEGY_ADDRESS_CACHE' if cache else 'static inline cell_t read_slot('
        helpers = template[template.index(helper_start):template.index('static inline __attribute__((always_inline)) cell_t read_operand')]
        start = source.index('static int execute(')
        end = source.index('static int number(', start)
        execute = source[start:end].replace('load_cell(', 'read_slot(').replace('store_cell(', 'write_slot(')
        prefix = '#define STRATEGY_GUARDED\n'
        if cache:
            for signature in ['static inline cell_t read_slot(', 'static inline int write_slot(']:
                pos = helpers.index(signature)
                begin = helpers.index('#ifdef STRATEGY_ADDRESS_CACHE', pos)
                middle = helpers.index('#ifdef STRATEGY_GUARDED', begin)
                finish = helpers.index('#endif', middle) + len('#endif\n')
                helpers = helpers[:begin] + helpers[middle:finish] + helpers[begin:middle] + helpers[finish:]
            if mode == 'fast-loop':
                helpers = helpers.replace('read_slot(volatile uint64_t *state, size_t index)',
                                          'read_slot(volatile uint64_t *state, size_t index, int cached)')
                for signature in ['static inline cell_t read_slot(', 'static inline int write_slot(']:
                    pos = helpers.index(signature)
                    begin = helpers.index('#ifdef STRATEGY_ADDRESS_CACHE', pos)
                    finish = helpers.index('#endif', begin)
                    helpers = helpers[:begin] + helpers[begin:finish].replace('\n', '\n    if (cached) {\n', 1) + '\n    }\n' + helpers[finish:]
                helpers = helpers.replace('unsigned *count, size_t index, cell_t value)',
                                          'unsigned *count, size_t index, cell_t value, int cached)')
                helpers = helpers.replace('static inline cell_t read_slot(', 'static inline __attribute__((always_inline)) cell_t read_slot(')
                helpers = helpers.replace('static inline int write_slot(', 'static inline __attribute__((always_inline)) int write_slot(')
                for fn in schema['functions'].values():
                    begin = execute.index(f'case {fn["id"]}: switch (row) {{')
                    finish = execute.index('default: return fail(h, "invalid PC row"); } break;', begin)
                    block = re.sub(r'read_slot\(state, base \+ (\d+)\)',
                                   lambda m: f'read_slot(state, base + {m[1]}, {int(fn["id"] in loops)})', execute[begin:finish])
                    block = re.sub(r'(write_slot\(state, journal, &count, .*?, \(cell_t\)\{.*?\})(\))',
                                   lambda m: f'{m[1]}, {int(fn["id"] in loops)}{m[2]}', block)
                    execute = execute[:begin] + block + execute[finish:]
            execute = execute.replace('size_t next = (depth + 1) * STRIDE;',
                                      'size_t next = (depth + 1) * STRIDE;\ninvalidate_frame(next);')
            prefix += '#define STRATEGY_ADDRESS_CACHE\n'
        source = prefix + source[:start] + helpers + execute + source[end:]
    if mode != 'fast-pc':
        source = source.replace('    return replay(h, state, journal, pc);', '''    for (unsigned i = 0; i < count; i++)
        state[journal[3 + i * 2]] = journal[4 + i * 2];
    set_pc(pc, depth, row);
    atomic_store_explicit((_Atomic uint64_t *)journal, 0, memory_order_release);
    return 0;''')
        source = source.replace('static int commit(', 'static inline __attribute__((always_inline)) int commit(')
    if mode != 'fast-commit':
        codec = Path(__file__).with_suffix('.c.in').read_text()
        for name, count, width in [('DEPTH', 256, 3), ('ROW', 10000, 4)]:
            words = [int.from_bytes(f'{n:0{width}d}'.encode(), 'little') for n in range(count)]
            codec = codec.replace('@' + name + '@', ','.join(map(str, words)))
        start = source.index('static unsigned digits(')
        end = source.index('static int replay(', start)
        source = source[:start] + codec + '\n' + source[end:]
        start = source.index("        if (status[0] != 'r'")
        end = source.index('            return 1;', start)
        source = source[:start] + '''        if (((volatile word_t *)status)->value != UINT32_C(0x6e6e7572) ||
            ((volatile word_t *)(status + 3))->value != UINT32_C(0x676e696e))
''' + source[end:]
    return source, dict(jit_bytes=0, loop_cached_functions=[name for name, fn in schema['functions'].items() if fn['id'] in loops])
