#include "runtime_internal.h"

const char *kt_vtid_from_pc(const char *pc, sbuf_t *out) {
    sb_clear(out);
    const char *pfx = VTHREAD_ROOT PATH_SEP;
    size_t pl = strlen(pfx);
    if (strncmp(pc, pfx, pl) != 0) return "";
    const char *rest = pc + pl;
    const char *slash = strchr(rest, '/');
    if (slash) sb_putn(out, rest, (size_t)(slash - rest));
    else sb_puts(out, rest);
    return out->p;
}

char *kt_stack(const char *root) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    char *r = malloc(n + 2);
    memcpy(r, root, n); r[n] = '/'; r[n + 1] = 0;
    return r;
}

char *kt_frame_root(const char *pc) {
    const char *last = NULL;
    for (const char *p = pc; (p = strstr(p, "/[")) != NULL; p += 2) last = p;
    if (!last) return NULL;
    size_t n = (size_t)(last - pc);
    char *r = malloc(n + 1);
    memcpy(r, pc, n); r[n] = 0;
    return r;
}

static char *trim_right_join(const char *root, const char *suffix) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    size_t sl = strlen(suffix);
    char *r = malloc(n + sl + 1);
    memcpy(r, root, n); memcpy(r + n, suffix, sl); r[n + sl] = 0;
    return r;
}

char *kt_entry_pc(const char *root) { return trim_right_join(root, "/[1,0]"); }
char *kt_scope_entry_pc(const char *root) { return trim_right_join(root, "/[0,0]"); }

char *kt_parent_frame(const char *root) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    if (n == 0) return strdup("");
    size_t last = (size_t)-1;
    for (size_t i = 0; i < n; i++) if (root[i] == '/') last = i;
    if (last == (size_t)-1) return strdup("");
    size_t cut = last + 1;
    char *r = malloc(cut + 1);
    memcpy(r, root, cut); r[cut] = 0;
    return r;
}

char *kt_member(const char *base, const char *name) {
    size_t bl = strlen(base), nl = strlen(name);
    char *r = malloc(bl + nl + 2);
    memcpy(r, base, bl); r[bl] = '.'; memcpy(r + bl + 1, name, nl); r[bl + 1 + nl] = 0;
    return r;
}

char *kt_lib_func(const char *pkg, const char *name) {
    sbuf_t b; sb_init(&b);
    sb_puts(&b, LIB_ROOT PATH_SEP);
    if (pkg && pkg[0]) { sb_puts(&b, pkg); sb_putc(&b, '.'); }
    sb_puts(&b, name);
    return sb_detach(&b);
}

char *kt_rwir(const char *opcode) {
    sbuf_t b; sb_init(&b);
    sb_puts(&b, LIB_ROOT PATH_SEP);
    sb_puts(&b, opcode);
    return sb_detach(&b);
}

void kt_vthread(const char *vtid, sbuf_t *out) {
    sb_clear(out);
    sb_puts(out, VTHREAD_ROOT PATH_SEP);
    sb_puts(out, vtid);
}

void kt_vthread_slot(const char *vtid, const char *frame, int i, int j, sbuf_t *out) {
    sb_clear(out);
    sb_puts(out, VTHREAD_ROOT PATH_SEP);
    sb_puts(out, vtid);
    if (frame && frame[0]) { sb_putc(out, '/'); sb_puts(out, frame); }
    sb_printf(out, "/[%d,%d]", i, j);
}

static void vt_member(const char *vtid, const char *seg, sbuf_t *out) {
    sb_clear(out);
    sb_puts(out, VTHREAD_ROOT PATH_SEP);
    sb_puts(out, vtid);
    sb_putc(out, '/');
    sb_puts(out, RUNTIME_MEMBER_SEP);
    sb_puts(out, seg);
}

void kt_vthread_pc(const char *vtid, sbuf_t *out) { vt_member(vtid, SEG_PC, out); }
void kt_vthread_status(const char *vtid, sbuf_t *out) { vt_member(vtid, SEG_STATUS, out); }
void kt_vthread_debugger(const char *vtid, sbuf_t *out) { vt_member(vtid, "debugger", out); }

void kt_vthread_status_msg(const char *vtid, const char *status, sbuf_t *out) {
    vt_member(vtid, status, out);
    sb_putc(out, '/');
    sb_puts(out, SEG_MSG);
}

static void frame_member(const char *root, const char *seg, sbuf_t *out) {
    sb_clear(out);
    char *s = kt_stack(root);
    sb_puts(out, s);
    free(s);
    sb_puts(out, RUNTIME_MEMBER_SEP);
    sb_puts(out, seg);
}

void kt_frame_callpc(const char *root, sbuf_t *out) { frame_member(root, SEG_CALLPC, out); }
void kt_frame_returnpc(const char *root, sbuf_t *out) { frame_member(root, SEG_RETURNPC, out); }
void kt_frame_ro(const char *root, sbuf_t *out) { frame_member(root, SEG_RO, out); }

bool kt_is_entry_pc(const char *pc) {
    const char *slash = strrchr(pc, '/');
    return slash && strcmp(slash, "/[1,0]") == 0;
}
