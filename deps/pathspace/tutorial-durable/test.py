
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WS = ROOT.parent

def find_first(env_key, candidates):
    override = os.environ.get(env_key)
    if override:
        return Path(override)
    for c in candidates:
        p = Path(c)
        if p.exists():
            return p
    return Path(candidates[0])

FRONTEND_SO = str(find_first("PATHSPACE_SO", [
    ROOT / "build" / "libpathspace.so.1",
    "/usr/lib/pathspace/libpathspace.so.1",
]))
PATHSPACE_BIN = str(find_first("PATHSPACE_BIN", [
    ROOT / "cli" / "target" / "release" / "pathspace",
    os.path.expanduser("~/.local/bin/pathspace"),
    shutil.which("pathspace") or "/usr/bin/pathspace",
]))

def extract_expected(script):
    lines = []
    in_block = False
    with open(script) as f:
        for raw in f:
            line = raw.rstrip('\n')
            if in_block:
                if line == '# /end':
                    break
                if line.startswith('# '):
                    lines.append(line[2:])
                elif line == '#':
                    lines.append('')
            elif line == '# expected:':
                in_block = True
    return lines

def run_script(script):
    env = os.environ.copy()
    env['PATHSPACE_BIN'] = PATHSPACE_BIN
    if 'PATHSPACE' not in env:
        with tempfile.TemporaryDirectory(prefix='pathspace-tut-') as tmp:
            env['PATHSPACE'] = f'fs://{tmp}/store'
            r = subprocess.run(['bash', script], capture_output=True, text=True, timeout=30, env=env)
            return r.stdout.rstrip('\n').split('\n') if r.stdout.strip() else [], r
    if env['PATHSPACE'].startswith('fs://'):
        subprocess.run([PATHSPACE_BIN, 'clear'], capture_output=True, timeout=10, env=env)
    r = subprocess.run(['bash', script], capture_output=True, text=True, timeout=30, env=env)
    return (r.stdout.rstrip('\n').split('\n') if r.stdout.strip() else []), r

def test_script(script):
    expected = extract_expected(script)
    actual, r = run_script(script)
    if r.returncode == 0 and expected == actual:
        print(f'PASS  {os.path.basename(script)}')
        return True
    print(f'FAIL  {os.path.basename(script)} (rc={r.returncode})')
    print(f'  expected ({len(expected)} lines): {expected[:3]}...' if len(expected) > 3 else f'  expected: {expected}')
    print(f'  actual   ({len(actual)} lines):   {actual[:3]}...' if len(actual) > 3 else f'  actual:   {actual}')
    if r.stderr.strip():
        print(f'  stderr: {r.stderr.strip()[:300]}')
    return False

CHILD = r'''
# Read one entry via the storage ABI to verify whether the prior process flushed its writes.
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
lib.pathspaceConnect.restype = ctypes.c_void_p
lib.pathspaceConnect.argtypes = [ctypes.c_char_p]
lib.pathspaceResolve.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
                               ctypes.POINTER(ctypes.c_uint64), ctypes.c_char_p,
                               ctypes.c_uint32]
err = ctypes.create_string_buffer(256)
h1 = lib.pathspaceConnect(sys.argv[2].encode())
assert h1, "connect failed"
off = ctypes.c_uint64()
rc = lib.pathspaceResolve(h1, sys.argv[3].encode(), 0, ctypes.byref(off), err, 256)
print("next-op-visible", rc == 0)
sys.exit(0)
'''

def redis_reachable(dsn):
    import socket
    addr = dsn.removeprefix("redis://").split("/", 1)[0]
    host, _, port = addr.partition(":")
    try:
        with socket.create_connection((host or "127.0.0.1", int(port or 6379)), timeout=0.3):
            return True
    except OSError:
        return False

def test_exit_flush():
    if not Path(FRONTEND_SO).exists():
        print(f'FAIL  exit-flush (missing frontend {FRONTEND_SO})')
        return False
    env = os.environ.copy()
    pfx = f'/flushprobe-{os.getpid()}'
    keys = [f'{pfx}/a', f'{pfx}/b', f'{pfx}/c', f'{pfx}/d']
    with tempfile.TemporaryDirectory(prefix="pathspace-flush-") as tmp:
        cases = [
            ('fs', f'fs://{tmp}/fs'),
            ('shm', f'shm://{tmp}/probe.shm'),
        ]
        rds_dsn = env.get('PATHSPACE', 'redis://127.0.0.1:6379')
        if rds_dsn.startswith('redis://') and redis_reachable(rds_dsn):
            cases.insert(0, ('redis', rds_dsn))
        ok = True
        for label, dsn in cases:
            cenv = {**env, 'PATHSPACE': dsn}
            subprocess.run([PATHSPACE_BIN, 'deltree', f'{pfx}/'], capture_output=True, env=cenv)
            for k in keys[:3]:
                subprocess.run([PATHSPACE_BIN, 'set', k, 'char/utf8:v'], capture_output=True, env=cenv)
            child = subprocess.run([sys.executable, '-c', CHILD, FRONTEND_SO, dsn, f'{pfx}/c'],
                                   capture_output=True, text=True, timeout=30, env=env)
            seen = 'next-op-visible True' in child.stdout
            got = subprocess.run([PATHSPACE_BIN, 'get', *keys], capture_output=True, text=True,
                                 timeout=10, env=cenv)
            if label == 'redis':
                subprocess.run([PATHSPACE_BIN, 'deltree', f'{pfx}/'], capture_output=True,
                               timeout=10, env=cenv)
            kept = [l for l in got.stdout.splitlines() if 'char/utf8:v' in l]
            if seen and len(kept) == len(keys):
                print(f'PASS  exit-flush {label}')
            else:
                print(f'FAIL  exit-flush {label} (next-op-visible={seen}, flushed {len(kept)}/{len(keys)})')
                if child.stderr:
                    print(f'  child stderr: {child.stderr.strip()}')
                if got.stderr:
                    print(f'  cli stderr: {got.stderr.strip()}')
                ok = False
        return ok

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    scripts = sorted(
        os.path.join(here, f) for f in os.listdir(here)
        if f.endswith('.sh')
    )
    if not scripts:
        print('no scripts found')
        sys.exit(1)

    results = [test_script(s) for s in scripts]
    results.append(test_exit_flush())
    passed = sum(results)
    print(f'\n{passed}/{len(results)} passed')
    sys.exit(0 if passed == len(results) else 1)

if __name__ == '__main__':
    main()
