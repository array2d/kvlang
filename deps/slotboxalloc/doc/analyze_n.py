
import math, csv, sys, os

def fmt_bytes(b):
    if b < 1024:
        return f"{b}B"
    elif b < 1024 * 1024:
        return f"{b/1024:.0f}KB"
    elif b < 1024 * 1024 * 1024:
        return f"{b/(1024*1024):.1f}MB"
    else:
        return f"{b/(1024**3):.2f}GB"

def obj_level(N, size_bytes):
    if size_bytes <= 8:
        return 0
    L0_need = math.ceil(size_bytes / 8)
    for k in range(1, 64):
        slot_units = N ** (k - 1)
        max_units = (N - 1) * slot_units
        if L0_need <= max_units:
            return k
    return 64

def obj_slots(N, size_bytes):
    k = obj_level(N, size_bytes)
    if k == 0:
        return 1, 0
    slot_units = N ** (k - 1)
    L0_need = math.ceil(size_bytes / 8)
    m = math.ceil(L0_need / slot_units)
    return m, k

def freebitmap_scan_ops(N):
    if N <= 64:
        words = math.ceil(N / 64)
        return f"tzcnt×{words}"
    elif N <= 256:
        chunks = math.ceil(N / 256)
        return f"AVX2×{chunks}"
    else:
        chunks = math.ceil(N / 512)
        return f"AVX512×{chunks}"

def analyze(N, mem_bytes, label):
    L0_units = mem_bytes // 8
    if L0_units < 1:
        return None

    if N == 1:
        max_level = 0
    else:
        max_level = int(math.floor(math.log(L0_units, N)))
    while (N ** max_level) > L0_units and max_level > 0:
        max_level -= 1
    while max_level < 60 and (N ** (max_level + 1)) <= L0_units:
        max_level += 1

    depth = max_level + 1
    root_level = max_level
    root_slots = L0_units // (N ** max_level) if max_level >= 0 else L0_units

    bitmap_bits = N
    bitmap_bytes = math.ceil(N / 8)
    bitmap_cl = math.ceil(bitmap_bytes / 64)
    scan_method = freebitmap_scan_ops(N)

    SMALL_SIZES = [8, 16, 24, 32, 48, 64, 96, 128, 192, 256]
    small_levels = sorted(set(obj_level(N, s) for s in SMALL_SIZES))
    small_min_L, small_max_L = small_levels[0], small_levels[-1]
    descent_small = max(0, root_level - small_max_L)
    alloc_ops_small = descent_small + math.ceil(N / 64)
    free_ops_small = descent_small + 1

    LARGE_SIZES = [1024*1024, 2*1024*1024, 4*1024*1024,
                   8*1024*1024, 16*1024*1024, 32*1024*1024,
                   64*1024*1024, 100*1024*1024]
    large_levels = sorted(set(obj_level(N, s) for s in LARGE_SIZES))
    if large_levels:
        large_min_L, large_max_L = large_levels[0], large_levels[-1]
        descent_large = max(0, root_level - large_max_L)
    else:
        large_min_L = large_max_L = -1
        descent_large = -1

    def gc_worst(level):
        if level == 0:
            return 8
        slot_bytes = 8 * (N ** (level - 1))
        return slot_bytes * (N - 1)

    small_gc_safe = gc_worst(small_max_L) <= 10 * 1024 * 1024
    large_gc_safe = large_max_L >= 0 and gc_worst(large_max_L) <= 10 * 1024 * 1024

    base_fields = 20
    arrays_no_gc = bitmap_bytes + N * 4
    box_head_no_gc = base_fields + arrays_no_gc
    box_head_with_gc = box_head_no_gc + 2 * N

    return {
        "N": N,
        "N_bits": int(math.log2(N)) if N & (N-1) == 0 else f"~{math.log2(N):.1f}",
        "L0": "8B (无slot)",
        "L1_slot": "8B",
        "L1_box": fmt_bytes(N * 8),
        "L2_slot": fmt_bytes(8 * N),
        "freebitmap_bits": N,
        "freebitmap_B": bitmap_bytes,
        "freebitmap_CL": bitmap_cl,
        "freebitmap_scan": scan_method,
        f"{label}_max_level": max_level,
        f"{label}_depth": depth,
        f"{label}_root_slots": root_slots,
        "small_range": "8B-256B",
        "small_levels": f"L{small_min_L}-L{small_max_L}",
        "small_level_count": len(small_levels),
        f"small_descent_{label}": descent_small,
        f"small_alloc_ops_{label}": alloc_ops_small,
        f"small_free_ops_{label}": free_ops_small,
        "small_gc_worst": fmt_bytes(gc_worst(small_max_L)),
        "small_gc_safe": "Y" if small_gc_safe else "N",
        "large_range": "1MB-100MB",
        "large_levels": f"L{large_min_L}-L{large_max_L}" if large_min_L >= 0 else "-",
        "large_level_count": len(large_levels) if large_levels else 0,
        f"large_descent_{label}": descent_large,
        "large_gc_worst": fmt_bytes(gc_worst(large_max_L)) if large_max_L >= 0 else "-",
        "large_gc_safe": "Y" if large_gc_safe else "N",
        "GC_L0": fmt_bytes(gc_worst(0)),
        "GC_L1": fmt_bytes(gc_worst(1)),
        "GC_L2": fmt_bytes(gc_worst(2)),
        "GC_L3": fmt_bytes(gc_worst(3)),
        "box_head_no_gc_B": box_head_no_gc,
        "box_head_with_gc_B": box_head_with_gc,
        "box_head_no_gc_CL": math.ceil(box_head_no_gc / 64),
        "box_head_with_gc_CL": math.ceil(box_head_with_gc / 64),
    }

def main():
    Ns = [2, 16, 64, 256, 1024]
    mem_configs = [(1 * 1024**3, "1GB"), (32 * 1024**3, "32GB")]

    results = []
    for N in Ns:
        row = {}
        for mem_bytes, label in mem_configs:
            r = analyze(N, mem_bytes, label)
            if r:
                row.update(r)
        results.append(row)

    header = (
        f"{'N':<5} {'1G深':<5} {'32G深':<5} "
        f"{'位图':<6} {'小obj层':<8} {'小降(1G)':<8} {'小降(32G)':<8} "
        f"{'小alloc':<8} {'小free':<7} {'小GC':<8} "
        f"{'大降(1G)':<8} {'大降(32G)':<8} {'大GC':<8} "
        f"{'boxH(B)':<8} {'boxH+GC':<8}"
    )
    print(header)
    print("-" * len(header))

    for r in results:
        line = (
            f"{r['N']:<5} {r['1GB_depth']:<5} {r['32GB_depth']:<5} "
            f"{r['freebitmap_B']:<6} {r['small_levels']:<8} "
            f"{r['small_descent_1GB']:<8} {r['small_descent_32GB']:<8} "
            f"{r['small_alloc_ops_1GB']:<8} {r['small_free_ops_1GB']:<7} "
            f"{r['small_gc_worst']:<8} "
            f"{str(r['large_descent_1GB']):<8} {str(r['large_descent_32GB']):<8} "
            f"{r['large_gc_worst']:<8} "
            f"{r['box_head_no_gc_B']:<8} {r['box_head_with_gc_B']:<8}"
        )
        print(line)

    print(f"\n列说明:")
    print(f"  N        = 每 box 的 slot 数")
    print(f"  1G深/32G深 = 总层数 (L0..L_max)")
    print(f"  位图      = freebitmap 字节数")
    print(f"  小obj层   = 8-256B 对象覆盖的 level 范围")
    print(f"  小降(1G/32G) = 小对象从 root 下降到目标层的步数")
    print(f"  小alloc   = 下降 + freebitmap 扫描 (典型操作数)")
    print(f"  小free    = 下降 + bitmap 标记 (典型操作数)")
    print(f"  小GC/大GC = 该范围内最坏 GC 数据搬运量")
    print(f"  大降(1G/32G) = 大对象(1-100MB)下降步数")
    print(f"  boxH(B)   = box_head_t 大小 (无 GC 字段)")
    print(f"  boxH+GC   = box_head_t 大小 (含 v2p+p2v)")

    script_dir = os.path.dirname(os.path.abspath(__file__))
    csv_path = os.path.join(script_dir, "n_analysis.csv")
    fieldnames = [
        "N", "N_bits",
        "L0", "L1_slot", "L1_box", "L2_slot",
        "freebitmap_bits", "freebitmap_B", "freebitmap_CL", "freebitmap_scan",
        "1GB_max_level", "1GB_depth", "1GB_root_slots",
        "32GB_max_level", "32GB_depth", "32GB_root_slots",
        "small_range", "small_levels", "small_level_count",
        "small_descent_1GB", "small_descent_32GB",
        "small_alloc_ops_1GB", "small_free_ops_1GB",
        "small_alloc_ops_32GB", "small_free_ops_32GB",
        "small_gc_worst", "small_gc_safe",
        "large_range", "large_levels", "large_level_count",
        "large_descent_1GB", "large_descent_32GB",
        "large_gc_worst", "large_gc_safe",
        "GC_L0", "GC_L1", "GC_L2", "GC_L3",
        "box_head_no_gc_B", "box_head_with_gc_B",
        "box_head_no_gc_CL", "box_head_with_gc_CL",
    ]
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction='ignore')
        writer.writeheader()
        for r in results:
            writer.writerow(r)
    print(f"\nCSV 已保存: {csv_path}")

if __name__ == "__main__":
    main()
