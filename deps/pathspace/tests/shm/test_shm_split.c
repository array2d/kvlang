
#define _GNU_SOURCE
#include "pathspace_shm.h"
#include "test_wire.h"
#include "test_util.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SBO_DATA_SIZE (8UL * 64 * 64 * 64 * 64) 
#define ART_SLAB_INIT (64UL * 1024 * 1024)

#define GROW_KEYS 16000
#define GROW_KEY_LEN 600

static void key_of(char *buf, size_t n, int i) {
  unsigned x = (unsigned)i * 2654435761u + 12345u;
  int p = snprintf(buf, n, "/L%07d-", i);
  while (p < GROW_KEY_LEN && p < (int)n - 1) {
    x = x * 1103515245u + 12345u;
    buf[p++] = (char)('a' + ((x >> 16) % 26));
  }
  buf[p] = 0;
}

static int set_int32(pathspace_t *kv, const char *key, int32_t v) {
  uint8_t raw[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 24)};
  uint8_t *xv;
  uint64_t n = 0;
  if (pathspace_test_wire_scalar(raw, 4, "", &xv, &n) != 0)
    return -1;
  int rc = pathspaceShmSet(kv, key, xv, (int32_t)n);
  free(xv);
  return rc;
}

static int32_t get_int32(pathspace_t *kv, const char *key) {
  int32_t len = 0;
  uint8_t *d = pathspaceShmGet(kv, key, 1, &len);
  if (!d || len <= 0)
    return -1;
  pathspaceXvalueHead h;
  memcpy(&h, d, sizeof h);
  if (pathspaceXvalueHeadLen(&h) != 4)
    return -1;
  const uint8_t *b = d + PATHSPACE_XVALUE_HEAD_LEN;
  return (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                   ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
}

static int fill_keys(pathspace_t *kv) {
  char key[GROW_KEY_LEN + 8];
  int fail = 0;
  for (int i = 0; i < GROW_KEYS; i++) {
    key_of(key, sizeof key, i);
    fail += set_int32(kv, key, i) != 0;
  }
  return fail;
}

static int check_keys(pathspace_t *kv, int step) {
  char key[GROW_KEY_LEN + 8];
  int bad = 0;
  for (int i = 0; i < GROW_KEYS; i += step) {
    key_of(key, sizeof key, i);
    bad += get_int32(kv, key) != i;
  }
  return bad;
}

static void t_layout(const char *db, const char *head, const char *data) {
  printf("[layout] three files created, sizes, basic set/get\n");
  pathspace_t *kv = pathspaceShmOpen(db, SBO_DATA_SIZE);
  REQUIRE(kv != NULL, "open failed");

  CHECK(fsize(head) > 0, "head file missing");
  CHECK(fsize(data) == (off_t)SBO_DATA_SIZE, "data file size %ld",
        (long)fsize(data));
  off_t main_sz = fsize(db);
  CHECK(main_sz > (off_t)ART_SLAB_INIT && main_sz < (off_t)ART_SLAB_INIT + 4096,
        "main file size %ld", (long)main_sz);

  CHECK(set_int32(kv, "/a", 42) == 0, "set /a");
  CHECK(get_int32(kv, "/a") == 42, "get /a");
  pathspaceShmClose(kv);
}

static void t_bad_size(const char *dir) {
  printf("[bad-size] data_size not 8*64^k is rejected before any file is "
         "created\n");
  char p[512], head[600], data[600];
  snprintf(p, sizeof p, "%s/bad", dir);
  snprintf(head, sizeof head, "%s.sbo.head", p);
  snprintf(data, sizeof data, "%s.sbo.data", p);
  CHECK(pathspaceShmOpen(p, 16) == NULL,
        "16 accepted"); 
  CHECK(pathspaceShmOpen(p, 8 * 64 * 3) == NULL, "8*64*3 accepted");
  CHECK(fsize(p) < 0 && fsize(head) < 0 && fsize(data) < 0,
        "files left behind");
}

static void t_v1_rejected(const char *dir) {
  printf("[v1] legacy single-file magic is rejected\n");
  char p[512];
  snprintf(p, sizeof p, "%s/v1", dir);
  int fd = open(p, O_RDWR | O_CREAT | O_TRUNC, 0644);
  REQUIRE(fd >= 0, "create v1 stub");
  static const char v1[4096] = "pathspc-c.v1";
  CHECK(write(fd, v1, sizeof v1) == (ssize_t)sizeof v1, "write v1 stub");
  close(fd);
  CHECK(pathspaceShmOpen(p, SBO_DATA_SIZE) == NULL, "v1 file accepted");
  unlink(p);
}

static void t_grow_and_reopen(const char *db) {
  printf("[grow] %d keys force ART slab growth, then verify across reopen\n",
         GROW_KEYS);
  pathspace_t *kv = pathspaceShmOpen(db, SBO_DATA_SIZE);
  REQUIRE(kv != NULL, "open failed");
  off_t before = fsize(db);

  int set_fail = fill_keys(kv);
  CHECK(set_fail == 0, "%d sets failed", set_fail);
  off_t after = fsize(db);
  CHECK(after >= (off_t)(2 * ART_SLAB_INIT),
        "main file did not grow: %ld -> %ld", (long)before, (long)after);
  printf("  main file %ld -> %ld bytes\n", (long)before, (long)after);

  int bad = check_keys(kv, 7);
  CHECK(bad == 0, "%d keys unreadable after grow", bad);
  CHECK(get_int32(kv, "/a") == 42, "pre-existing /a lost");
  pathspaceShmClose(kv);

  kv = pathspaceShmOpen(db, 8);
  REQUIRE(kv != NULL, "reopen failed");
  CHECK(fsize(db) == after, "reopen changed main file size");
  bad = check_keys(kv, 13);
  CHECK(bad == 0, "%d keys unreadable after reopen", bad);
  CHECK(set_int32(kv, "/after-reopen", 7) == 0, "set after reopen");
  CHECK(get_int32(kv, "/after-reopen") == 7, "get after reopen");
  pathspaceShmClose(kv);
}

static void t_cross_process(const char *dir) {
  printf("[xproc] one process grows, another syncs and reads the grown area\n");
  char db[512];
  snprintf(db, sizeof db, "%s/xp", dir);
  pathspace_t *kv = pathspaceShmOpen(db, SBO_DATA_SIZE);
  REQUIRE(kv != NULL, "open failed");
  CHECK(set_int32(kv, "/seed", 1) == 0, "seed");

  int to_child[2], to_parent[2];
  REQUIRE(pipe(to_child) == 0 && pipe(to_parent) == 0, "pipe");
  pid_t pid = fork();
  REQUIRE(pid >= 0, "fork");
  if (pid == 0) {
    close(to_child[1]);
    close(to_parent[0]);
    pathspace_t *ckv = pathspaceShmOpen(db, SBO_DATA_SIZE);
    if (!ckv)
      _exit(10);
    if (get_int32(ckv, "/seed") != 1)
      _exit(11);
    char c = 'r';
    if (write(to_parent[1], &c, 1) != 1)
      _exit(12);
    if (read(to_child[0], &c, 1) != 1)
      _exit(13);
    
    char key[GROW_KEY_LEN + 8];
    key_of(key, sizeof key, GROW_KEYS - 1);
    if (get_int32(ckv, key) != GROW_KEYS - 1)
      _exit(14);
    key_of(key, sizeof key, 0);
    if (get_int32(ckv, key) != 0)
      _exit(15);
    if (set_int32(ckv, "/from-child", 99) != 0)
      _exit(16);
    pathspaceShmClose(ckv);
    _exit(0);
  }
  close(to_child[0]);
  close(to_parent[1]);
  char c;
  CHECK(read(to_parent[0], &c, 1) == 1 && c == 'r', "child ready");

  int set_fail = fill_keys(kv);
  CHECK(set_fail == 0, "%d sets failed", set_fail);
  CHECK(fsize(db) >= (off_t)(2 * ART_SLAB_INIT), "parent did not grow");
  c = 'g';
  CHECK(write(to_child[1], &c, 1) == 1, "signal child");

  int status = 0;
  CHECK(waitpid(pid, &status, 0) == pid, "waitpid");
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "child exit status %d (signal %d)",
        WIFEXITED(status) ? WEXITSTATUS(status) : -1,
        WIFSIGNALED(status) ? WTERMSIG(status) : 0);
  CHECK(get_int32(kv, "/from-child") == 99, "parent sees child's write");
  pathspaceShmClose(kv);
}

int main(int argc, char **argv) {
  char tmpl[] = "/tmp/pathspace-split-XXXXXX";
  const char *dir = argc > 1 ? argv[1] : mkdtemp(tmpl);
  if (!dir) {
    perror("mkdtemp");
    return 2;
  }
  char db[512], head[600], data[600];
  snprintf(db, sizeof db, "%s/db", dir);
  snprintf(head, sizeof head, "%s.sbo.head", db);
  snprintf(data, sizeof data, "%s.sbo.data", db);

  t_layout(db, head, data);
  t_bad_size(dir);
  t_v1_rejected(dir);
  t_grow_and_reopen(db);
  t_cross_process(dir);

  if (argc <= 1) {
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
      fprintf(stderr, "cleanup failed: %s\n", dir);
  }
  printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
