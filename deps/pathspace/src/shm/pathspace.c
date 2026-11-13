
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   
#endif

#define BLOCKALLOC_IMPLEMENTATION
#include <blockalloc/blockalloc.h>
#define SBO_MIN_SHARD_BYTES (1ULL << 40)
#define SLOTBOXALLOC_IMPLEMENTATION
#include "slotboxalloc/slotboxobj.h"
#include "pathspace_shm.h"
#include <pathspace/pathspace.h>
#include "xvalue_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define KVS_MAGIC "pathspc-c.v5"
#define ART_PREFIX_MAX 10

#define ART_BLOCK_SZ 104
#define ART_SLAB_INIT (64UL * 1024 * 1024)

#define SBO_HEAD_POOL_INIT (4UL * 1024 * 1024)

#define REGION_RESERVE (1ULL << 38)

#define ART_RESERVE (1ULL << 36)
#define DATA_RESERVE (1ULL << 40)

#define SBO_PUNCH_MIN (64UL * 1024)

static int reserved_meta_path(const char *key) {
    static const char root[] = "/.pathspace-meta";
    return strncmp(key, root, sizeof root - 1) == 0 &&
           (key[sizeof root - 1] == 0 || key[sizeof root - 1] == '/');
}

enum { ART_N4 = 0,
       ART_N16 = 1,
       ART_N48 = 2,
       ART_N256 = 3,
       ART_MOVED = 255 };

typedef struct {
    uint8_t type, prefix[ART_PREFIX_MAX], prefix_len;
    uint16_t count;
    uint16_t _pad;
    uint64_t val;
} art_hdr_t;

#define AV_HAS(v) ((int)((v) & 1u))
#define AV_OFF(v) ((uint64_t)((v) >> 1))
#define AV_MAKE(has, off) ((((uint64_t)(off)) << 1) | ((has) ? 1ull : 0ull))
static inline uint64_t art_val_load(const art_hdr_t *h) {
    return __atomic_load_n(&h->val, __ATOMIC_ACQUIRE);
}
static inline void art_val_store(art_hdr_t *h, uint64_t v) {
    __atomic_store_n(&h->val, v, __ATOMIC_RELEASE);
}
static inline bool art_val_cas(art_hdr_t *h, uint64_t *exp, uint64_t nv) {
    return __atomic_compare_exchange_n(&h->val, exp, nv, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

typedef struct {
    art_hdr_t h;
    uint8_t keys[4];
    int32_t children[4];
} art_n4_t;
typedef struct {
    art_hdr_t h;
    uint8_t keys[16];
    int32_t children[16];
} art_n16_t;
typedef struct {
    art_hdr_t h;
    uint8_t index[256];
    int32_t children[48];
} art_n48_t;
typedef struct {
    art_hdr_t h;
    int32_t children[256];
} art_n256_t;

_Static_assert(sizeof(art_n4_t) <= ART_BLOCK_SZ && sizeof(art_n16_t) <= ART_BLOCK_SZ,
               "N4/N16 must fit one ART block");

static int art_node_sz(int t) {
    return t == ART_N4    ? sizeof(art_n4_t)
           : t == ART_N16 ? sizeof(art_n16_t)
           : t == ART_N48 ? sizeof(art_n48_t)
                          : sizeof(art_n256_t);
}

#define P2_SLOT_N 64

enum { P2_SLOT_FREE = 0, P2_SLOT_MINE = 1, P2_SLOT_CLAIMING = 3 };
typedef struct {
    uint32_t pid;
    uint32_t state;
    uint64_t starttime;
    uint64_t recovery_ack;
    uint32_t quiesced;   
    uint32_t claim_id;   
} p2_slot_t;

#define GARB_CAP (1u << 21)              
#define GARB_THRESHOLD (64ull << 20)     
#define P2_BARRIER_MS_DEFAULT 200
#define GARB_MAX_CLAIM ((1u << 30) - 1u)
typedef struct {
    uint64_t off;
    uint32_t epoch;
    uint32_t meta;
} garb_ent_t;
enum { GE_EMPTY = 0, GE_RESERVED = 1, GE_COMMITTED = 2 };
#define GE_STATE(m) ((uint32_t)((m) & 3u))
#define GE_CLAIM(m) ((uint32_t)((m) >> 2))
#define GE_MAKE(st, id) ((((uint32_t)(id)) << 2) | (uint32_t)(st))

typedef struct {
    char magic[12];
    uint64_t art_slab_size, sbo_head_size, sbo_data_size;
    int32_t art_root;
    
    uint64_t gens_size;
    uint64_t gen_epoch;        
    uint32_t format_version;   
    uint32_t recovery_epoch;   
    uint32_t ack_seq;          
    uint32_t barrier_state;
    uint64_t barrier_pid, barrier_starttime;
    uint64_t garb_cap, garb_head, garb_tail;
    uint64_t committed_bytes;  
    uint64_t freed_bytes;      
    uint64_t garb_freeing;
    uint64_t round_ok, round_aborted, leak_boxes;
    
    uint64_t list_gen;
    uint32_t claim_seq;        
    p2_slot_t slots[P2_SLOT_N];
} pathspace_hdr_t;

#define ART_OFF (sizeof(pathspace_hdr_t) + sizeof(blocks_meta_t))

_Static_assert(ART_SLAB_INIT / (ART_BLOCK_SZ + 2) > 32767ULL / 4,
               "ART_SLAB_INIT too small: blockalloc would pick 14-bit ids");
_Static_assert(ART_RESERVE / (ART_BLOCK_SZ + 8) <= (1ULL << 30),
               "ART_RESERVE too large for 30-bit block ids");
_Static_assert((ART_BLOCK_SZ + 8) % 8 == 0, "ART stride must keep 8B alignment");
_Static_assert(
    SBO_HEAD_POOL_INIT / (sizeof(sbo_box_t) + 2) > 32767ULL / 4,
    "SBO_HEAD_POOL_INIT too small: blockalloc would pick 14-bit ids");

#define SBO_HEAD_FIXED                                                         \
    (sizeof(sbo_meta_t) + sizeof(blocks_meta_t) + sizeof(sbo_lock_t))
static blocks_meta_t *sbo_pool(sbo_meta_t *m) {
    return sbo_pool_meta(m, 0);
}
static sbo_lock_t *sbo_plock(sbo_meta_t *m) {
    return sbo_pool_lock(m, 0);
}
static uint8_t *sbo_pmem(sbo_meta_t *m) {
    return sbo_pool_mem(m, 0);
}
static sbo_box_t *sbo_box(sbo_meta_t *m, int32_t id) {
    return (sbo_box_t *)(sbo_pmem(m) + (size_t)id * m->block_stride +
                         m->box_off);
}

typedef struct {
    int fd;
    uint8_t *base;
    size_t mapped;
    size_t reserve;
} shm_region_t;

struct pathspace {
    shm_region_t r_art, r_head, r_data, r_gen, r_garb;
    garb_ent_t *garb;             
    uint64_t gen_n;               
    uint64_t art_total_seen;      
    pathspace_hdr_t *hdr;
    uint64_t store_id;   
    int p2_slot;         
    uint32_t pid;        
    uint32_t fork_gen;   
    uint32_t quiesced;   
    pthread_t maint_thread;
    uint32_t maint_seq;  
    uint32_t maint_started, maint_stop;  
    uint32_t reclaim_unavailable;        
    blocks_meta_t *art_meta;
    uint8_t *art_data;
    sbo_meta_t *sbo_meta;
    uint8_t *sbo_data;
};

static inline void list_gen_bump(pathspace_t *kv) {
    __atomic_add_fetch(&kv->hdr->list_gen, 1ull, __ATOMIC_RELEASE);
}

static int region_map(shm_region_t *r, size_t size) {
    if (size > r->reserve)
        return -1;
    if (size <= r->mapped)
        return 0;
    if (mmap(r->base, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
             r->fd, 0) == MAP_FAILED)
        return -1;
    r->mapped = size;
    return 0;
}

static int region_attach(shm_region_t *r, bool create, size_t size,
                         size_t reserve) {
    struct stat st;
    if (create ? ftruncate(r->fd, (off_t)size) != 0
               : fstat(r->fd, &st) != 0 || st.st_size < (off_t)size)
        return -1;
    r->reserve = size > reserve ? size : reserve;
    void *p = mmap(NULL, r->reserve, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        return -1;
    r->base = p;
    return region_map(r, size);
}

static void region_close(shm_region_t *r) {
    if (r->base)
        munmap(r->base, r->reserve);
    if (r->fd >= 0)
        close(r->fd);
}

static void kv_sbo_free(pathspace_t *kv, uint64_t off);
static art_hdr_t *art_hdr(pathspace_t *kv, int32_t id);
static int32_t art_child(pathspace_t *kv, void *n, uint8_t b);
static int pfx_shared(const uint8_t *a, int al, const uint8_t *b, int bl);
static int32_t art_walk(pathspace_t *kv, int32_t nid, const uint8_t *key, int klen,
                        int d, int32_t *par, int *pard);
static int32_t art_follow(pathspace_t *kv, int32_t id);

typedef struct {
    uint32_t struct_gen;
    uint32_t value_gen;
} kv_gens_t;

static uint64_t gen_cap(const pathspace_t *kv) {
    blocks_meta_t *m = kv->art_meta;
    uint64_t head = (uint64_t)m->sizeof_block_head;
    return m->total_size / (head + m->block_size);
}

static int32_t art_id_of(pathspace_t *kv, const void *p) {
    if (!p || !kv->art_data)
        return -1;
    return (int32_t)blockid_bydataoffset(kv->art_meta,
                                         (uint64_t)((const uint8_t *)p - kv->art_data));
}

static kv_gens_t *gens_at(pathspace_t *kv, int32_t id) {
    if (id < 0 || !kv->r_gen.base)
        return NULL;
    if ((uint64_t)id >= kv->gen_n)   
        return NULL;
    return (kv_gens_t *)kv->r_gen.base + id;
}

static void gen_bump_struct(pathspace_t *kv, int32_t id) {
    kv_gens_t *g = gens_at(kv, id);
    if (g)
        __atomic_add_fetch(&g->struct_gen, 1u, __ATOMIC_ACQ_REL);
}

static void gen_bump_value(pathspace_t *kv, int32_t id) {
    kv_gens_t *g = gens_at(kv, id);
    if (g)
        __atomic_add_fetch(&g->value_gen, 2u, __ATOMIC_ACQ_REL);
}

static int64_t gen_alloc_epoch(pathspace_t *kv) {
    return (int64_t)__atomic_add_fetch(&kv->hdr->gen_epoch, 1ull, __ATOMIC_ACQ_REL);
}

static int p2_maint_start(pathspace_t *kv);   
static bool p2_claim_alive(pathspace_t *kv, uint32_t claim);
static int kv_sync(pathspace_t *kv);

#ifdef P2_TEST_HOOKS
static struct {
    uint32_t garb_threshold_megabytes; 
    int barrier_ms;                    
    uint32_t hold_drain_ms;            
    uint32_t hold_reserve_ms;          
    bool fail_alloc;                   
    bool fail_art_insert;              
} g_p2_test_knobs;
#endif

static uint64_t p2_garb_threshold(void) {
#ifdef P2_TEST_HOOKS
    if (g_p2_test_knobs.garb_threshold_megabytes)
        return (uint64_t)g_p2_test_knobs.garb_threshold_megabytes << 20;
#endif
    return GARB_THRESHOLD;
}

static void garb_full_report(pathspace_t *kv) {
    static bool once;
    if (once)
        return;
    once = true;
    uint64_t h = __atomic_load_n(&kv->hdr->garb_head, __ATOMIC_ACQUIRE);
    uint64_t t = __atomic_load_n(&kv->hdr->garb_tail, __ATOMIC_ACQUIRE);
    uint32_t m = __atomic_load_n(&kv->garb[h & (GARB_CAP - 1)].meta,
                                 __ATOMIC_ACQUIRE);
    fprintf(stderr,
            "pathspace: garbage ring full (cap=%llu, head=%llu, tail=%llu, "
            "bytes=%llu): head entry #%llu state=%u claim=%u is holding "
            "the drain (no reclaim round yet)\n",
            (unsigned long long)kv->hdr->garb_cap, (unsigned long long)h,
            (unsigned long long)t,
            (unsigned long long)(kv->hdr->committed_bytes -
                                 kv->hdr->freed_bytes),
            (unsigned long long)h, GE_STATE(m), GE_CLAIM(m));
}

static int garb_reserve(pathspace_t *kv, uint64_t *idx) {
    uint32_t claim = kv->p2_slot >= 0 ? kv->hdr->slots[kv->p2_slot].claim_id : 0;
    if (!claim)
        return -1;
    uint64_t h = __atomic_load_n(&kv->hdr->garb_head, __ATOMIC_ACQUIRE);
    uint64_t t = __atomic_load_n(&kv->hdr->garb_tail, __ATOMIC_ACQUIRE);
    if (t - h >= kv->hdr->garb_cap) {
        garb_full_report(kv);
        return -1;
    }
    if (kv->reclaim_unavailable &&
        kv->hdr->garb_cap - (t - h) < kv->hdr->garb_cap / 4) {
        garb_full_report(kv);        
        return -1;
    }
    
    garb_ent_t *e = &kv->garb[t & (GARB_CAP - 1)];
    uint32_t m = __atomic_load_n(&e->meta, __ATOMIC_ACQUIRE);
    if (m != GE_EMPTY) {
        if (GE_STATE(m) != GE_RESERVED || p2_claim_alive(kv, GE_CLAIM(m)))
            return -1;
        __atomic_store_n(&e->meta, GE_EMPTY, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&e->meta, GE_MAKE(GE_RESERVED, claim), __ATOMIC_RELEASE);
    __atomic_store_n(&kv->hdr->garb_tail, t + 1, __ATOMIC_RELEASE);
    *idx = t;
    return 0;
}

static void garb_commit(pathspace_t *kv, uint64_t idx, uint64_t off,
                        uint64_t bytes_retired) {
    garb_ent_t *e = &kv->garb[idx & (GARB_CAP - 1)];
    uint32_t claim = GE_CLAIM(__atomic_load_n(&e->meta, __ATOMIC_ACQUIRE));
    e->off = off;
    e->epoch = __atomic_load_n(&kv->hdr->recovery_epoch, __ATOMIC_ACQUIRE);
    __atomic_store_n(&e->meta, GE_MAKE(GE_COMMITTED, claim), __ATOMIC_RELEASE);
    uint64_t bytes = kv->hdr->committed_bytes + bytes_retired;
    __atomic_store_n(&kv->hdr->committed_bytes, bytes, __ATOMIC_RELEASE);
    if (bytes - __atomic_load_n(&kv->hdr->freed_bytes, __ATOMIC_ACQUIRE) >=
        p2_garb_threshold())
        (void)p2_maint_start(kv);    
}

static void garb_cancel(pathspace_t *kv, uint64_t idx) {
    __atomic_store_n(&kv->garb[idx & (GARB_CAP - 1)].meta, GE_EMPTY,
                     __ATOMIC_RELEASE);
}

static int32_t p2_node_at(pathspace_t *kv, const char *key) {
    int klen = (int)strlen(key);
    int d = 0;
    int32_t nid = kv->hdr->art_root;
    while (nid >= 0) {
        art_hdr_t *h = art_hdr(kv, nid);
        if (!h || h->type == ART_MOVED)
            return -1;
        if (h->prefix_len) {
            int s = pfx_shared(h->prefix, h->prefix_len,
                               (const uint8_t *)key + d, klen - d);
            if (s != h->prefix_len && d + s != klen)
                return -1;
            d += s;
        }
        
        if (d == klen)
            return nid;
        nid = art_child(kv, h, (uint8_t)key[d]);
        d++;
    }
    return -1;
}

static int p2_proc_starttime(uint32_t pid, uint64_t *out) {
#if defined(__linux__)
    char p[64];
    if (snprintf(p, sizeof p, "/proc/%u/stat", pid) >= (int)sizeof p)
        return 2;
    FILE *f = fopen(p, "r");
    if (!f)
        return errno == ENOENT ? 1 : 2;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    if (!n)
        return 2;
    buf[n] = 0;
    char *rp = strrchr(buf, ')');   
    if (!rp)
        return 2;
    char *s = rp + 1;
    int field = 3;                  
    while (*s) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        if (field == 3 && (*s == 'Z' || *s == 'X'))
            return 1;                   
        if (field == 22) {
            *out = strtoull(s, NULL, 10);
            return 0;
        }
        while (*s && *s != ' ')
            s++;
        field++;
    }
    return 2;
#else
    (void)pid;
    (void)out;
    return 2;                       
#endif
}

static bool p2_slot_alive(const p2_slot_t *s) {
    uint64_t st = 0;
    uint32_t state = __atomic_load_n(&s->state, __ATOMIC_ACQUIRE);
    if (state == P2_SLOT_FREE)
        return false;
    if (state == P2_SLOT_CLAIMING)
        return true;
    int rc = p2_proc_starttime(s->pid, &st);
    if (rc == 1)
        return false;               
    if (rc != 0)
        return true;                
    return st == s->starttime;
}

static int p2_reg_claim(pathspace_t *kv) {
    uint64_t st = 0;
    if (p2_proc_starttime((uint32_t)getpid(), &st) != 0)
        st = 0;
    uint32_t me = (uint32_t)getpid();
    for (int i = 0; i < P2_SLOT_N; i++) {
        p2_slot_t *s = &kv->hdr->slots[i];
        uint32_t v = __atomic_load_n(&s->state, __ATOMIC_ACQUIRE);
        if (v == P2_SLOT_CLAIMING && s->pid == 0) {
            
            __atomic_compare_exchange_n(&s->state, &v, P2_SLOT_FREE, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
            continue;
        }
        if (v != P2_SLOT_FREE && !p2_slot_alive(s))
            __atomic_compare_exchange_n(&s->state, &v, P2_SLOT_FREE, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    for (int i = 0; i < P2_SLOT_N; i++) {
        p2_slot_t *s = &kv->hdr->slots[i];
        uint32_t expect = P2_SLOT_FREE;
        if (!__atomic_compare_exchange_n(&s->state, &expect, P2_SLOT_CLAIMING, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
        uint32_t cid = __atomic_add_fetch(&kv->hdr->claim_seq, 1u,
                                          __ATOMIC_ACQ_REL);
        if (cid > GARB_MAX_CLAIM) {          
            __atomic_store_n(&s->state, P2_SLOT_FREE, __ATOMIC_RELEASE);
            return -3;
        }
        s->pid = me;
        s->starttime = st;
        s->quiesced = 1;   
        s->claim_id = cid;
        __atomic_store_n(&s->recovery_ack,
                         __atomic_load_n(&kv->hdr->recovery_epoch, __ATOMIC_ACQUIRE),
                         __ATOMIC_RELEASE);
        __atomic_store_n(&s->state, P2_SLOT_MINE, __ATOMIC_RELEASE);
        return i;
    }
    return -2;                      
}

static void p2_reg_release(pathspace_t *kv) {
    if (kv->p2_slot < 0)
        return;
    if (kv->pid != (uint32_t)getpid()) {   
        kv->p2_slot = -1;
        return;
    }
    p2_slot_t *s = &kv->hdr->slots[kv->p2_slot];
    uint32_t expect = P2_SLOT_MINE;
    __atomic_compare_exchange_n(&s->state, &expect, P2_SLOT_FREE, false,
                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    kv->p2_slot = -1;
}

static void p2_ack_slot(pathspace_t *kv, uint32_t ep) {
    if (kv->p2_slot < 0)
        return;
    __atomic_store_n(&kv->hdr->slots[kv->p2_slot].recovery_ack, ep,
                     __ATOMIC_RELEASE);
    __atomic_add_fetch(&kv->hdr->ack_seq, 1u, __ATOMIC_RELEASE);
#if defined(__linux__)
    
    if (__atomic_load_n(&kv->hdr->barrier_state, __ATOMIC_ACQUIRE))
        syscall(SYS_futex, &kv->hdr->ack_seq, FUTEX_WAKE, INT_MAX, NULL, NULL,
                0);
#endif
}

#define GEN_RESERVE (8ULL << 30)    
static int gens_grow(pathspace_t *kv) {
    uint64_t want = gen_cap(kv) * sizeof(kv_gens_t);
    if (want > GEN_RESERVE)
        return -1;
    kv->gen_n = want / sizeof(kv_gens_t);
    kv->art_total_seen = kv->art_meta->total_size;
    if (want <= kv->r_gen.mapped)
        return 0;
    if (!kv->r_gen.base) {
        void *p = mmap(NULL, GEN_RESERVE, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED)
            return -1;
        kv->r_gen.base = p;
        kv->r_gen.reserve = GEN_RESERVE;
    }
    if (ftruncate(kv->r_gen.fd, (off_t)want) != 0)
        return -1;
    if (region_map(&kv->r_gen, (size_t)want) != 0)
        return -1;
    kv->hdr->gens_size = want;
    return 0;
}

static uint32_t g_fork_gen;
static pthread_once_t g_atfork_once = PTHREAD_ONCE_INIT;
static void p2_atfork_child(void) {
    __atomic_add_fetch(&g_fork_gen, 1u, __ATOMIC_ACQ_REL);
}
static void p2_atfork_install(void) {
    (void)pthread_atfork(NULL, NULL, p2_atfork_child);
}

#define P2_FORK_GUARD(kv)                                                     \
    ((kv)->fork_gen != __atomic_load_n(&g_fork_gen, __ATOMIC_ACQUIRE))
#define P2_VIEW_GUARD(kv) ((kv)->quiesced || P2_FORK_GUARD(kv))

static int head_of(const uint8_t *val, int32_t val_len, pathspaceXvalueHead *out) {
    if (!val || val_len < (int32_t)PATHSPACE_XVALUE_HEAD_LEN)
        return -1;
    memcpy(out, val, sizeof *out);
    
    const uint64_t total = pathspaceXvalueHeadTotalLen(out);
    if (total == 0 || total > (uint64_t)val_len)
        return -1;
    
    {
        const uint8_t *langtype = NULL;
        uint16_t langtype_len = 0;
        if (pathspace_c_scan_attrs(val, total, &langtype, &langtype_len) != 0)
            return -1;
    }
    return 0;
}

static int value_ok(const uint8_t *val, int32_t val_len,
                    pathspaceXvalueHead *out) {
    if (head_of(val, val_len, out) != 0)
        return -1;
    return 0;
}

static long long p2_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int p2_barrier_ms(void) {
#ifdef P2_TEST_HOOKS
    int ms = g_p2_test_knobs.barrier_ms;
    if (ms == 0)
        ms = P2_BARRIER_MS_DEFAULT;
#else
    int ms = P2_BARRIER_MS_DEFAULT;
#endif
    return ms < 1 ? 1 : (ms > 5000 ? 5000 : ms);
}

#if defined(__linux__)
static void p2_futex_wait(uint32_t *addr, uint32_t val, int ms) {
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    syscall(SYS_futex, addr, FUTEX_WAIT, val, &ts, NULL, 0);
}
static void p2_futex_wake(uint32_t *addr) {
    syscall(SYS_futex, addr, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
}
static void p2_futex_wait_priv(uint32_t *addr, uint32_t val, int ms) {
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val, &ts, NULL, 0);
}
static void p2_futex_wake_priv(uint32_t *addr) {
    syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}
#else
static void p2_futex_wait(uint32_t *a, uint32_t v, int ms) { (void)a; (void)v; usleep((useconds_t)ms * 1000); }
static void p2_futex_wake(uint32_t *a) { (void)a; }
static void p2_futex_wait_priv(uint32_t *a, uint32_t v, int ms) { (void)a; (void)v; usleep((useconds_t)ms * 1000); }
static void p2_futex_wake_priv(uint32_t *a) { (void)a; }
#endif

enum { P2_BARRIER_FREE = 0, P2_BARRIER_OWNED = 1 };

static uint32_t p2_my_claim(pathspace_t *kv) {
    return kv->p2_slot >= 0 ? kv->hdr->slots[kv->p2_slot].claim_id : 0;
}

static bool p2_claim_alive(pathspace_t *kv, uint32_t claim) {
    if (!claim)
        return false;
    for (int i = 0; i < P2_SLOT_N; i++) {
        p2_slot_t *s = &kv->hdr->slots[i];
        if (__atomic_load_n(&s->state, __ATOMIC_ACQUIRE) != P2_SLOT_MINE)
            continue;
        if (__atomic_load_n(&s->claim_id, __ATOMIC_ACQUIRE) != claim)
            continue;
        return p2_slot_alive(s);
    }
    return false;
}

static bool p2_elect(pathspace_t *kv) {
    uint32_t mine = p2_my_claim(kv);
    if (!mine)
        return false;
    uint32_t own = GE_MAKE(P2_BARRIER_OWNED, mine);
    for (;;) {
        uint32_t cur = __atomic_load_n(&kv->hdr->barrier_state, __ATOMIC_ACQUIRE);
        if (cur == GE_MAKE(P2_BARRIER_FREE, 0)) {
            if (__atomic_compare_exchange_n(&kv->hdr->barrier_state, &cur, own,
                                            false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE))
                return true;
            continue;
        }
        if (GE_STATE(cur) != P2_BARRIER_OWNED)
            return false;                 
        if (GE_CLAIM(cur) == mine)
            return false;                 
        if (p2_claim_alive(kv, GE_CLAIM(cur)))
            return false;                 
        if (__atomic_compare_exchange_n(&kv->hdr->barrier_state, &cur, own, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;                  
    }
}

static void p2_release_busy(pathspace_t *kv) {
    uint32_t mine = GE_MAKE(P2_BARRIER_OWNED, p2_my_claim(kv));
    __atomic_compare_exchange_n(&kv->hdr->barrier_state, &mine,
                                GE_MAKE(P2_BARRIER_FREE, 0), false,
                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static bool p2_wait_acks(pathspace_t *kv, uint32_t r, int deadline_ms) {
    uint8_t dead[P2_SLOT_N] = {0};
    long long t0 = p2_now_ms();
    for (;;) {
        uint32_t seq = __atomic_load_n(&kv->hdr->ack_seq, __ATOMIC_ACQUIRE);
        bool missing = false;
        int first = -1;
        for (int i = 0; i < P2_SLOT_N; i++) {
            p2_slot_t *s = &kv->hdr->slots[i];
            if (__atomic_load_n(&s->state, __ATOMIC_ACQUIRE) != P2_SLOT_MINE)
                continue;
            if (__atomic_load_n(&s->recovery_ack, __ATOMIC_ACQUIRE) == r)
                continue;
            if (__atomic_load_n(&s->quiesced, __ATOMIC_ACQUIRE))
                continue;
            if (!dead[i]) {
                uint64_t ts = 0;
                int rc = p2_proc_starttime(s->pid, &ts);
                if (rc == 1 || (rc == 0 && ts != s->starttime))
                    dead[i] = 1;
            }
            if (dead[i])
                continue;
            missing = true;
            if (first < 0)
                first = i;
        }
        if (!missing)
            return true;
        long long left = (long long)deadline_ms - (p2_now_ms() - t0);
        if (left <= 0 || __atomic_load_n(&kv->maint_stop, __ATOMIC_ACQUIRE)) {
            if (first >= 0) {
                p2_slot_t *s = &kv->hdr->slots[first];
                fprintf(stderr,
                        "pathspace: barrier round %u abandoned (waited %dms): "
                        "slot %d pid=%u start=%llu ack=%llu quiesced=%u\n",
                        r, deadline_ms, first, s->pid,
                        (unsigned long long)s->starttime,
                        (unsigned long long)__atomic_load_n(&s->recovery_ack,
                                                            __ATOMIC_ACQUIRE),
                        __atomic_load_n(&s->quiesced, __ATOMIC_ACQUIRE));
            }
            return false;
        }
        p2_futex_wait(&kv->hdr->ack_seq, seq, (int)(left < 5 ? left : 5));
    }
}

static int p2_drain(pathspace_t *kv, uint32_t r) {
    int freed = 0;
    uint64_t i = __atomic_load_n(&kv->hdr->garb_head, __ATOMIC_ACQUIRE);
    for (;;) {
        
        if (i >= __atomic_load_n(&kv->hdr->garb_tail, __ATOMIC_ACQUIRE))
            goto out;
        garb_ent_t *e = &kv->garb[i & (GARB_CAP - 1)];
        uint32_t m = __atomic_load_n(&e->meta, __ATOMIC_ACQUIRE);
        uint32_t st = GE_STATE(m);
        if (st == GE_EMPTY) {
            __atomic_store_n(&kv->hdr->garb_head, i + 1, __ATOMIC_RELEASE);
            i++;
            continue;
        }
        if (st == GE_RESERVED) {
            if (p2_claim_alive(kv, GE_CLAIM(m)))
                goto out;
            __atomic_store_n(&e->meta, GE_EMPTY, __ATOMIC_RELEASE);
            __atomic_add_fetch(&kv->hdr->leak_boxes, 1, __ATOMIC_ACQ_REL);
            fprintf(stderr,
                    "pathspace: garbage entry %llu reserved by dead claim %u: "
                    "leaked\n", (unsigned long long)i, GE_CLAIM(m));
            __atomic_store_n(&kv->hdr->garb_head, i + 1, __ATOMIC_RELEASE);
            i++;
            continue;
        }
        if (st != GE_COMMITTED)
            goto out;                 
        if (!((int32_t)(r - __atomic_load_n(&e->epoch, __ATOMIC_ACQUIRE)) > 0))
            goto out;                 
        uint64_t off = __atomic_load_n(&e->off, __ATOMIC_ACQUIRE);
        uint64_t cap = sbo_allocated_size(kv->sbo_meta, off);  
        __atomic_store_n(&kv->hdr->garb_freeing, off, __ATOMIC_RELEASE);
        __atomic_store_n(&e->meta, GE_EMPTY, __ATOMIC_RELEASE);
        __atomic_store_n(&kv->hdr->garb_head, i + 1, __ATOMIC_RELEASE);
#ifdef P2_TEST_HOOKS
        
        if (g_p2_test_knobs.hold_drain_ms)
            usleep((useconds_t)g_p2_test_knobs.hold_drain_ms * 1000);
#endif
        kv_sbo_free(kv, off);
        __atomic_store_n(&kv->hdr->freed_bytes,
                         kv->hdr->freed_bytes + cap, __ATOMIC_RELEASE);
        i++;
        freed++;
    }
out:
    __atomic_store_n(&kv->hdr->garb_freeing, 0, __ATOMIC_RELEASE);
    return freed;
}

static int p2_round(pathspace_t *kv) {
#if !defined(__linux__)
    (void)kv;
    return 0;                             
#else
    if (!kv || kv->hdr->art_root < 0)
        return 0;
    if (kv->pid != (uint32_t)getpid())
        return -1;                        
    if (!p2_elect(kv))
        return -1;
    
    if (kv_sync(kv) != 0) {
        p2_release_busy(kv);
        return -1;
    }
    uint64_t pending = __atomic_exchange_n(&kv->hdr->garb_freeing, 0,
                                           __ATOMIC_ACQ_REL);
    if (pending) {
        __atomic_add_fetch(&kv->hdr->leak_boxes, 1, __ATOMIC_ACQ_REL);
        fprintf(stderr, "pathspace: previous leader died freeing box %llu: "
                        "leaked\n", (unsigned long long)pending);
    }
    uint32_t r = __atomic_add_fetch(&kv->hdr->recovery_epoch, 1u,
                                    __ATOMIC_ACQ_REL);
    if (!p2_wait_acks(kv, r, p2_barrier_ms())) {
        __atomic_add_fetch(&kv->hdr->round_aborted, 1, __ATOMIC_ACQ_REL);
        p2_release_busy(kv);
        return -2;
    }
    int freed = p2_drain(kv, r);
    for (int i = 0; i < P2_SLOT_N; i++) {
        p2_slot_t *s = &kv->hdr->slots[i];
        uint32_t v = P2_SLOT_MINE;
        if (__atomic_load_n(&s->state, __ATOMIC_ACQUIRE) != P2_SLOT_MINE)
            continue;
        if (p2_slot_alive(s))
            continue;
        __atomic_compare_exchange_n(&s->state, &v, P2_SLOT_FREE, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    __atomic_add_fetch(&kv->hdr->round_ok, 1, __ATOMIC_ACQ_REL);
    p2_release_busy(kv);
    return freed;
#endif
}

static void *p2_maint(void *arg) {
    pathspace_t *kv = arg;
    while (!__atomic_load_n(&kv->maint_stop, __ATOMIC_ACQUIRE)) {
        uint32_t seq = __atomic_load_n(&kv->maint_seq, __ATOMIC_ACQUIRE);
        if (__atomic_load_n(&kv->maint_stop, __ATOMIC_ACQUIRE))
            break;
        p2_futex_wait_priv(&kv->maint_seq, seq, 50);
        if (__atomic_load_n(&kv->maint_stop, __ATOMIC_ACQUIRE))
            break;
        if (kv->hdr->committed_bytes -
                __atomic_load_n(&kv->hdr->freed_bytes, __ATOMIC_ACQUIRE) <
            p2_garb_threshold())
            continue;
        p2_round(kv);                 
    }
    return NULL;
}

static int p2_maint_start(pathspace_t *kv) {
#if !defined(__linux__)
    (void)kv;
    return 0;                        
#else
    uint32_t st = 0;
    if (!__atomic_compare_exchange_n(&kv->maint_started, &st, 1u, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return st == 2u ? -1 : 0;    
    if (kv->pid != (uint32_t)getpid()) {
        __atomic_store_n(&kv->maint_started, 0u, __ATOMIC_RELEASE);
        return 0;                    
    }
    if (pthread_create(&kv->maint_thread, NULL, p2_maint, kv) != 0) {
        __atomic_store_n(&kv->maint_started, 2u, __ATOMIC_RELEASE);
        kv->reclaim_unavailable = 1;
        fprintf(stderr, "pathspace: maintenance thread create failed; reclaim "
                        "unavailable (writes hard-fail when the ring is 3/4 "
                        "full)\n");
        return -1;
    }
    return 0;
#endif
}

static void p2_maint_stop(pathspace_t *kv) {
    if (kv->pid != (uint32_t)getpid()) {   
        __atomic_store_n(&kv->maint_started, 0u, __ATOMIC_RELEASE);
        return;
    }
    if (__atomic_load_n(&kv->maint_started, __ATOMIC_ACQUIRE) != 1u)
        return;                            
    __atomic_store_n(&kv->maint_stop, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&kv->maint_seq, 1u, __ATOMIC_RELEASE);
    p2_futex_wake_priv(&kv->maint_seq);
    pthread_join(kv->maint_thread, NULL);
    __atomic_store_n(&kv->maint_started, 0u, __ATOMIC_RELEASE);
}

#ifdef P2_TEST_HOOKS

void pathspaceShmTestSetGarbageThresholdMegabytes(uint32_t megabytes) {
    g_p2_test_knobs.garb_threshold_megabytes = megabytes;
}

void pathspaceShmTestSetBarrierMilliseconds(int milliseconds) {
    g_p2_test_knobs.barrier_ms = milliseconds;
}

void pathspaceShmTestSetHoldDrainMilliseconds(uint32_t milliseconds) {
    g_p2_test_knobs.hold_drain_ms = milliseconds;
}

void pathspaceShmTestSetHoldReserveMilliseconds(uint32_t milliseconds) {
    g_p2_test_knobs.hold_reserve_ms = milliseconds;
}

void pathspaceShmTestSetFailAlloc(bool enabled) {
    g_p2_test_knobs.fail_alloc = enabled;
}

void pathspaceShmTestSetFailArtInsert(bool enabled) {
    g_p2_test_knobs.fail_art_insert = enabled;
}

int64_t pathspaceShmArtAlignCheck(pathspace_t *kv) {
    if (!kv || !kv->art_meta)
        return -2;
    blocks_meta_t *m = kv->art_meta;
    if (m->sizeof_block_head != 8)
        return -2;
    uint64_t stride = (uint64_t)m->sizeof_block_head + m->block_size;
    if (stride % 8)
        return -2;
    uint64_t n = m->total_size / stride;
    for (uint64_t id = 0; id < n; id++) {
        uint64_t off = blockdata_offset(m, id);
        uint64_t val = off + offsetof(art_hdr_t, val);
        if (off % 8 || val % 8 || (val & 63) > 56)
            return (int64_t)id;
    }
    return -1;
}

void pathspaceShmTestSetFormatVersion(pathspace_t *kv, uint32_t v) {
    if (kv && kv->hdr)
        kv->hdr->format_version = v;
}

#endif

static int kv_sync(pathspace_t *kv) {
    if (region_map(&kv->r_art, ART_OFF + (size_t)kv->hdr->art_slab_size) != 0)
        return -1;
    
    if (kv->art_meta->total_size != kv->art_total_seen &&
        gens_grow(kv) != 0)
        return -1;
    if (region_map(&kv->r_head, (size_t)kv->hdr->sbo_head_size) != 0)
        return -1;
    return region_map(&kv->r_data, (size_t)kv->hdr->sbo_data_size);
}

static int art_slab_grow(pathspace_t *kv) {
    size_t want = (size_t)kv->hdr->art_slab_size * 2;
    if (ART_OFF + want > kv->r_art.reserve ||
        ftruncate(kv->r_art.fd, (off_t)(ART_OFF + want)) != 0 ||
        region_map(&kv->r_art, ART_OFF + want) != 0)
        return -1;
    kv->hdr->art_slab_size = want;
    kv->art_meta->total_size = want;
    return 0;
}

#ifdef BLOCKALLOC_COUNT_MASK
#define BM_MALLOC_BLOCKS(m) blocks_malloc_count(m)
#define BM_HEAD_SIZE(m)     blocks_head_size(m)
#else
#define BM_MALLOC_BLOCKS(m) ((uint64_t)(m)->malloc_blocks)
#define BM_HEAD_SIZE(m)     ((uint64_t)(m)->sizeof_block_head)
#endif
static bool sbo_pool_full(sbo_meta_t *m) {
    blocks_meta_t *p = sbo_pool(m);
    if (p->free_next_id != -1)
        return false;
    uint64_t next_end = (uint64_t)block_offset(p, BM_MALLOC_BLOCKS(p)) +
                        BM_HEAD_SIZE(p) + p->block_size;
    return next_end > p->total_size;
}

static int sbo_head_grow(pathspace_t *kv) {
    sbo_meta_t *m = kv->sbo_meta;
    blocks_meta_t *p = sbo_pool(m);
    uint64_t stride = p->sizeof_block_head + p->block_size;
    uint64_t cap = (1ULL << (p->sizeof_block_head == 2 ? 14 : 30)) * stride;
    uint64_t cur = p->total_size, want = cur * 2;
    if (want > cap)
        want = cap;
    if (SBO_HEAD_FIXED + want > kv->r_head.reserve)
        want = (kv->r_head.reserve - SBO_HEAD_FIXED) & ~(uint64_t)7;
    if (want <= cur ||
        ftruncate(kv->r_head.fd, (off_t)(SBO_HEAD_FIXED + want)) != 0 ||
        region_map(&kv->r_head, SBO_HEAD_FIXED + (size_t)want) != 0)
        return -1;
    p->total_size = want;
    m->per_slot_meta = want;
    m->max_blocks = (uint32_t)(want / m->block_stride);
    kv->hdr->sbo_head_size = SBO_HEAD_FIXED + want;
    return 0;
}

static int sbo_data_grow(pathspace_t *kv) {
    sbo_meta_t *m = kv->sbo_meta;
    uint64_t cur = m->data_size, want = cur * 64;
    if (want > kv->r_data.reserve)
        return -1;
    if (sbo_pool_full(m) && sbo_head_grow(kv) != 0)
        return -1;
    if (ftruncate(kv->r_data.fd, (off_t)want) != 0 ||
        region_map(&kv->r_data, (size_t)want) != 0)
        return -1;
    int64_t nid = blocks_alloc(sbo_pool(m), sbo_pmem(m));
    if (nid < 0)
        return -1;
    sbo_box_t *root = sbo_box(m, 0), *b = sbo_box(m, (int32_t)nid);
    memcpy(b, root, sizeof *b);
    b->parent = 0;
    b->parent_slot = 0;
    b->self_id = (int32_t)nid;
    uint64_t mm = root->child_mask;
    while (mm) {
        int i = __builtin_ctzll(mm);
        mm &= mm - 1;
        sbo_box(m, root->children[i])->parent = (int32_t)nid;
    }
    sbo_lock_t *lk = sbo_plock(m);
    uint8_t old_lvl = b->objlevel;
    if (old_lvl >= 1 && old_lvl <= 3 && lk->hot_id1[old_lvl - 1] == 1)
        lk->hot_id1[old_lvl - 1] = (uint32_t)nid + 1;
    uint8_t lvl = (uint8_t)(old_lvl + 1);
    sbo_box_format(root, lvl, -1, 255, 0);
    root->free_mask = ~1ULL;
    root->child_mask = 1ULL;
    root->states[0] = SBO_BOX;
    root->children[0] = (int32_t)nid;
    uint16_t b_comb = sbo_combined(b);
    root->child_cap[0] = b_comb;
    root->child_max_enc = b_comb;
    root->child_max_count = 1;
    root->max_obj_cap = SBO_MULT_MAX;
    root->hint_child = 0;
    m->data_size = want;
    m->slot_shift = (uint8_t)__builtin_ctzll(want);
    kv->hdr->sbo_data_size = want;
    return 0;
}

static int kv_sbo_grow(pathspace_t *kv, bool failed, uint64_t *seen_data) {
    sbo_meta_t *m = kv->sbo_meta;
    sbo_lock(sbo_plock(m));
    int rc = 0;
    if (sbo_pool_full(m))
        rc = sbo_head_grow(kv);
    else if (failed && m->data_size == *seen_data)
        rc = sbo_data_grow(kv);
    else if (failed)
        *seen_data = m->data_size;
    sbo_unlock(sbo_plock(m));
    return rc;
}

static uint64_t kv_sbo_alloc(pathspace_t *kv, size_t n) {
    sbo_meta_t *m = kv->sbo_meta;
    uint64_t seen_data = 0;
    for (int attempt = 0; attempt < 8; attempt++) {
        if (attempt && kv_sync(kv) != 0)
            return (uint64_t)-1;
        if (sbo_pool_full(m) && kv_sbo_grow(kv, false, &seen_data) != 0)
            return (uint64_t)-1;
        uint64_t off = sbo_alloc(m, n);
        if (off != (uint64_t)-1)
            return off;
        if (kv_sbo_grow(kv, true, &seen_data) != 0)
            return (uint64_t)-1;
    }
    return (uint64_t)-1;
}

static void kv_sbo_free(pathspace_t *kv, uint64_t off) {
#if defined(__linux__)
    
    uint64_t sz = sbo_allocated_size(kv->sbo_meta, off);
    if (sz >= SBO_PUNCH_MIN) {
        uint64_t pg = (uint64_t)sysconf(_SC_PAGESIZE);
        uint64_t a = (off + pg - 1) & ~(pg - 1), b = (off + sz) & ~(pg - 1);
        if (b > a)
            (void)fallocate(kv->r_data.fd,
                            FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                            (off_t)a, (off_t)(b - a));
    }
#endif
    sbo_free(kv->sbo_meta, off);
}

static void *art_blk(pathspace_t *kv, int32_t id) {
    if (id < 0)
        return NULL;
    return kv->art_data + blockdata_offset(kv->art_meta, (uint64_t)id);
}

_Static_assert(offsetof(blocks_meta_t, used_blocks) ==
                   offsetof(blocks_meta_t, block_size) + 2 * sizeof(uint64_t),
               "blockalloc: sizeof_block_head/malloc_blocks must share one u64 word");
#ifdef BLOCKALLOC_COUNT_MASK
static inline void bm_set_malloc_blocks(blocks_meta_t *m, uint64_t n) {
    blocks_set_malloc_count(m, n);
}
#else
#define BM_WORD_OFF (offsetof(blocks_meta_t, block_size) + sizeof(uint64_t))
static unsigned bm_count_shift(void) {
    blocks_meta_t t;
    memset(&t, 0, sizeof t);
    t.malloc_blocks = 1;
    uint64_t w;
    memcpy(&w, (const char *)&t + BM_WORD_OFF, sizeof w);
    return w ? (unsigned)__builtin_ctzll(w) : 64u;
}

static void bm_set_malloc_blocks(blocks_meta_t *m, uint64_t n) {
    uint64_t *w = (uint64_t *)((char *)m + BM_WORD_OFF);
    const unsigned sh = bm_count_shift();
    const uint64_t mask = ((1ULL << 60) - 1) << sh;
    uint64_t old = __atomic_load_n(w, __ATOMIC_RELAXED);
    __atomic_store_n(w, (old & ~mask) | ((n << sh) & mask), __ATOMIC_RELEASE);
}
#endif

static bool bm_layout_ok(void) {
    blocks_meta_t t;
    memset(&t, 0, sizeof t);
    t.sizeof_block_head = 8;
    t.malloc_blocks = 5;
    t.used_blocks = 7;
    bm_set_malloc_blocks(&t, 0x0123456789ABCDEull);
    return t.sizeof_block_head == 8 && t.malloc_blocks == 0x0123456789ABCDEull &&
           t.block_size == 0 && t.used_blocks == 7;
}
static int64_t art_bump(pathspace_t *kv, uint32_t k) {
    blocks_meta_t *m = kv->art_meta;
    spin_lock(&m->lock);
    uint64_t id = BM_MALLOC_BLOCKS(m);
    if ((uint64_t)block_offset(m, id + k) > m->total_size) {
        spin_unlock(&m->lock);
        return -1;
    }
    setblock_t(m, kv->art_data + block_offset(m, id), 1, -1);
    bm_set_malloc_blocks(m, id + k);
    m->used_blocks += k;
    spin_unlock(&m->lock);
    return (int64_t)id;
}
static int32_t art_balloc(pathspace_t *kv, int t) {
    const uint64_t head = BM_HEAD_SIZE(kv->art_meta);
    const uint64_t stride = head + kv->art_meta->block_size;
    const uint32_t k = (uint32_t)(((uint64_t)art_node_sz(t) +
                                   head + stride - 1) /
                                  stride);
    int64_t id = art_bump(kv, k);
    while (id < 0) {
        if (art_slab_grow(kv) != 0 || gens_grow(kv) != 0)
            return -1;
        id = art_bump(kv, k);
    }
    if (id >= 0) {
        kv_gens_t *g = gens_at(kv, (int32_t)id);
        if (g) {
            
            __atomic_store_n(&g->struct_gen,
                             (uint32_t)gen_alloc_epoch(kv), __ATOMIC_RELEASE);
            __atomic_store_n(&g->value_gen, 0u, __ATOMIC_RELEASE);
        }
    }
    return (int32_t)id;
}
static art_hdr_t *art_hdr(pathspace_t *kv, int32_t id) {
    return (art_hdr_t *)art_blk(kv, id);
}

static int pfx_shared(const uint8_t *a, int al, const uint8_t *b, int bl) {
    int n = al < bl ? al : bl;
    for (int i = 0; i < n; i++)
        if (a[i] != b[i])
            return i;
    return n;
}

static int32_t art_child(pathspace_t *kv, void *n, uint8_t b) {
    art_hdr_t *h = (art_hdr_t *)n;
    switch (h->type) {
    case ART_N4: {
        art_n4_t *x = n;
        for (int i = 0; i < (int)h->count; i++)
            if (x->keys[i] == b)
                return x->children[i];
        return -1;
    }
    case ART_N16: {
        art_n16_t *x = n;
        int lo = 0, hi = (int)h->count - 1;
        while (lo <= hi) {
            int m = (lo + hi) / 2;
            if (x->keys[m] == b)
                return x->children[m];
            if (x->keys[m] < b)
                lo = m + 1;
            else
                hi = m - 1;
        }
        return -1;
    }
    case ART_N48: {
        art_n48_t *x = n;
        uint8_t idx = x->index[b];
        return idx == 255 ? -1 : x->children[idx];
    }
    case ART_N256: {
        return ((art_n256_t *)n)->children[b];
    }
    }
    return -1;
}

static int32_t art_follow(pathspace_t *kv, int32_t id);

static int32_t art_walk(pathspace_t *kv, int32_t nid, const uint8_t *key, int klen,
                        int d, int32_t *par, int *pard) {
    while (nid >= 0) {
        art_hdr_t *h = art_hdr(kv, nid);
        if (!h || h->type == ART_MOVED)
            return -1;
        if (h->prefix_len) {
            
            if (pfx_shared(h->prefix, h->prefix_len, key + d, klen - d) !=
                h->prefix_len)
                return -1;
            d += h->prefix_len;
        }
        if (d == klen)
            return AV_HAS(art_val_load(h)) ? nid : -1;
        if (par)
            *par = nid;
        if (pard)
            *pard = d;
        nid = art_child(kv, h, key[d]);
        d++;
    }
    return -1;
}

static int32_t art_find2(pathspace_t *kv, int32_t nid, const uint8_t *key, int klen,
                         int32_t *par, int *pard) {
    if (nid < 0 || !key)
        return -1;
    int32_t parent = -1;
    int parent_d = 0;
    int32_t id = art_walk(kv, nid, key, klen, 0, &parent, &parent_d);
    if (par)
        *par = parent;
    if (pard)
        *pard = parent_d;
    return id;
}
static int32_t art_find(pathspace_t *kv, int32_t nid, const uint8_t *key,
                        int klen) {
    return art_find2(kv, nid, key, klen, NULL, NULL);
}

static int last_key_sep(const uint8_t *key, int klen, int *seplen) {
    int slash = -1, mid = -1;
    for (int i = 0; i < klen; i++) {
        if (key[i] == '/')
            slash = i;
        if (i + 1 < klen && key[i] == 0xC2 && key[i + 1] == 0xB7)
            mid = i;
    }
    if (mid > slash) {
        *seplen = 2;
        return mid;
    }
    if (slash > 0) {
        *seplen = 1;
        return slash;
    }
    *seplen = 0;
    return -1;
}

static int32_t art_find_dir(pathspace_t *kv, int32_t nid, const uint8_t *key,
                            int klen, int last_sep, int seplen, int32_t *dirn,
                            int *dird) {
    int d = 0;
    int32_t dir = -1;
    int dd = 0;
    if (nid < 0 || !key)
        return -1;
    while (nid >= 0) {
        art_hdr_t *h = art_hdr(kv, nid);
        if (!h || h->type == ART_MOVED)
            return -1;
        int entry_d = d;
        if (h->prefix_len) {
            int s = pfx_shared(h->prefix, h->prefix_len, key + d, klen - d);
            if (s != h->prefix_len)
                return -1;
            d += h->prefix_len;
            if (d > klen)
                return -1;
        }
        if (dir < 0 && last_sep > 0 && seplen > 0 &&
            ((entry_d <= last_sep && last_sep < d) ||
             d == last_sep + seplen)) {
            dir = nid;
            dd = d;
        }
        if (d == klen) {
            if (dirn)
                *dirn = dir;
            if (dird)
                *dird = dd;
            return AV_HAS(art_val_load(h)) ? nid : -1;
        }
        nid = art_child(kv, h, key[d]);
        d++;
    }
    return -1;
}
static art_hdr_t *art_search(pathspace_t *kv, int32_t nid, const uint8_t *key,
                             int klen) {
    int32_t id = art_find(kv, nid, key, klen);
    return id < 0 ? NULL : art_hdr(kv, id);
}
static int32_t art_follow(pathspace_t *kv, int32_t id) {
    for (int i = 0; i < 8 && id >= 0; i++) {
        art_hdr_t *h = art_hdr(kv, id);
        if (!h)
            return -1;
        if (h->type != ART_MOVED)
            return id;
        id = (int32_t)AV_OFF(art_val_load(h));
    }
    return -1;
}

static int32_t art_new_leaf(pathspace_t *kv, uint64_t off) {
    int32_t id = art_balloc(kv, ART_N4);
    if (id < 0)
        return -1;
    art_n4_t *x = art_blk(kv, id);
    memset(x, 0, sizeof(*x));
    x->h.type = ART_N4;
    art_val_store(&x->h, AV_MAKE(1, off));
    return id;
}
static int32_t art_new_node(pathspace_t *kv, int t) {
    int32_t id = art_balloc(kv, t);
    if (id < 0)
        return -1;
    void *x = art_blk(kv, id);
    int sz = art_node_sz(t);
    memset(x, 0, sz);
    ((art_hdr_t *)x)->type = (uint8_t)t;
    if (t == ART_N48)
        memset(((art_n48_t *)x)->index, 255, 256);
    if (t == ART_N256) {
        art_n256_t *n = x;
        for (int i = 0; i < 256; i++)
            n->children[i] = -1;
    }
    return id;
}

static int32_t art_grow(pathspace_t *kv, void *on) {
    art_hdr_t *oh = on;
    int nt;
    if (oh->type == ART_N4)
        nt = ART_N16;
    else if (oh->type == ART_N16)
        nt = ART_N48;
    else if (oh->type == ART_N48)
        nt = ART_N256;
    else
        return -1;
    int32_t nid = art_new_node(kv, nt);
    if (nid < 0)
        return -1;
    art_hdr_t *nh = art_hdr(kv, nid);
    nh->prefix_len = oh->prefix_len;
    memcpy(nh->prefix, oh->prefix, oh->prefix_len);
    art_val_store(nh, art_val_load(oh));
    
    for (int i = 0; i < (int)oh->count; i++) {
        uint8_t b = 0;
        int32_t c = -1;
        switch (oh->type) {
        case ART_N4: {
            art_n4_t *x = on;
            b = x->keys[i];
            c = x->children[i];
            break;
        }
        case ART_N16: {
            art_n16_t *x = on;
            b = x->keys[i];
            c = x->children[i];
            break;
        }
        case ART_N48: {
            art_n48_t *x = on;
            c = x->children[i];
            for (int j = 0; j < 256; j++)
                if (x->index[j] == i) {
                    b = (uint8_t)j;
                    break;
                }
            break;
        }
        }
        switch (nt) {
        case ART_N16: {
            art_n16_t *x = (art_n16_t *)nh;
            x->keys[i] = b;
            x->children[i] = c;
            break;
        }
        case ART_N48: {
            art_n48_t *x = (art_n48_t *)nh;
            x->index[b] = (uint8_t)i;
            x->children[i] = c;
            break;
        }
        case ART_N256: {
            ((art_n256_t *)nh)->children[b] = c;
            break;
        }
        }
    }
    nh->count = oh->count;
    if (nt == ART_N16) {
        art_n16_t *x = (art_n16_t *)nh;
        for (int i = 0; i < (int)nh->count - 1; i++)
            for (int j = i + 1; j < (int)nh->count; j++)
                if (x->keys[i] > x->keys[j]) {
                    uint8_t tk = x->keys[i];
                    x->keys[i] = x->keys[j];
                    x->keys[j] = tk;
                    int32_t tc = x->children[i];
                    x->children[i] = x->children[j];
                    x->children[j] = tc;
                }
    }
    {   
        int32_t oid = art_id_of(kv, oh);
        gen_bump_struct(kv, nid);   
        gen_bump_struct(kv, oid);   
    }
    oh->type = ART_MOVED;
    art_val_store(oh, AV_MAKE(0, (uint32_t)nid));
    return nid;
}

static int art_add(pathspace_t *kv, void *n, uint8_t b, int32_t cid) {
    gen_bump_struct(kv, art_id_of(kv, n)); 
    art_hdr_t *h = n;
    switch (h->type) {
    case ART_N4: {
        if (h->count >= 4)
            return -1;
        art_n4_t *x = n;
        x->keys[h->count] = b;
        x->children[h->count] = cid;
        h->count++;
        return 0;
    }
    case ART_N16: {
        if (h->count >= 16)
            return -1;
        art_n16_t *x = n;
        int p = (int)h->count;
        while (p > 0 && x->keys[p - 1] > b) {
            x->keys[p] = x->keys[p - 1];
            x->children[p] = x->children[p - 1];
            p--;
        }
        x->keys[p] = b;
        x->children[p] = cid;
        h->count++;
        return 0;
    }
    case ART_N48: {
        if (h->count >= 48)
            return -1;
        art_n48_t *x = n;
        int s = (int)h->count;
        x->index[b] = (uint8_t)s;
        x->children[s] = cid;
        h->count++;
        return 0;
    }
    case ART_N256: {
        art_n256_t *x = n;
        if (x->children[b] >= 0)
            return -1;
        x->children[b] = cid;
        h->count++;
        return 0;
    }
    }
    return -1;
}

static void art_rm(pathspace_t *kv, void *n, uint8_t b) {
    art_hdr_t *h = n;
    switch (h->type) {
    case ART_N4: {
        art_n4_t *x = n;
        for (int i = 0; i < (int)h->count; i++)
            if (x->keys[i] == b) {
                for (int j = i; j < (int)h->count - 1; j++) {
                    x->keys[j] = x->keys[j + 1];
                    x->children[j] = x->children[j + 1];
                }
                h->count--;
                return;
            }
        break;
    }
    case ART_N16: {
        art_n16_t *x = n;
        int lo = 0, hi = (int)h->count - 1, pos = -1;
        while (lo <= hi) {
            int m = (lo + hi) / 2;
            if (x->keys[m] == b) {
                pos = m;
                break;
            }
            if (x->keys[m] < b)
                lo = m + 1;
            else
                hi = m - 1;
        }
        if (pos < 0)
            return;
        for (int j = pos; j < (int)h->count - 1; j++) {
            x->keys[j] = x->keys[j + 1];
            x->children[j] = x->children[j + 1];
        }
        h->count--;
        break;
    }
    case ART_N48: {
        art_n48_t *x = n;
        uint8_t idx = x->index[b];
        if (idx == 255)
            return;
        x->index[b] = 255;
        int last = (int)h->count - 1;
        if (idx != last) {
            x->children[idx] = x->children[last];
            for (int j = 0; j < 256; j++)
                if (x->index[j] == last) {
                    x->index[j] = idx;
                    break;
                }
        }
        h->count--;
        break;
    }
    case ART_N256: {
        art_n256_t *x = n;
        x->children[b] = -1;
        h->count--;
        break;
    }
    }
}

static int32_t art_leaf_chain(pathspace_t *kv, const uint8_t *key, int klen,
                              int d, uint64_t off) {
    int rem = klen - d;
    if (rem <= ART_PREFIX_MAX) {
        int32_t id = art_new_leaf(kv, off);
        if (id < 0)
            return -1;
        art_n4_t *x = art_blk(kv, id);
        if (rem > 0)
            memcpy(x->h.prefix, key + d, (size_t)rem);
        x->h.prefix_len = (uint8_t)rem;
        return id;
    }
    int32_t id = art_new_node(kv, ART_N4);
    if (id < 0)
        return -1;
    art_hdr_t *x = art_hdr(kv, id);
    memcpy(x->prefix, key + d, ART_PREFIX_MAX);
    x->prefix_len = ART_PREFIX_MAX;
    int32_t sub = art_leaf_chain(kv, key, klen, d + ART_PREFIX_MAX + 1, off);
    if (sub < 0)
        return -1;
    art_add(kv, x, key[d + ART_PREFIX_MAX], sub);
    return id;
}

static int32_t art_ins(pathspace_t *kv, int32_t nid, const uint8_t *key, int klen,
                       int d, uint64_t off) {
    if (nid < 0)
        return art_leaf_chain(kv, key, klen, d, off);

    art_hdr_t *h = art_hdr(kv, nid);
    if (!h)
        return -1;
    int shared = pfx_shared(h->prefix, h->prefix_len, key + d, klen - d);
    int mpl = h->prefix_len < (klen - d) ? h->prefix_len : (klen - d);

    if (shared == mpl && (klen - d) < h->prefix_len) {
        
        int32_t nn = art_new_node(kv, ART_N4);
        if (nn < 0)
            return -1;
        art_hdr_t *nh = art_hdr(kv, nn);
        nh->prefix_len = (uint8_t)shared;
        if (shared > 0)
            memcpy(nh->prefix, key + d, (size_t)shared);
        uint8_t ob = h->prefix[shared];
        h->prefix_len -= (uint8_t)(shared + 1);
        if (h->prefix_len > 0)
            memmove(h->prefix, h->prefix + shared + 1, h->prefix_len);
        art_add(kv, nh, ob, nid);
        art_val_store(nh, AV_MAKE(1, off));
        return nn;
    }
    if (shared < mpl) { 
        int32_t nn = art_new_node(kv, ART_N4);
        if (nn < 0)
            return -1;
        art_hdr_t *nh = art_hdr(kv, nn);
        nh->prefix_len = (uint8_t)shared;
        memcpy(nh->prefix, key + d, shared);
        uint8_t ob = h->prefix[shared];
        h->prefix_len -= (uint8_t)(shared + 1);
        if (h->prefix_len > 0)
            memmove(h->prefix, h->prefix + shared + 1, h->prefix_len);
        art_add(kv, nh, ob, nid);
        int32_t leaf = art_leaf_chain(kv, key, klen, d + shared + 1, off);
        if (leaf < 0)
            return -1;
        art_add(kv, nh, key[d + shared], leaf);
        return nn;
    }
    d += h->prefix_len;
    if (d == klen) {
        art_val_store(h, AV_MAKE(1, off));
        return nid;
    }
    int32_t cid = art_child(kv, h, key[d]);
    if (cid >= 0) {
        int32_t nc = art_ins(kv, cid, key, klen, d + 1, off);
        if (nc < 0)
            return -1;
        if (nc != cid) {
            art_rm(kv, h, key[d]);
            art_add(kv, h, key[d], nc);
        }
        return nid;
    }
    int32_t leaf = art_leaf_chain(kv, key, klen, d + 1, off);
    if (leaf < 0)
        return -1;
    if ((h->type == ART_N4 && h->count >= 4) ||
        (h->type == ART_N16 && h->count >= 16) ||
        (h->type == ART_N48 && h->count >= 48)) {
        int32_t g = art_grow(kv, h);
        if (g < 0)
            return -1;
        art_add(kv, art_hdr(kv, g), key[d], leaf);
        return g;
    }
    art_add(kv, h, key[d], leaf);
    return nid;
}

static int32_t art_del(pathspace_t *kv, int32_t nid, const uint8_t *key, int klen,
                       int d, bool *del) {
    if (nid < 0)
        return -1;
    art_hdr_t *h = art_hdr(kv, nid);
    if (!h)
        return -1;
    if (h->prefix_len) {
        if (pfx_shared(h->prefix, h->prefix_len, key + d, klen - d) !=
            h->prefix_len)
            return nid;
        d += h->prefix_len;
    }
    if (d == klen) {
        uint64_t hv = art_val_load(h);
        if (!AV_HAS(hv))
            return nid;
        uint64_t gidx;
        if (garb_reserve(kv, &gidx) != 0)
            return -1;                  
        if (!art_val_cas(h, &hv, 0)) {
            garb_cancel(kv, gidx);
            return nid;
        }
        gen_bump_value(kv, art_id_of(kv, h)); 
        gen_bump_struct(kv, art_id_of(kv, h)); 
        garb_commit(kv, gidx, AV_OFF(hv),
                    sbo_allocated_size(kv->sbo_meta, AV_OFF(hv)));
        *del = true;
    } else {
        int32_t cid = art_child(kv, h, key[d]);
        if (cid >= 0) {
            int32_t rc = art_del(kv, cid, key, klen, d + 1, del);
            if (rc < 0)
                return rc;
        }
    }
    return nid;
}

static char *pjoin(const char *a, const char *b) {
    size_t al = strlen(a), bl = strlen(b);
    int sep = (al > 0 && a[al - 1] != '/') ? 1 : 0;
    char *r = malloc(al + sep + bl + 1);
    memcpy(r, a, al);
    if (sep)
        r[al] = '/';
    memcpy(r + al + sep, b, bl + 1);
    return r;
}
static void psplit(const char *k, char **p, char **n) {
    const char *s = strrchr(k, '/');
    if (!s || s == k) {
        *p = strdup("/");
        *n = strdup(s ? s + 1 : k);
        return;
    }
    size_t pl = (size_t)(s - k) + 1; 
    *p = malloc(pl + 1);
    memcpy(*p, k, pl);
    (*p)[pl] = '\0';
    *n = strdup(s + 1);
}

static int bad_dir_prefix(const char *p) {
    if (!p || !p[0])
        return 1;
    if (strcmp(p, "/") == 0)
        return 0;
    size_t l = strlen(p);
    if (p[l - 1] == '/')
        return 0;
    if (l >= 2 && (unsigned char)p[l - 2] == 0xC2 &&
        (unsigned char)p[l - 1] == 0xB7)
        return 0;
    return 1;
}

static char *edir(const char *p) {
    size_t l = strlen(p);
    if (l > 0 && p[l - 1] == '/')
        return strdup(p);
    char *r = malloc(l + 2);
    memcpy(r, p, l);
    r[l] = '/';
    r[l + 1] = '\0';
    return r;
}

static int32_t art_cover(pathspace_t *kv, int32_t nid, const uint8_t *pfx,
                         int plen, char *buf, int bcap, int *bpos) {
    int d = 0;
    if (nid < 0 || !pfx || plen < 0 || !buf || !bpos)
        return -1;
    while (nid >= 0) {
        art_hdr_t *h = art_hdr(kv, nid);
        if (!h)
            return -1;
        int pd = d, cover = 0;
        if (h->prefix_len) {
            int s = pfx_shared(h->prefix, h->prefix_len, pfx + d, plen - d);
            if (s < h->prefix_len) {
                if (d + s < plen)
                    return -1;
                cover = 1;
            } else
                d += h->prefix_len;
        }
        if (cover || d == plen) {
            if (pd >= bcap)
                return -1;
            if (pd)
                memcpy(buf, pfx, (size_t)pd);
            *bpos = pd;
            return nid;
        }
        nid = art_child(kv, h, pfx[d]);
        if (nid < 0)
            return -1;
        d++;
    }
    return -1;
}

typedef struct {
    char **keys;
    int32_t count;
    int32_t cap;
} keyscan_t;

static void keyscan_free(keyscan_t *scan) {
    for (int32_t i = 0; i < scan->count; i++)
        free(scan->keys[i]);
    free(scan->keys);
    scan->keys = NULL;
    scan->count = scan->cap = 0;
}

static int keyscan_add_n(keyscan_t *scan, const char *key, size_t len) {
    if (scan->count == INT32_MAX)
        return -1;
    if (scan->count == scan->cap) {
        int32_t cap = scan->cap > INT32_MAX / 2 ? INT32_MAX :
                      scan->cap ? scan->cap * 2 : 64;
        char **keys = realloc(scan->keys, (size_t)cap * sizeof *keys);
        if (!keys)
            return -1;
        scan->keys = keys;
        scan->cap = cap;
    }
    char *copy = strndup(key, len);
    if (!copy)
        return -1;
    scan->keys[scan->count++] = copy;
    return 0;
}

static int child_name_len(const char *rest, int restlen) {
    for (int i = 0; i < restlen; i++) {
        if (rest[i] == '/')
            return i;
        if (i + 1 < restlen && (unsigned char)rest[i] == 0xC2 &&
            (unsigned char)rest[i + 1] == 0xB7)
            return i;
    }
    return restlen;
}

static int art_has_live_value(pathspace_t *kv, int32_t nid) {
    if (nid < 0)
        return 0;
    art_hdr_t *h = art_hdr(kv, nid);
    if (!h)
        return -1;
    if (AV_HAS(art_val_load(h)))
        return 1;
    const int32_t *children;
    int count = h->count;
    switch (h->type) {
    case ART_N4:
        children = ((art_n4_t *)h)->children;
        break;
    case ART_N16:
        children = ((art_n16_t *)h)->children;
        break;
    case ART_N48:
        children = ((art_n48_t *)h)->children;
        break;
    case ART_N256:
        children = ((art_n256_t *)h)->children;
        count = 256;
        break;
    default:
        return 0;
    }
    for (int i = 0; i < count; i++) {
        int live = art_has_live_value(kv, children[i]);
        if (live != 0)
            return live;
    }
    return 0;
}

static int art_scan_children(pathspace_t *kv, int32_t nid, char *buf, int bpos, int bcap,
                             int plen, keyscan_t *scan) {
    if (nid < 0)
        return 0;
    art_hdr_t *h = art_hdr(kv, nid);
    if (!h || h->prefix_len > bcap - bpos - 1)
        return -1;
    
    int start = bpos > plen + 1 ? bpos - plen - 2 : 0;
    for (int i = 0; i < h->prefix_len; i++)
        buf[bpos++] = h->prefix[i];
    buf[bpos] = 0;
    if (bpos > plen) {
        const char *rest = buf + plen;
        int restlen = bpos - plen;
        int len = start + child_name_len(rest + start, restlen - start);
        
        if (len < restlen) {
            if (len == 0)
                return 0;
            int live = art_has_live_value(kv, nid);
            if (live <= 0)
                return live;
            if (rest[len] == '/' && len + 1 == restlen &&
                AV_HAS(art_val_load(h)))
                len++;
            return keyscan_add_n(scan, rest, (size_t)len);
        }
        if (AV_HAS(art_val_load(h)) &&
            keyscan_add_n(scan, rest, (size_t)len) != 0)
            return -1;
    }
    switch (h->type) {
    case ART_N4: {
        art_n4_t *x = (art_n4_t *)h;
        for (int i = 0; i < (int)h->count; i++) {
            if (bpos + 1 >= bcap)
                return -1;
            buf[bpos] = x->keys[i];
            if (art_scan_children(kv, x->children[i], buf, bpos + 1, bcap,
                                  plen, scan) != 0)
                return -1;
        }
        break;
    }
    case ART_N16: {
        art_n16_t *x = (art_n16_t *)h;
        for (int i = 0; i < (int)h->count; i++) {
            if (bpos + 1 >= bcap)
                return -1;
            buf[bpos] = x->keys[i];
            if (art_scan_children(kv, x->children[i], buf, bpos + 1, bcap,
                                  plen, scan) != 0)
                return -1;
        }
        break;
    }
    case ART_N48: {
        art_n48_t *x = (art_n48_t *)h;
        for (int i = 0; i < 256; i++) {
            if (x->index[i] == 255)
                continue;
            if (bpos + 1 >= bcap)
                return -1;
            buf[bpos] = (uint8_t)i;
            if (art_scan_children(kv, x->children[x->index[i]], buf, bpos + 1,
                                  bcap, plen, scan) != 0)
                return -1;
        }
        break;
    }
    case ART_N256: {
        art_n256_t *x = (art_n256_t *)h;
        for (int i = 0; i < 256; i++) {
            if (x->children[i] < 0)
                continue;
            if (bpos + 1 >= bcap)
                return -1;
            buf[bpos] = (uint8_t)i;
            if (art_scan_children(kv, x->children[i], buf, bpos + 1, bcap,
                                  plen, scan) != 0)
                return -1;
        }
        break;
    }
    }
    return 0;
}

static int art_scan_pfx(pathspace_t *kv, const char *pfx, int plen, char *buf,
                        int bcap, keyscan_t *scan) {
    if (plen >= bcap)
        return -1;
    int bpos = 0;
    int32_t nid = art_cover(kv, kv->hdr->art_root, (const uint8_t *)pfx,
                            plen, buf, bcap, &bpos);
    if (nid < 0)
        return 0;
    return art_scan_children(kv, nid, buf, bpos, bcap, plen, scan);
}

static int art_collect_keys(pathspace_t *kv, int32_t nid, char *buf, int bpos,
                            int bcap, keyscan_t *scan) {
    if (nid < 0)
        return 0;
    art_hdr_t *h = art_hdr(kv, nid);
    if (!h || h->prefix_len > bcap - bpos - 1)
        return -1;
    for (int i = 0; i < h->prefix_len; i++)
        buf[bpos++] = h->prefix[i];
    if (AV_HAS(art_val_load(h)) && keyscan_add_n(scan, buf, (size_t)bpos) != 0)
        return -1;
    const uint8_t *keys = NULL;
    const int32_t *children = NULL;
    int n = 0;
    switch (h->type) {
    case ART_N4:
        keys = ((art_n4_t *)h)->keys;
        children = ((art_n4_t *)h)->children;
        n = h->count;
        break;
    case ART_N16:
        keys = ((art_n16_t *)h)->keys;
        children = ((art_n16_t *)h)->children;
        n = h->count;
        break;
    case ART_N48: {
        art_n48_t *x = (art_n48_t *)h;
        for (int i = 0; i < 256; i++) {
            if (x->index[i] == 255)
                continue;
            if (bpos + 1 >= bcap)
                return -1;
            buf[bpos] = (char)i;
            if (art_collect_keys(kv, x->children[x->index[i]], buf, bpos + 1,
                                 bcap, scan) != 0)
                return -1;
        }
        return 0;
    }
    case ART_N256: {
        art_n256_t *x = (art_n256_t *)h;
        for (int i = 0; i < 256; i++) {
            if (x->children[i] < 0)
                continue;
            if (bpos + 1 >= bcap)
                return -1;
            buf[bpos] = (char)i;
            if (art_collect_keys(kv, x->children[i], buf, bpos + 1, bcap,
                                 scan) != 0)
                return -1;
        }
        return 0;
    }
    default:
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (bpos + 1 >= bcap)
            return -1;
        buf[bpos] = (char)keys[i];
        if (art_collect_keys(kv, children[i], buf, bpos + 1, bcap, scan) != 0)
            return -1;
    }
    return 0;
}

static int art_scan_keys(pathspace_t *kv, const char *pfx, int plen,
                         keyscan_t *scan) {
    char buf[4096];
    if (plen >= (int)sizeof buf)
        return -1;
    int bpos = 0;
    int32_t nid = art_cover(kv, kv->hdr->art_root, (const uint8_t *)pfx, plen,
                            buf, (int)sizeof buf, &bpos);
    if (nid < 0)
        return 0;
    return art_collect_keys(kv, nid, buf, bpos, (int)sizeof buf, scan);
}

static bool sbo_data_size_ok(size_t n) {
    for (uint64_t s = 8; s; s *= 64)
        if (s == n)
            return true;
    return false;
}

pathspace_t *pathspaceShmOpen(const char *path, size_t data_size) {
    if (!path)
        return NULL;
    if (!bm_layout_ok()) {
        fprintf(stderr, "pathspace: unexpected blockalloc blocks_meta_t layout\n");
        return NULL;
    }
    char head_path[PATH_MAX], data_path[PATH_MAX], gen_path[PATH_MAX],
        garb_path[PATH_MAX];
    if (snprintf(head_path, sizeof head_path, "%s.sbo.head", path) >=
            (int)sizeof head_path ||
        snprintf(data_path, sizeof data_path, "%s.sbo.data", path) >=
            (int)sizeof data_path ||
        snprintf(gen_path, sizeof gen_path, "%s.gen", path) >=
            (int)sizeof gen_path ||
        snprintf(garb_path, sizeof garb_path, "%s.garb", path) >=
            (int)sizeof garb_path)
        return NULL;

    pathspace_t *kv = calloc(1, sizeof(*kv));
    if (!kv)
        return NULL;
    kv->p2_slot = -1;
    kv->r_art.fd = kv->r_head.fd = kv->r_data.fd = kv->r_gen.fd =
        kv->r_garb.fd = -1;

    bool created = false;
    kv->r_art.fd = open(path, O_RDWR);
    if (kv->r_art.fd < 0) {
        if (!sbo_data_size_ok(data_size))
            goto fail;
        kv->r_art.fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
        if (kv->r_art.fd < 0)
            goto fail;
        created = true;
    }

    size_t art_slab, sbo_head;
    if (created) {
        art_slab = ART_SLAB_INIT;
        sbo_head = sbo_meta_size(data_size, SBO_HEAD_POOL_INIT);
    } else {
        pathspace_hdr_t tmp;
        if (pread(kv->r_art.fd, &tmp, sizeof tmp, 0) != (ssize_t)sizeof tmp ||
            memcmp(tmp.magic, KVS_MAGIC, sizeof(KVS_MAGIC) - 1) != 0)
            goto fail;
        
        if (tmp.format_version != PATHSPACE_FORMAT_VERSION) {
            fprintf(stderr, "pathspace: unsupported store format (version=%u, "
                            "expected %u): %s; rebuild the shm store\n",
                    tmp.format_version, PATHSPACE_FORMAT_VERSION, path);
            goto fail;
        }
        art_slab = (size_t)tmp.art_slab_size;
        sbo_head = (size_t)tmp.sbo_head_size;
        data_size = (size_t)tmp.sbo_data_size;
    }

    int fl = created ? O_RDWR | O_CREAT | O_TRUNC : O_RDWR;
    if (region_attach(&kv->r_art, created, ART_OFF + art_slab,
                      ART_RESERVE) != 0 ||
        (kv->r_head.fd = open(head_path, fl, 0644)) < 0 ||
        region_attach(&kv->r_head, created, sbo_head, REGION_RESERVE) != 0 ||
        (kv->r_data.fd = open(data_path, fl, 0644)) < 0 ||
        region_attach(&kv->r_data, created, data_size, DATA_RESERVE) != 0 ||
        (kv->r_gen.fd = open(gen_path, fl, 0644)) < 0 ||
        (kv->r_garb.fd = open(garb_path, fl, 0644)) < 0 ||
        region_attach(&kv->r_garb, created,
                      (size_t)GARB_CAP * sizeof(garb_ent_t),
                      (size_t)GARB_CAP * sizeof(garb_ent_t)) != 0)
        goto fail;

    kv->hdr = (pathspace_hdr_t *)kv->r_art.base;
    kv->art_meta = (blocks_meta_t *)(kv->r_art.base + sizeof(pathspace_hdr_t));
    kv->art_data = kv->r_art.base + ART_OFF;
    kv->sbo_meta = (sbo_meta_t *)kv->r_head.base;
    kv->sbo_data = kv->r_data.base;
    kv->garb = (garb_ent_t *)kv->r_garb.base;

    {
        static uint32_t store_seq;
        kv->store_id = ((uint64_t)getpid() << 32) | (uint64_t)++store_seq;
    }

    if (created) {
        memset(kv->hdr, 0, sizeof(*kv->hdr));
        kv->hdr->format_version = PATHSPACE_FORMAT_VERSION;
        kv->hdr->garb_cap = GARB_CAP;
        kv->hdr->art_slab_size = art_slab;
        kv->hdr->sbo_head_size = sbo_head;
        kv->hdr->sbo_data_size = data_size;
        kv->hdr->art_root = -1;
        if (blocks_init(kv->art_meta, art_slab, ART_BLOCK_SZ) != 0 ||
            sbo_init(kv->sbo_meta, sbo_head, data_size) != 0)
            goto fail;
        
        kv->art_meta->sizeof_block_head = 8;
        if ((kv->art_meta->sizeof_block_head +
             kv->art_meta->block_size) % 8 != 0)
            goto fail;
        
        memcpy(kv->hdr->magic, KVS_MAGIC, sizeof(KVS_MAGIC) - 1);
    }
    if (!created &&
        (kv->hdr->format_version != PATHSPACE_FORMAT_VERSION ||
         kv->art_meta->sizeof_block_head != 8 ||
         kv->hdr->garb_cap != GARB_CAP)) {
        fprintf(stderr, "pathspace: unsupported store format (version=%u, "
                        "garb_cap=%llu); rebuild the shm store\n",
                kv->hdr->format_version, (unsigned long long)kv->hdr->garb_cap);
        goto fail;
    }
    if (gens_grow(kv) != 0)   
        goto fail;

    if (kv->sbo_meta->root_slots != 1)
        goto fail;

    kv->p2_slot = p2_reg_claim(kv);
    if (kv->p2_slot == -2) {
        fprintf(stderr, "pathspace: too many processes, N=%d\n", P2_SLOT_N);
        goto fail;
    }
    if (kv->p2_slot == -3) {
        fprintf(stderr, "pathspace: claim_id exhausted (2^30 opens): "
                        "rebuild the store\n");
        goto fail;
    }
    if (kv->p2_slot < 0)
        goto fail;
    kv->pid = (uint32_t)getpid();
    (void)pthread_once(&g_atfork_once, p2_atfork_install);
    kv->fork_gen = __atomic_load_n(&g_fork_gen, __ATOMIC_ACQUIRE);
    kv->quiesced = 1;               
    if (kv->hdr->committed_bytes -
            __atomic_load_n(&kv->hdr->freed_bytes, __ATOMIC_ACQUIRE) >=
            p2_garb_threshold() &&
        p2_maint_start(kv) != 0)
        goto fail;                  

    return kv;

fail:
    p2_maint_stop(kv);
    if (kv->p2_slot >= 0 && kv->hdr)
        p2_reg_release(kv);
    region_close(&kv->r_art);
    region_close(&kv->r_head);
    region_close(&kv->r_data);
    region_close(&kv->r_gen);
    region_close(&kv->r_garb);
    free(kv);
    if (created) {
        unlink(path);
        unlink(head_path);
        unlink(data_path);
        unlink(gen_path);
        unlink(garb_path);
    }
    return NULL;
}

static bool p2_other_alive(pathspace_t *kv) {
    for (int i = 0; i < P2_SLOT_N; i++) {
        if (i == kv->p2_slot)
            continue;
        p2_slot_t *s = &kv->hdr->slots[i];
        if (__atomic_load_n(&s->state, __ATOMIC_ACQUIRE) != P2_SLOT_MINE)
            continue;
        if (p2_slot_alive(s))
            return true;
    }
    return false;
}

void pathspaceShmClose(pathspace_t *kv) {
    if (!kv)
        return;
    
    if (kv->pid == (uint32_t)getpid() && kv->p2_slot >= 0 &&
        !p2_other_alive(kv)) {
#if defined(__linux__)
        __atomic_store_n(&kv->hdr->slots[kv->p2_slot].quiesced, 1u,
                         __ATOMIC_RELEASE);
        (void)p2_round(kv);
#else
        uint64_t h = __atomic_load_n(&kv->hdr->garb_head, __ATOMIC_ACQUIRE);
        uint64_t t = __atomic_load_n(&kv->hdr->garb_tail, __ATOMIC_ACQUIRE);
        for (; h < t; h++) {
            garb_ent_t *e = &kv->garb[h & (GARB_CAP - 1)];
            uint32_t m = __atomic_load_n(&e->meta, __ATOMIC_ACQUIRE);
            if (GE_STATE(m) == GE_COMMITTED) {
                uint64_t off = __atomic_load_n(&e->off, __ATOMIC_ACQUIRE);
                uint64_t cap = sbo_allocated_size(kv->sbo_meta, off);
                __atomic_store_n(&e->meta, GE_EMPTY, __ATOMIC_RELEASE);
                kv_sbo_free(kv, off);
                __atomic_store_n(&kv->hdr->freed_bytes,
                                 kv->hdr->freed_bytes + cap, __ATOMIC_RELEASE);
            } else {
                __atomic_store_n(&e->meta, GE_EMPTY, __ATOMIC_RELEASE);
            }
        }
        __atomic_store_n(&kv->hdr->garb_head, t, __ATOMIC_RELEASE);
#endif
    }
    p2_maint_stop(kv);
    p2_reg_release(kv);
    region_close(&kv->r_art);
    region_close(&kv->r_head);
    region_close(&kv->r_data);
    region_close(&kv->r_gen);
    region_close(&kv->r_garb);
    free(kv);
}

static int parse_value(const uint8_t *s, uint64_t bound, int32_t *ol) {
    if (bound > INT32_MAX)
        bound = INT32_MAX;
    pathspaceXvalueHead wire;
    if (head_of(s, (int32_t)bound, &wire) != 0)
        return -1;
    const uint64_t total = pathspaceXvalueHeadTotalLen(&wire);
    if (total == 0 || total > bound)
        return -1;
    *ol = (int32_t)total;
    return 0;
}

static int read_value(pathspace_t *kv, uint64_t off, uint8_t **out, int32_t *ol) {
    *out = kv->sbo_data + off;
    return parse_value(*out, sbo_allocated_size(kv->sbo_meta, off), ol);
}

static uint64_t ro_bound(const pathspace_t *kv, uint64_t off) {
    uint64_t m = kv->r_data.mapped;
    return off < m ? m - off : 0;
}
static int read_value_ro(pathspace_t *kv, uint64_t off, uint8_t **out,
                         int32_t *ol) {
    *out = kv->sbo_data + off;
    return parse_value(*out, ro_bound(kv, off), ol);
}

static art_hdr_t *resolve_fetch(pathspace_t *kv, const char *path, char *out, int osz,
                                uint8_t **raw, int32_t *rl, int *fetched) {
    *fetched = 0;
    strncpy(out, path, osz - 1);
    out[osz - 1] = '\0';
    for (int depth = 0; depth < 16; depth++) {
        art_hdr_t *h =
            art_search(kv, kv->hdr->art_root, (const uint8_t *)out, (int)strlen(out));
        if (!h)
            return h;
        uint64_t hv = art_val_load(h);
        if (!AV_HAS(hv))
            return h;
        uint8_t *s = kv->sbo_data + AV_OFF(hv);
        uint64_t sz = ro_bound(kv, AV_OFF(hv));
        pathspaceXvalueHead wire;
        if (head_of(s, sz > INT32_MAX ? INT32_MAX : (int32_t)sz, &wire) == 0) {
            uint64_t l3 = pathspaceXvalueHeadLen(&wire);
            if (pathspace_c_reference_of(s, sz) != PATHSPACE_XVALUE_REFERENCE_PTR ||
                l3 >= (uint64_t)osz) {
                if (parse_value(s, sz, rl) != 0)
                    return NULL;
                *raw = s;
                *fetched = 1;
                return h;
            }
            memcpy(out, s + PATHSPACE_XVALUE_HEAD_LEN, (size_t)l3);
            out[l3] = 0;
            continue;
        }
        return NULL;
    }
    return NULL;
}

static void resolve_path(pathspace_t *kv, const char *path, char *out, int osz) {
    uint8_t *raw;
    int32_t rl;
    int fetched;
    (void)resolve_fetch(kv, path, out, osz, &raw, &rl, &fetched);
}

static int dir_ext_path(pathspace_t *kv, const char *dir, char *out, int osz) {
    out[0] = 0;
    art_hdr_t *h =
        art_search(kv, kv->hdr->art_root, (const uint8_t *)dir, (int)strlen(dir));
    if (!h)
        return 0;
    uint64_t hv = art_val_load(h);
    if (!AV_HAS(hv))
        return 0;
    uint8_t *raw;
    int32_t rl;
    if (read_value_ro(kv, AV_OFF(hv), &raw, &rl) < 0)
        return 0;
    pathspaceXvalueHead wire;
    if (head_of(raw, rl, &wire) != 0 ||
        pathspace_c_reference_of(raw, (uint64_t)rl) != PATHSPACE_XVALUE_REFERENCE_EXT)
        return 0;
    uint64_t l4 = pathspaceXvalueHeadLen(&wire);
    if (l4 == 0 || l4 >= (uint64_t)osz)
        return 0;
    memcpy(out, raw + PATHSPACE_XVALUE_HEAD_LEN, (size_t)l4);
    out[l4] = 0;
    return 1;
}

uint8_t *pathspaceShmGet(pathspace_t *kv, const char *key, int resolve,
                       int32_t *ol) {
    if (!kv || !key || !ol)
        return NULL;
    *ol = 0;
    if (reserved_meta_path(key))
        return NULL;
    if (kv_sync(kv) != 0)
        return NULL;
    char kbuf[1024];
    
    if (strlen(key) >= sizeof kbuf)
        return NULL;
    uint8_t *raw;
    int32_t rl;
    int fetched = 0;
    art_hdr_t *h;
    if (resolve)
        h = resolve_fetch(kv, key, kbuf, sizeof(kbuf), &raw, &rl, &fetched);
    else {
        strncpy(kbuf, key, sizeof(kbuf) - 1);
        kbuf[sizeof(kbuf) - 1] = '\0';
        h = art_search(kv, kv->hdr->art_root, (const uint8_t *)kbuf,
                       (int)strlen(kbuf));
    }
    uint64_t hv = h ? art_val_load(h) : 0;
    if (!h || !AV_HAS(hv)) {
        
        char *parent = NULL, *name = NULL;
        psplit(kbuf, &parent, &name);
        char extpath[1024];
        if (dir_ext_path(kv, parent, extpath, sizeof extpath)) {
            char *target = pjoin(extpath, name);
            art_hdr_t *eh = art_search(kv, kv->hdr->art_root, (const uint8_t *)target,
                                       (int)strlen(target));
            free(target);
            uint64_t ehv = eh ? art_val_load(eh) : 0;
            if (eh && AV_HAS(ehv)) {
                uint8_t *raw;
                int32_t rl;
                if (read_value_ro(kv, AV_OFF(ehv), &raw, &rl) == 0) {
                    *ol = rl;
                    free(parent);
                    free(name);
                    return raw;
                }
            }
        }
        free(parent);
        free(name);
        return NULL;
    }
    if (!fetched && read_value_ro(kv, AV_OFF(hv), &raw, &rl) < 0)
        return NULL;
    *ol = rl;
    return raw;
}

int pathspaceShmSetPart(pathspace_t *kv, const char *key, uint32_t offset,
                      const uint8_t *buf, uint32_t buf_len) {
    if (!kv || !key || (buf_len && !buf))
        return -1;
    int32_t rl = 0;
    uint8_t *d = pathspaceShmGet(kv, key, 1, &rl);
    uint64_t bound = 0U;
    if (!d || read_value(kv, (uint64_t)(d - kv->sbo_data), &d, &rl) != 0 ||
        pathspace_c_effective_len(d, (uint64_t)rl, &bound) != 0 ||
        (uint64_t)offset + buf_len > (uint64_t)PATHSPACE_XVALUE_HEAD_LEN + bound)
        return -1;
    memcpy(d + offset, buf, buf_len);
    return 0;
}

#define PATHSPACE_STATUS_MAX_LEN 16u
#define PATHSPACE_STATUS_MIN_CAP 80u

static int is_vthread_status_key(const char *key) {
    static const char pfx[] = "/vthread/";
    static const char suf[] = "/\xE2\x80\xA5status";
    if (!key)
        return 0;
    size_t n = strlen(key), sl = sizeof suf - 1;
    return n > sl && strncmp(key, pfx, sizeof pfx - 1) == 0 &&
           memcmp(key + n - sl, suf, sl) == 0;
}

static int is_char_langtype(const uint8_t *langtype, uint16_t length) {
    if (!langtype || length == 0)
        return 0;
    uint16_t at = 0;
    if (langtype[0] == '[') {
        while (at < length && langtype[at] != ']')
            ++at;
        if (at >= length)
            return 0;
        ++at;
    }
    return (uint16_t)(length - at) >= 5 &&
           memcmp(langtype + at, "char/", 5) == 0;
}

static int shm_set_local(pathspace_t *kv, const char *key, const uint8_t *val,
                         int32_t val_len, art_hdr_t *old, size_t reserve) {
    size_t alloc_len = (size_t)val_len;
    if (reserve > alloc_len)
        alloc_len = reserve;
    if (is_vthread_status_key(key) && alloc_len < PATHSPACE_STATUS_MIN_CAP)
        alloc_len = PATHSPACE_STATUS_MIN_CAP;
    uint64_t ohv = old ? art_val_load(old) : 0;
    bool retiring = old && AV_HAS(ohv);
    uint64_t gidx = 0;
    uint64_t cap = 0;
    if (retiring) {
        cap = sbo_allocated_size(kv->sbo_meta, AV_OFF(ohv));
        if ((uint64_t)alloc_len <= cap) {
            
            kv_gens_t *g = gens_at(kv, art_id_of(kv, old));
            if (g)
                __atomic_add_fetch(&g->value_gen, 1u, __ATOMIC_ACQ_REL);
            memcpy(kv->sbo_data + AV_OFF(ohv), val, (size_t)val_len);
            if (g)
                __atomic_add_fetch(&g->value_gen, 1u, __ATOMIC_ACQ_REL);
            return 0;
        }
        
        if (garb_reserve(kv, &gidx) != 0)
            return -1;
#ifdef P2_TEST_HOOKS
        if (g_p2_test_knobs.hold_reserve_ms)
            usleep((useconds_t)g_p2_test_knobs.hold_reserve_ms * 1000);
        if (g_p2_test_knobs.fail_alloc) {
            garb_cancel(kv, gidx);
            return -1;
        }
#endif
    }
    uint64_t off = kv_sbo_alloc(kv, alloc_len);
    if (off == (uint64_t)-1) {
        if (retiring)
            garb_cancel(kv, gidx);
        return -1;
    }
    memcpy(kv->sbo_data + off, val, val_len);
#ifdef P2_TEST_HOOKS
    if (retiring && g_p2_test_knobs.fail_art_insert) {
        garb_cancel(kv, gidx);
        kv_sbo_free(kv, off);
        return -1;
    }
#endif
    int32_t root = art_ins(kv, kv->hdr->art_root, (const uint8_t *)key,
                           (int)strlen(key), 0, off);
    if (root < 0) {
        if (retiring)
            garb_cancel(kv, gidx);
        kv_sbo_free(kv, off);          
        return -1;
    }
    kv->hdr->art_root = root;
    {   
        int32_t nid = p2_node_at(kv, key);
        kv_gens_t *g = gens_at(kv, nid);
        if (g) {
            gen_bump_struct(kv, nid);
            gen_bump_value(kv, nid);
        }
    }
    if (retiring)
        garb_commit(kv, gidx, AV_OFF(ohv), cap);
    else
        list_gen_bump(kv);   
    return 0;
}

static int shm_set_raw(pathspace_t *kv, const char *key, const uint8_t *val,
                       int32_t val_len, size_t reserve) {
    art_hdr_t *old = art_search(kv, kv->hdr->art_root, (const uint8_t *)key,
                                (int)strlen(key));
    return shm_set_local(kv, key, val, val_len, old, reserve);
}

static int frame_operand_ptr(const char *key, const uint8_t *wire_bytes,
                             uint64_t wire_len) {
    if (pathspace_c_reference_of(wire_bytes, wire_len) !=
            PATHSPACE_XVALUE_REFERENCE_PTR ||
        strncmp(key, "/vthread/", 9) != 0)
        return 0;
    const char *slot = strrchr(key, '/');
    if (!slot || slot[1] != '[')
        return 0;
    char *end;
    long row = strtol(slot + 2, &end, 10);
    if (row <= 0 || *end++ != ',')
        return 0;
    long col = strtol(end, &end, 10);
    return col != 0 && *end++ == ']' && *end == '\0';
}

static int shm_set_validated(pathspace_t *kv, const char *key,
                            const uint8_t *val, int32_t val_len,
                            const pathspaceXvalueHead *wire) {
    if (kv_sync(kv) != 0)
        return -1;
    char kbuf[1024];
    if (strlen(key) >= sizeof kbuf || strstr(key, "//") ||
        reserved_meta_path(key))
        return -1;
    strcpy(kbuf, key);

    art_hdr_t *local = art_search(kv, kv->hdr->art_root,
                                   (const uint8_t *)kbuf, (int)strlen(kbuf));
    
    uint64_t lhv = local ? art_val_load(local) : 0;
    if (!local || !AV_HAS(lhv)) {
        char *pp = NULL, *nn = NULL;
        psplit(kbuf, &pp, &nn);
        char extpath[1024];
        if (pp && nn && dir_ext_path(kv, pp, extpath, sizeof extpath)) {
            char *tgt = pjoin(extpath, nn);
            art_hdr_t *eh = art_search(kv, kv->hdr->art_root,
                                       (const uint8_t *)tgt, (int)strlen(tgt));
            free(tgt);
            if (eh && AV_HAS(art_val_load(eh)) &&
                !frame_operand_ptr(kbuf, val, (uint64_t)val_len)) {
                free(pp);
                free(nn);
                return -1;
            }
        }
        free(pp);
        free(nn);
    }

    if (is_vthread_status_key(kbuf)) {
        const uint8_t *status_langtype = NULL;
        uint16_t status_langtype_len = 0;
        if (pathspace_c_scan_attrs(val, (uint64_t)val_len, &status_langtype,
                                 &status_langtype_len) != 0)
            return -1;
        uint64_t effective = 0;
        if (pathspaceXvalueHeadEffectiveLen(val, (uint64_t)val_len, &effective) != 0 ||
            effective > PATHSPACE_STATUS_MAX_LEN ||
            !is_char_langtype(status_langtype, status_langtype_len))
            return -1;
        if (local && AV_HAS(lhv)) {
            uint8_t *old_raw = kv->sbo_data + AV_OFF(lhv);
            uint64_t old_sz = sbo_allocated_size(kv->sbo_meta, AV_OFF(lhv));
            pathspaceXvalueHead old;
            
            const uint8_t *old_langtype = NULL;
            uint16_t old_langtype_len = 0;
            if (pathspace_c_scan_attrs(old_raw, (uint64_t)old_sz, &old_langtype,
                                     &old_langtype_len) != 0)
                return -1;
            uint64_t old_effective = 0;
            if (head_of(old_raw, (int32_t)old_sz, &old) != 0 ||
                pathspaceXvalueHeadEffectiveLen(old_raw, (uint64_t)old_sz,
                                              &old_effective) != 0 ||
                old_effective > PATHSPACE_STATUS_MAX_LEN ||
                old_sz < PATHSPACE_STATUS_MIN_CAP ||
                !is_char_langtype(old_langtype, old_langtype_len))
                return -1;
        }
    }
    return shm_set_local(kv, kbuf, val, val_len, local, 0);
}

int pathspaceShmSet(pathspace_t *kv, const char *key, const uint8_t *val,
                  int32_t val_len) {
    if (!kv || !key || !val || val_len <= 0)
        return -1;
    pathspaceXvalueHead wire;
    if (value_ok(val, val_len, &wire) != 0)
        return -1;
    return shm_set_validated(kv, key, val, val_len, &wire);
}

int pathspaceShmSetValue(pathspace_t *kv, const char *key, const uint8_t *val,
                       int32_t val_len) {
    pathspaceXvalueHead head;
    if (!kv || !key || !val || val_len <= 0 ||
        value_ok(val, val_len, &head) != 0)
        return -2;
    return shm_set_validated(kv, key, val, val_len, &head);
}

static int shm_del_key(pathspace_t *kv, const char *key) {
    bool d = false;
    int32_t root = art_del(kv, kv->hdr->art_root, (const uint8_t *)key,
                           (int)strlen(key), 0, &d);
    if (root < 0)
        return -1;
    kv->hdr->art_root = root;
    return d ? 1 : 0;
}

int pathspaceShmDel(pathspace_t *kv, const char *key) {
    if (!kv || !key)
        return -1;
    if (kv_sync(kv) != 0)
        return -1;
    if (strlen(key) >= 1024)
        return -1;                  
    
    if (shm_del_key(kv, key) != 1)
        return -1;
    list_gen_bump(kv);              
    return 0;
}

static char *memdir(const char *p) {
    size_t l = strlen(p);
    char *r = malloc(l + 3);
    memcpy(r, p, l);
    r[l] = (char)0xC2;
    r[l + 1] = (char)0xB7;
    r[l + 2] = 0;
    return r;
}

static int subtree_key(const char *base, size_t bl, const char *key) {
    if (bl == 0)
        return key[0] == '/';
    if (strncmp(key, base, bl) != 0)
        return 0;
    const unsigned char *r = (const unsigned char *)key + bl;
    return r[0] == 0 || r[0] == '/' || (r[0] == 0xC2 && r[1] == 0xB7) ||
           (r[0] == 0xE2 && r[1] == 0x80 && r[2] == 0xA5);
}

static int is_ptr_key(pathspace_t *kv, const char *key) {
    art_hdr_t *h = art_search(kv, kv->hdr->art_root, (const uint8_t *)key,
                              (int)strlen(key));
    uint64_t hv = h ? art_val_load(h) : 0;
    if (!h || !AV_HAS(hv))
        return 0;
    uint8_t *raw = NULL;
    int32_t rl = 0;
    pathspaceXvalueHead wire;
    return read_value(kv, AV_OFF(hv), &raw, &rl) == 0 && rl > 0 &&
           head_of(raw, rl, &wire) == 0 &&
           pathspace_c_reference_of(raw, (uint64_t)rl) ==
               PATHSPACE_XVALUE_REFERENCE_PTR;
}

int pathspaceShmDeltree(pathspace_t *kv, const char *prefix) {
    if (!kv || !prefix || reserved_meta_path(prefix))
        return -1;
    if (kv_sync(kv) != 0)
        return -1;
    size_t pl = strlen(prefix);
    if (pl >= 1024)
        return -1;                  
    char base[1024];
    memcpy(base, prefix, pl + 1);
    size_t bl = pl;
    if (strcmp(prefix, "/") == 0)
        bl = 0;
    else if (bl && base[bl - 1] == '/')
        bl--;
    else if (bl >= 2 && (unsigned char)base[bl - 2] == 0xC2 &&
             (unsigned char)base[bl - 1] == 0xB7)
        bl -= 2;
    base[bl] = 0;
    if (bl && reserved_meta_path(base))
        return -1;
    
    if (pl && is_ptr_key(kv, prefix))
        return pathspaceShmDel(kv, prefix);
    if (bl && bl != pl && is_ptr_key(kv, base)) {
        int rc = pathspaceShmDel(kv, base);
        pathspaceShmDel(kv, prefix);  
        return rc;
    }
    keyscan_t keys = {0};
    if (art_scan_keys(kv, bl ? base : "/", bl ? (int)bl : 1, &keys) != 0) {
        keyscan_free(&keys);
        return -1;
    }
    int rc = 0, any = 0;
    for (int32_t i = 0; i < keys.count; i++) {
        const char *k = keys.keys[i];
        if (!subtree_key(base, bl, k) || reserved_meta_path(k) ||
            strlen(k) >= 1024)
            continue;
        int d = shm_del_key(kv, k);
        if (d < 0)
            rc = -1;
        else if (d)
            any = 1;
    }
    keyscan_free(&keys);
    if (any)
        list_gen_bump(kv);          
    return rc;
}

int pathspaceShmCp(pathspace_t *kv, const char *src, const char *dst) {
    if (!kv || !src || !dst)
        return -1;
    int32_t rl;
    uint8_t *raw = pathspaceShmGet(kv, src, 0, &rl);
    if (!raw || rl <= 0)
        return -1;
    uint8_t *tmp = malloc((size_t)rl);
    if (!tmp)
        return -1;
    memcpy(tmp, raw, (size_t)rl);
    int rc = pathspaceShmSet(kv, dst, tmp, rl);
    free(tmp);
    return rc;
}

static int cptree_rec(pathspace_t *kv, const char *src, const char *dst) {
    {
        int32_t rl;
        uint8_t *raw = pathspaceShmGet(kv, src, 0, &rl);
        if (raw && rl > 0) {
            if (pathspaceShmCp(kv, src, dst) != 0)
                return -1;
        }
    }
    char *es = edir(src), *ed = edir(dst);
    char **ns;
    int32_t nc;
    if (pathspaceShmList(kv, es, false, 1, &ns, &nc) != 0) {
        free(es);
        free(ed);
        return -1;
    }
    int rc = 0;
    for (int i = 0; i < nc; i++) {
        char *cs = pjoin(es, ns[i]), *cd = pjoin(ed, ns[i]);
        if (!cs || !cd || cptree_rec(kv, cs, cd) != 0)
            rc = -1;
        free(cs);
        free(cd);
        if (rc != 0)
            break;
    }
    for (int i = 0; i < nc; i++)
        free(ns[i]);
    free(ns);
    free(es);
    free(ed);
    if (rc != 0)
        return -1;

    char *ms = memdir(src), *md = memdir(dst);
    char **mms;
    int32_t mc;
    if (pathspaceShmList(kv, ms, false, 1, &mms, &mc) != 0) {
        free(ms);
        free(md);
        return -1;
    }
    for (int i = 0; i < mc; i++) {
        size_t msl = strlen(ms), mdl = strlen(md), nl = strlen(mms[i]);
        char *cs = malloc(msl + nl + 1);
        char *cd = malloc(mdl + nl + 1);
        if (!cs || !cd) {
            rc = -1;
        } else {
            memcpy(cs, ms, msl);
            memcpy(cs + msl, mms[i], nl + 1);
            memcpy(cd, md, mdl);
            memcpy(cd + mdl, mms[i], nl + 1);
            if (cptree_rec(kv, cs, cd) != 0)
                rc = -1;
        }
        free(cs);
        free(cd);
        if (rc != 0)
            break;
    }
    for (int i = 0; i < mc; i++)
        free(mms[i]);
    free(mms);
    free(ms);
    free(md);
    return rc;
}

int pathspaceShmCptree(pathspace_t *kv, const char *src, const char *dst) {
    if (!kv || !src || !dst || reserved_meta_path(src) ||
        reserved_meta_path(dst))
        return -1;
    size_t sl = strlen(src);
    while (sl > 1 && src[sl - 1] == '/')
        sl--;
    if (sl == 1 && src[0] == '/' && dst[0] == '/')
        return dst[1] ? -1 : 0;
    if (strncmp(src, dst, sl) == 0) {
        if (dst[sl] == 0 || (dst[sl] == '/' && dst[sl + 1] == 0))
            return 0;
        if (dst[sl] == '/' ||
            ((unsigned char)dst[sl] == 0xc2 &&
             (unsigned char)dst[sl + 1] == 0xb7))
            return -1;
    }
    pathspaceShmDeltree(kv, dst);
    return cptree_rec(kv, src, dst);
}

int pathspaceShmCplist(pathspace_t *kv, const char *src, const char *dst) {
    if (!kv || !src || !dst || reserved_meta_path(src) ||
        reserved_meta_path(dst))
        return -1;
    if (strcmp(src, dst) == 0)
        return 0;
    pathspaceShmDeltree(kv, dst);
    int32_t rl;
    uint8_t *raw = pathspaceShmGet(kv, src, 0, &rl);
    if (raw && rl > 0 && pathspaceShmCp(kv, src, dst) != 0)
        return -1;
    char *ms = memdir(src), *md = memdir(dst);
    char **mms;
    int32_t mc;
    if (pathspaceShmList(kv, ms, false, 1, &mms, &mc) != 0) {
        free(ms);
        free(md);
        return -1;
    }
    size_t msl = strlen(ms), mdl = strlen(md);
    int rc = 0;
    for (int i = 0; i < mc; i++) {
        size_t nl = strlen(mms[i]);
        char *cs = malloc(msl + nl + 1);
        char *cd = malloc(mdl + nl + 1);
        if (!cs || !cd) {
            rc = -1;
        } else {
            memcpy(cs, ms, msl);
            memcpy(cs + msl, mms[i], nl + 1);
            memcpy(cd, md, mdl);
            memcpy(cd + mdl, mms[i], nl + 1);
            if (pathspaceShmCp(kv, cs, cd) != 0)
                rc = -1;
        }
        free(cs);
        free(cd);
        if (rc != 0)
            break;
    }
    for (int i = 0; i < mc; i++)
        free(mms[i]);
    free(mms);
    free(ms);
    free(md);
    return rc;
}

static int same_child(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    if (na && a[na - 1] == '/') na--;
    if (nb && b[nb - 1] == '/') nb--;
    return na == nb && memcmp(a, b, na) == 0;
}

static int list_target(pathspace_t *kv, const char *prefix, bool ex, int resolve,
                       char *target, size_t tcap, char *ext, size_t ecap) {
    ext[0] = 0;
    if (reserved_meta_path(prefix))
        return 1;
    if (kv_sync(kv) != 0 || bad_dir_prefix(prefix))
        return -1;
    if (resolve) {
        if (strlen(prefix) >= 1024)
            return -1;              
        resolve_path(kv, prefix, target, 1024);
    } else {
        size_t l = strlen(prefix);
        if (l >= tcap)
            return -1;
        memcpy(target, prefix, l + 1);
    }
    if (reserved_meta_path(target))
        return 1;
    if (ex) {
        char *dir = edir(target);
        if (!dir)
            return -1;
        if (!dir_ext_path(kv, dir, ext, (int)ecap))
            ext[0] = 0;
        free(dir);
    }
    return 0;
}

static int list_scan(pathspace_t *kv, const char *pfx, const char *ext,
                     char ***on, int32_t *oc) {
    keyscan_t children = {0};
    char buf[4096];
    if (kv->hdr->art_root >= 0 &&
        art_scan_pfx(kv, pfx, (int)strlen(pfx), buf, (int)sizeof buf, &children) != 0)
        goto fail;
    if (ext[0] && art_scan_pfx(kv, ext, (int)strlen(ext), buf, (int)sizeof buf,
                               &children) != 0)
        goto fail;
    
    int32_t keep = 0;
    if (children.count > 1) {
        uint32_t buckets = 8u;
        while (buckets < (uint32_t)children.count * 2u)
            buckets <<= 1u;
        int32_t *table = (int32_t *)malloc((size_t)buckets * sizeof *table);
        if (!table)
            goto fail;
        
        int32_t *next = (int32_t *)malloc((size_t)children.count * sizeof *next);
        if (!next) {
            free(table);
            goto fail;
        }
        for (uint32_t b = 0; b < buckets; b++)
            table[b] = -1;
        for (int32_t i = 0; i < children.count; i++) {
            const char *name = children.keys[i];
            
            if ((strcmp(pfx, "/") == 0 && strcmp(name, ".pathspace-meta") == 0) ||
                strstr(name, "\xE2\x80\xA5") != NULL) {
                free(children.keys[i]);
                continue;
            }
            const size_t name_len = strlen(name);
            
            size_t base_len = name_len;
            if (base_len && name[base_len - 1] == '/')
                base_len--;
            uint32_t hash = 2166136261u; 
            for (size_t c = 0; c < base_len; c++)
                hash = (hash ^ (unsigned char)name[c]) * 16777619u;
            const uint32_t bucket = hash & (buckets - 1u);
            int32_t twin = -1;
            for (int32_t slot = table[bucket]; slot >= 0; slot = next[slot]) {
                if (same_child(children.keys[slot], name)) {
                    twin = slot;
                    break;
                }
            }
            if (twin >= 0) {
                
                if (name[name_len - 1] == '/') {
                    free(children.keys[twin]);
                    children.keys[twin] = children.keys[i];
                } else {
                    free(children.keys[i]);
                }
                continue;
            }
            children.keys[keep] = children.keys[i];
            next[keep] = table[bucket];
            table[bucket] = keep;
            keep++;
        }
        free(next);
        free(table);
        children.count = keep;
        *on = children.keys;
        *oc = children.count;
        return 0;
    }
    
    for (int32_t i = 0; i < children.count; i++) {
        const char *name = children.keys[i];
        const int hidden = (strcmp(pfx, "/") == 0 &&
                            strcmp(name, ".pathspace-meta") == 0) ||
                           strstr(name, "\xE2\x80\xA5") != NULL;
        if (hidden) {
            free(children.keys[i]);
            continue;
        }
        children.keys[keep++] = children.keys[i];
    }
    children.count = keep;
    *on = children.keys;
    *oc = children.count;
    return 0;
fail:
    keyscan_free(&children);
    return -1;
}
int pathspaceShmList(pathspace_t *kv, const char *prefix, bool ex, int resolve,
                   char ***on, int32_t *oc) {
    if (!kv || !prefix || !on || !oc)
        return -1;
    *on = NULL;
    *oc = 0;
    char target[4096], ext[1024];
    int rc = list_target(kv, prefix, ex, resolve, target, sizeof target, ext,
                         sizeof ext);
    if (rc != 0)
        return rc > 0 ? 0 : -1;
    return list_scan(kv, target, ext, on, oc);
}

int pathspaceShmListLen(pathspace_t *kv, const char *prefix, bool ex, int resolve,
                      int32_t *out_count) {
    if (!out_count)
        return -1;
    *out_count = 0;
    if (!kv || !prefix)
        return -1;
    char target[4096], ext[1024];
    int rc = list_target(kv, prefix, ex, resolve, target, sizeof target, ext,
                         sizeof ext);
    if (rc != 0)
        return rc > 0 ? 0 : -1;
    char **names = NULL;
    int32_t count = 0;
    if (list_scan(kv, target, ext, &names, &count) != 0)
        return -1;
    for (int32_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
    *out_count = count;
    return 0;
}

static int storage_locate(pathspace_t *kv, const char *key, int32_t *out_id,
                          uint64_t *out_offset) {
    art_hdr_t *h = art_search(kv, kv->hdr->art_root, (const uint8_t *)key,
                              (int)strlen(key));
    if (!h)
        return PATHSPACE_MISSING;
    uint64_t hv = art_val_load(h);
    if (!AV_HAS(hv))
        return PATHSPACE_MISSING;
    *out_id = art_id_of(kv, h);
    *out_offset = AV_OFF(hv);
    return PATHSPACE_OK;
}

static int anchor_from_offset(pathspace_t *kv, int64_t key_offset,
                              pathspaceAnchor_t *out) {
    if (!kv || !out || key_offset <= 0)
        return -1;
    if ((uint64_t)key_offset >= kv->art_meta->total_size)
        return -1;
    int64_t block_id = blockid_bydataoffset(kv->art_meta, (uint64_t)key_offset);
    if (block_id < 0 || (uint64_t)block_id >= kv->art_meta->used_blocks)
        return -1;
    out->key_offset = key_offset;
    out->block_id = (int32_t)block_id;
    return 0;
}

static int storage_anchor_ok(pathspace_t *kv, const pathspaceAnchor_t *anchor) {
    if (!kv || !anchor || anchor->block_id < 0)
        return 0;
    if ((uint64_t)anchor->block_id >= kv->art_meta->used_blocks)
        return 0;
    art_hdr_t *h = art_hdr(kv, anchor->block_id);
    if (!h || h->type == ART_MOVED)
        return 0;
    return 1;
}

int pathspaceShmStorageResolveFrom(pathspace_t *kv, const char *key,
                                 pathspaceAnchor_t anchor, uint64_t *out_offset) {
    int32_t id = -1;
    if (!kv || !key || !out_offset)
        return PATHSPACE_INVALID;
    *out_offset = 0;
    if (reserved_meta_path(key))
        return PATHSPACE_INVALID;
    if (kv_sync(kv) != 0)
        return PATHSPACE_BACKEND;
    if (!storage_anchor_ok(kv, &anchor))
        return PATHSPACE_ANCHOR_INVALID;
    return storage_locate(kv, key, &id, out_offset);
}

uint8_t *pathspaceShmStorageDataBase(pathspace_t *kv) {
    return kv ? kv->sbo_data : NULL;
}

int pathspaceShmStorageResolve(pathspace_t *kv, const char *key,
                             uint64_t *out_offset) {
    int32_t id = -1;
    if (!kv || !key || !out_offset)
        return PATHSPACE_INVALID;
    *out_offset = 0;
    if (reserved_meta_path(key))
        return PATHSPACE_INVALID;
    if (kv_sync(kv) != 0)
        return PATHSPACE_BACKEND;
    return storage_locate(kv, key, &id, out_offset);
}

#ifdef P2_TEST_HOOKS

int pathspaceShmTestAnchorOf(pathspace_t *kv, const char *parent_key,
                           int64_t *out_anchor) {
    if (!kv || !parent_key || !out_anchor)
        return PATHSPACE_INVALID;
    
    if (reserved_meta_path(parent_key))
        return PATHSPACE_INVALID;
    if (kv_sync(kv) != 0)
        return PATHSPACE_BACKEND;
    art_hdr_t *h = art_search(kv, kv->hdr->art_root,
                              (const uint8_t *)parent_key,
                              (int)strlen(parent_key));
    if (!h || !AV_HAS(art_val_load(h)))
        return PATHSPACE_MISSING;
    
    int32_t id = art_id_of(kv, h);
    if (id < 0)
        return PATHSPACE_BACKEND;
    if ((uint64_t)id >= kv->art_meta->used_blocks)
        return PATHSPACE_BACKEND;
    if (art_hdr(kv, id)->type == ART_MOVED)
        return PATHSPACE_BACKEND;
    int64_t offset = (int64_t)blockdata_offset(kv->art_meta, (uint64_t)id);
    if (offset <= 0)
        return PATHSPACE_BACKEND;
    *out_anchor = offset;
    return PATHSPACE_OK;
}
#endif

static void storage_write_head(uint8_t *dst, uint64_t body_len,
                               uint64_t attr_len) {
    pathspaceXvalueHead h;
    memset(&h, 0, sizeof h);
    pathspaceXvalueHeadSetLen(&h, body_len);
    pathspaceXvalueHeadSetAttrsLen(&h, (uint16_t)attr_len);
    memcpy(dst, &h, sizeof h);
}

int pathspaceShmStorageRealloc(pathspace_t *kv, const char *key,
                             uint64_t body_len, uint64_t attr_len,
                             uint64_t *out_offset) {
    if (!kv || !key || !out_offset)
        return PATHSPACE_INVALID;
    *out_offset = 0;
    if (reserved_meta_path(key) || strstr(key, "//") ||
        strlen(key) >= 1024)
        return PATHSPACE_INVALID;
    if (attr_len > UINT16_MAX)
        return PATHSPACE_INVALID;
    uint64_t need = PATHSPACE_XVALUE_HEAD_LEN + body_len;
    if (need < body_len || need > UINT64_MAX - attr_len)
        return PATHSPACE_INVALID;
    need += attr_len;
    if (need > INT32_MAX)
        return PATHSPACE_INVALID;
    if (kv_sync(kv) != 0)
        return PATHSPACE_BACKEND;

    art_hdr_t *node = art_search(kv, kv->hdr->art_root, (const uint8_t *)key,
                                 (int)strlen(key));
    uint64_t ohv = node ? art_val_load(node) : 0;
    bool retiring = node && AV_HAS(ohv);
    uint64_t gidx = 0;
    uint64_t old_cap = 0;
    if (retiring) {
        uint64_t off = AV_OFF(ohv);
        old_cap = sbo_allocated_size(kv->sbo_meta, off);
        if (need <= old_cap) {
            
            kv_gens_t *g = gens_at(kv, art_id_of(kv, node));
            if (g)
                __atomic_add_fetch(&g->value_gen, 1u, __ATOMIC_ACQ_REL);
            storage_write_head(kv->sbo_data + off, body_len, attr_len);
            if (g)
                __atomic_add_fetch(&g->value_gen, 1u, __ATOMIC_ACQ_REL);
            *out_offset = off;
            return PATHSPACE_OK;
        }
        if (garb_reserve(kv, &gidx) != 0)
            return PATHSPACE_BACKEND;
    }
    uint64_t off = kv_sbo_alloc(kv, (size_t)need);
    if (off == (uint64_t)-1) {
        if (retiring)
            garb_cancel(kv, gidx);
        return PATHSPACE_BACKEND;
    }
    
    memset(kv->sbo_data + off, 0, (size_t)need);
    storage_write_head(kv->sbo_data + off, body_len, attr_len);
    int32_t root = art_ins(kv, kv->hdr->art_root, (const uint8_t *)key,
                           (int)strlen(key), 0, off);
    if (root < 0) {
        if (retiring)
            garb_cancel(kv, gidx);
        kv_sbo_free(kv, off);
        return PATHSPACE_BACKEND;
    }
    kv->hdr->art_root = root;
    {
        int32_t nid = p2_node_at(kv, key);
        kv_gens_t *g = gens_at(kv, nid);
        if (g) {
            gen_bump_struct(kv, nid);
            gen_bump_value(kv, nid);
        }
    }
    if (retiring)
        garb_commit(kv, gidx, AV_OFF(ohv), old_cap);
    else
        list_gen_bump(kv);
    *out_offset = off;
    return PATHSPACE_OK;
}

static int storage_is_index_key(const char *key) {
    size_t n = strlen(key);
    if (n == 0)
        return 0;
    if (key[n - 1] == '/')
        return 1;
    return n >= 2 && (unsigned char)key[n - 2] == 0xC2 &&
           (unsigned char)key[n - 1] == 0xB7;
}

static int storage_has_children(pathspace_t *kv, const char *key) {
    keyscan_t children = {0};
    char buf[4096];
    int rc = 0;
    if (kv->hdr->art_root < 0)
        return PATHSPACE_OK;
    if (art_scan_pfx(kv, key, (int)strlen(key), buf, (int)sizeof buf,
                     &children) != 0) {
        keyscan_free(&children);
        return PATHSPACE_BACKEND;
    }
    if (children.count > 0)
        rc = PATHSPACE_HAS_CHILDREN;
    keyscan_free(&children);
    return rc;
}

int pathspaceShmStorageFree(pathspace_t *kv, const char *key) {
    if (!kv || !key)
        return PATHSPACE_INVALID;
    if (reserved_meta_path(key) || strlen(key) >= 1024)
        return PATHSPACE_INVALID;
    if (kv_sync(kv) != 0)
        return PATHSPACE_BACKEND;
    
    if (storage_is_index_key(key)) {
        char *d = edir(key);
        int child_rc = storage_has_children(kv, d);
        free(d);
        if (child_rc != PATHSPACE_OK)
            return child_rc;
    }
    int rc = shm_del_key(kv, key);
    if (rc < 0)
        return PATHSPACE_BACKEND;
    if (rc == 0)
        return PATHSPACE_MISSING;
    list_gen_bump(kv);
    return PATHSPACE_OK;
}

int pathspaceShmStorageMakeindex(pathspace_t *kv, const char *key) {
    if (!kv || !key)
        return PATHSPACE_INVALID;
    if (reserved_meta_path(key) || strlen(key) >= 1024 ||
        strstr(key, "//"))
        return PATHSPACE_INVALID;
    if (!storage_is_index_key(key))
        return PATHSPACE_INVALID;
    uint64_t off = 0;
    int rc = pathspaceShmStorageResolve(kv, key, &off);
    if (rc == PATHSPACE_OK)
        return PATHSPACE_OK; 
    if (rc != PATHSPACE_MISSING)
        return rc;
    
    rc = pathspaceShmStorageRealloc(kv, key, 0, 0, &off);
    return rc;
}

static int storage_key_exists(pathspace_t *kv, const char *key) {
    if (!kv || !key)
        return 0;
    art_hdr_t *h = art_search(kv, kv->hdr->art_root, (const uint8_t *)key,
                              (int)strlen(key));
    if (h && AV_HAS(art_val_load(h)))
        return 1;

    if (storage_is_index_key(key)) {
        
        keyscan_t children = {0};
        char buf[4096];
        int rc = 0;
        if (kv->hdr->art_root >= 0)
            rc = art_scan_pfx(kv, key, (int)strlen(key), buf, (int)sizeof buf,
                              &children);
        int has = rc == 0 && children.count > 0;
        keyscan_free(&children);
        return has;
    }

    size_t n = strlen(key);
    char dir[1024];
    if (n == 0 || n + 2 > sizeof dir)
        return 0;
    memcpy(dir, key, n);
    dir[n] = '/';
    dir[n + 1] = 0;
    h = art_search(kv, kv->hdr->art_root, (const uint8_t *)dir, (int)strlen(dir));
    return h && AV_HAS(art_val_load(h));
}

static int storage_index_state(pathspace_t *kv, const char *key) {
    if (!storage_is_index_key(key))
        return storage_key_exists(kv, key) ? PATHSPACE_NOT_INDEX : PATHSPACE_MISSING;
    if (!storage_key_exists(kv, key))
        return PATHSPACE_MISSING;
    return PATHSPACE_OK;
}

static int storage_children(pathspace_t *kv, const char *key, keyscan_t *out) {
    char buf[4096];
    if (kv->hdr->art_root >= 0 &&
        art_scan_pfx(kv, key, (int)strlen(key), buf, (int)sizeof buf, out) != 0) {
        keyscan_free(out);
        return PATHSPACE_BACKEND;
    }
    if (out->count == 0 && !storage_key_exists(kv, key))
        return PATHSPACE_MISSING;
    return PATHSPACE_OK;
}

static int storage_list_prefix(const char *key, char *out, size_t cap) {
    size_t n = strlen(key);
    if (n == 0 || n >= cap)
        return PATHSPACE_INVALID;
    memcpy(out, key, n + 1);
    return PATHSPACE_OK;
}

int pathspaceShmStorageListlen(pathspace_t *kv, const char *key, int64_t *out_count,
                             uint32_t *out_max_name_length) {
    if (!kv || !key || !out_count || !out_max_name_length)
        return PATHSPACE_INVALID;
    *out_count = 0;
    *out_max_name_length = 0;
    if (reserved_meta_path(key) || strlen(key) >= 1024)
        return PATHSPACE_INVALID;
    if (kv_sync(kv) != 0)
        return PATHSPACE_BACKEND;
    int rc = storage_index_state(kv, key);
    if (rc != PATHSPACE_OK)
        return rc;
    char pfx[1024];
    rc = storage_list_prefix(key, pfx, sizeof pfx);
    if (rc != PATHSPACE_OK)
        return rc;
    keyscan_t children = {0};
    rc = storage_children(kv, pfx, &children);
    if (rc != PATHSPACE_OK)
        return rc;
    uint32_t max_len = 0;
    for (int32_t i = 0; i < children.count; i++) {
        size_t n = strlen(children.keys[i]);
        if (n > max_len)
            max_len = (uint32_t)n;
    }
    *out_count = children.count;
    *out_max_name_length = max_len;
    keyscan_free(&children);
    return PATHSPACE_OK;
}

int pathspaceShmStorageList(pathspace_t *kv, const char *key, char *out_names,
                          uint32_t out_capacity, uint32_t *out_length,
                          uint32_t *out_count, uint64_t *out_generation) {
    if (!kv || !key)
        return PATHSPACE_INVALID;
    if (out_length)
        *out_length = 0;
    if (out_count)
        *out_count = 0;
    if (out_generation)
        *out_generation = 0;
    if (reserved_meta_path(key) || strlen(key) >= 1024)
        return PATHSPACE_INVALID;
    if (kv_sync(kv) != 0)
        return PATHSPACE_BACKEND;
    int rc = storage_index_state(kv, key);
    if (rc != PATHSPACE_OK)
        return rc;
    char pfx[1024];
    rc = storage_list_prefix(key, pfx, sizeof pfx);
    if (rc != PATHSPACE_OK)
        return rc;
    keyscan_t children = {0};
    rc = storage_children(kv, pfx, &children);
    if (rc != PATHSPACE_OK)
        return rc;
    uint64_t total = 0;
    for (int32_t i = 0; i < children.count; i++)
        total += (uint64_t)strlen(children.keys[i]) + 1;
    if (out_generation)
        *out_generation = __atomic_load_n(&kv->hdr->list_gen, __ATOMIC_ACQUIRE);
    if (out_count)
        *out_count = (uint32_t)children.count;
    if (out_length)
        *out_length = (uint32_t)total;
    
    if (out_names != NULL) {
        if (total > out_capacity) {
            
            keyscan_free(&children);
            return PATHSPACE_CAPACITY;
        }
        char *p = out_names;
        for (int32_t i = 0; i < children.count; i++) {
            size_t n = strlen(children.keys[i]) + 1;
            memcpy(p, children.keys[i], n);
            p += n;
        }
    }
    keyscan_free(&children);
    return PATHSPACE_OK;
}

#define PATHSPACE_SHM_DEFAULT_SIZE (8ULL * 64 * 64 * 64 * 64)

#if defined(__GNUC__)
#define PATHSPACE_PUBLIC __attribute__((visibility("default")))
#else
#define PATHSPACE_PUBLIC
#endif

static int pathspace_dsn_path(const char *dsn, char *out, size_t out_size) {
    if (!dsn)
        return -1;
    const char *separator = strstr(dsn, "://");
    if (!separator || separator - dsn != 3 || strncmp(dsn, "shm", 3) != 0)
        return -1;
    int written = snprintf(out, out_size, "%s", separator + 3);
    return written > 0 && (size_t)written < out_size ? 0 : -1;
}

static int pathspace_fail(int status, char *err, uint32_t err_cap,
                        const char *operation, const char *key) {
    if (err && err_cap)
        snprintf(err, err_cap, "pathspace: %s failed at %s", operation,
                 key ? key : "(null)");
    return status;
}

PATHSPACE_PUBLIC void *pathspaceConnect(const char *dsn) {
    
    int entry_errno = errno;
    char path[1024];
    if (pathspace_dsn_path(dsn, path, sizeof path) != 0) {
        
        errno = EINVAL;
        return NULL;
    }
    void *handle = (void *)pathspaceShmOpen(path, PATHSPACE_SHM_DEFAULT_SIZE);
    if (handle != NULL) {
        errno = entry_errno;
        return handle;
    }
    if (errno == 0)
        errno = EIO;
    return NULL;
}

PATHSPACE_PUBLIC void pathspaceClose(void *handle) {
    if (handle)
        pathspaceShmClose((pathspace_t *)handle);
}

PATHSPACE_PUBLIC uint8_t *pathspaceXvalueInProcessAddress(void *handle) {
    if (!handle)
        return NULL;
    return pathspaceShmStorageDataBase((pathspace_t *)handle);
}

PATHSPACE_PUBLIC int pathspaceResolveOffset(void *handle, const char *key,
                                 int64_t parent_key_anchor,
                                 uint64_t *out_value_offset, char *err,
                                 uint32_t err_cap) {
    if (!handle || !key || !out_value_offset)
        return PATHSPACE_INVALID;
    *out_value_offset = 0;
    int internal;
    if (parent_key_anchor == PATHSPACE_ANCHOR_ROOT) {
        internal = pathspaceShmStorageResolve((pathspace_t *)handle, key,
                                            out_value_offset);
    } else {
        pathspaceAnchor_t anchor;
        if (anchor_from_offset((pathspace_t *)handle, parent_key_anchor,
                               &anchor) != 0)
            return pathspace_fail(PATHSPACE_ANCHOR_INVALID, err, err_cap,
                                "resolve", key);
        internal = pathspaceShmStorageResolveFrom((pathspace_t *)handle, key,
                                                anchor, out_value_offset);
    }
    if (internal == PATHSPACE_OK)
        return PATHSPACE_OK;
    return pathspace_fail(internal, err, err_cap, "resolve", key);
}

PATHSPACE_PUBLIC int pathspaceRealloc(void *handle, const char *key,
                                 uint64_t body_length, uint64_t attr_length,
                                 uint64_t *out_value_offset, char *err,
                                 uint32_t err_cap) {
    if (!handle || !key || !out_value_offset)
        return PATHSPACE_INVALID;
    *out_value_offset = 0;
    int internal = pathspaceShmStorageRealloc((pathspace_t *)handle, key,
                                            body_length, attr_length,
                                            out_value_offset);
    if (internal == PATHSPACE_OK)
        return PATHSPACE_OK;
    return pathspace_fail(internal, err, err_cap, "realloc",
                        key);
}

PATHSPACE_PUBLIC int pathspaceFree(void *handle, const char *key, char *err,
                               uint32_t err_cap) {
    if (!handle || !key)
        return PATHSPACE_INVALID;
    int internal = pathspaceShmStorageFree((pathspace_t *)handle, key);
    if (internal == PATHSPACE_OK)
        return PATHSPACE_OK;
    return pathspace_fail(internal, err, err_cap, "free", key);
}

PATHSPACE_PUBLIC int pathspaceCp(void *handle, const char *old_key,
                             const char *new_key, char *err,
                             uint32_t err_cap) {
    if (!handle || !old_key || !new_key)
        return PATHSPACE_INVALID;
    if (pathspaceShmCp((pathspace_t *)handle, old_key, new_key) != 0)
        return pathspace_fail(PATHSPACE_BACKEND, err, err_cap, "cp", old_key);
    return PATHSPACE_OK;
}

PATHSPACE_PUBLIC int pathspaceMkindex(void *handle, const char *key, char *err,
                                  uint32_t err_cap) {
    if (!handle || !key)
        return PATHSPACE_INVALID;
    int internal = pathspaceShmStorageMakeindex((pathspace_t *)handle, key);
    if (internal == PATHSPACE_OK)
        return PATHSPACE_OK;
    return pathspace_fail(internal, err, err_cap, "mkindex",
                        key);
}

PATHSPACE_PUBLIC int pathspaceDeltree(void *handle, const char *key, char *err,
                                  uint32_t err_cap) {
    if (!handle || !key)
        return PATHSPACE_INVALID;
    if (pathspaceShmDeltree((pathspace_t *)handle, key) != 0)
        return pathspace_fail(PATHSPACE_BACKEND, err, err_cap, "deltree", key);
    return PATHSPACE_OK;
}

PATHSPACE_PUBLIC int pathspaceCpdir(void *handle, const char *source_key,
                                const char *destination_key, char *err,
                                uint32_t err_cap) {
    if (!handle || !source_key || !destination_key)
        return PATHSPACE_INVALID;
    if (pathspaceShmCptree((pathspace_t *)handle, source_key, destination_key) != 0)
        return pathspace_fail(PATHSPACE_BACKEND, err, err_cap, "cpdir", source_key);
    return PATHSPACE_OK;
}

PATHSPACE_PUBLIC int pathspaceListlen(void *handle, const char *key,
                                  int64_t *out_count,
                                  uint32_t *out_max_name_length, char *err,
                                  uint32_t err_cap) {
    if (!handle || !key || !out_count || !out_max_name_length)
        return PATHSPACE_INVALID;
    *out_count = 0;
    *out_max_name_length = 0;
    int internal = pathspaceShmStorageListlen((pathspace_t *)handle, key, out_count,
                                            out_max_name_length);
    if (internal == PATHSPACE_OK)
        return PATHSPACE_OK;
    return pathspace_fail(internal, err, err_cap, "listlen",
                        key);
}

PATHSPACE_PUBLIC int pathspaceList(void *handle, const char *key, char *out_names,
                               uint32_t out_capacity, uint32_t *out_length,
                               uint32_t *out_count, uint64_t *out_generation,
                               char *err, uint32_t err_cap) {
    if (!handle || !key)
        return PATHSPACE_INVALID;
    int internal = pathspaceShmStorageList((pathspace_t *)handle, key, out_names,
                                         out_capacity, out_length, out_count,
                                         out_generation);
    if (internal == PATHSPACE_OK)
        return PATHSPACE_OK;
    return pathspace_fail(internal, err, err_cap, "list", key);
}
