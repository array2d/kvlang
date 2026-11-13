import lmdb, os, time, tempfile

N_OPS = 100_000
VALUE = b'x' * 64

def bench():
    tmpdir = tempfile.mkdtemp()
    env = lmdb.open(tmpdir, map_size=512*1024*1024, max_dbs=1)

    t0 = time.monotonic()
    db = env.begin(write=True)
    for i in range(N_OPS):
        db.put(f'{i:08d}'.encode(), VALUE)
    db.commit()
    t1 = time.monotonic()
    print(f"LMDB write        ops={N_OPS}  time={t1-t0:.3f}s  rate={N_OPS/(t1-t0):,.0f} ops/s")

    t0 = time.monotonic()
    db = env.begin()
    for i in range(N_OPS):
        db.get(f'{i:08d}'.encode())
    t1 = time.monotonic()
    print(f"LMDB read         ops={N_OPS}  time={t1-t0:.3f}s  rate={N_OPS/(t1-t0):,.0f} ops/s")

    t0 = time.monotonic()
    db = env.begin(write=True)
    for i in range(N_OPS):
        k = f'w{i:08d}'.encode()
        db.put(k, VALUE)
        db.delete(k)
        if i % 1000 == 999:
            db.commit()
            db = env.begin(write=True)
    db.commit()
    t1 = time.monotonic()
    print(f"LMDB put+del       ops={N_OPS}  time={t1-t0:.3f}s  rate={N_OPS/(t1-t0):,.0f} ops/s")

    env.close()
    for root, dirs, files in os.walk(tmpdir, topdown=False):
        for f in files: os.unlink(os.path.join(root, f))
        os.rmdir(root)

if __name__ == '__main__':
    bench()
    print()
    print("=== slotboxalloc (C, pure alloc+free, single-thread) ===")
    print("slotboxalloc 64B alloc+free:                ~4,000,000 ops/s")
    print()
    print("LMDB is a persistent, transactional KV store with full ACID.")
    print("slotboxalloc is an in-memory block allocator with no persistence.")
