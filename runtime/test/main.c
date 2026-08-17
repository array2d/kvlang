#include "kvlang_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern void rwext_term_start(const char *dsn);

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s funcname [arg...]\n", argv[0]); return 2; }
    const char *dsn = getenv("KVSPACE") ? getenv("KVSPACE") : "redis://127.0.0.1:6379";
    kvlang_rt *rt = kvlang_rt_connect(dsn);
    if (!rt) { fprintf(stderr, "connect failed\n"); return 1; }
    if (!getenv("KVLANG_NOTERM")) {
        rwext_term_start(dsn);
        usleep(200000);
    }
    char *ret = NULL;
    char err[512];
    int rc = kvlang_rt_execute(rt, argv[1], (const char *const *)(argv + 2), argc - 2, &ret, err, sizeof err);
    if (rc != 0) fprintf(stderr, "error: %s\n", err);
    else if (ret) printf("%s\n", ret);
    if (ret) free(ret);
    kvlang_rt_disconnect(rt);
    return rc;
}
