
#define _GNU_SOURCE
#include "pathspace_shm.h"
#include "test_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "failed: %s (line %d)\n", #x, __LINE__);           \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static uint8_t *slack(const char *s, uint64_t *n) {
    uint8_t *v = NULL;
    if (pathspace_test_wire_slack((const uint8_t *)s, strlen(s), "", &v, n) != 0)
        return NULL;
    return v;
}

int main(void) {
    char dir[] = "/tmp/pathspace-art-align-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[128];
    snprintf(path, sizeof path, "%s/db", dir);

    pathspace_t *kv = pathspaceShmOpen(path, 8UL * 64 * 64 * 64 * 64);
    CHECK(kv != NULL);

    for (int i = 0; i < 64; i++) {
        char key[64];
        snprintf(key, sizeof key, "/a/%0*d", 1 + i % 7, i);
        uint64_t n = 0;
        uint8_t *v = slack("x", &n);
        CHECK(v != NULL);
        CHECK(pathspaceShmSet(kv, key, v, (int32_t)n) == 0);
        free(v);
    }

    CHECK(pathspaceShmArtAlignCheck(kv) == -1);

    pathspaceShmTestSetFormatVersion(kv, PATHSPACE_FORMAT_VERSION - 1);
    pathspaceShmClose(kv);
    
    struct stat st;
    CHECK(stat(path, &st) == 0 && truncate(path, st.st_size - 8) == 0);
    char errpath[160];
    snprintf(errpath, sizeof errpath, "%s/stderr", dir);
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    FILE *ef = fopen(errpath, "w+");
    CHECK(saved >= 0 && ef != NULL);
    dup2(fileno(ef), STDERR_FILENO);
    pathspace_t *old = pathspaceShmOpen(path, 8UL * 64 * 64 * 64 * 64);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    CHECK(old == NULL);
    char msg[512] = {0};
    rewind(ef);
    size_t ml = fread(msg, 1, sizeof msg - 1, ef);
    fclose(ef);
    msg[ml] = 0;
    char want[64];
    snprintf(want, sizeof want, "version=%u", (unsigned)(PATHSPACE_FORMAT_VERSION - 1));
    if (!strstr(msg, "rebuild") || !strstr(msg, want))
        fprintf(stderr, "open stderr: [%s]\n", msg);
    CHECK(strstr(msg, "rebuild") != NULL);
    CHECK(strstr(msg, want) != NULL);

    printf("PASS art_alignment\n");
    return 0;
}
