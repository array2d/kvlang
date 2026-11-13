
#define _GNU_SOURCE
#include "pathspace_shm.h"
#include "test_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SBO_DATA_SIZE (8UL * 64 * 64 * 64 * 64)

static int write_new_body(pathspace_t *kv, const char *key, uint64_t body_len,
                          uint8_t **body);

static int failures = 0;
#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);                   \
      fprintf(stderr, __VA_ARGS__);                                            \
      fprintf(stderr, "\n");                                                   \
    }                                                                          \
  } while (0)
#define REQUIRE(cond, ...)                                                     \
  do {                                                                         \
    int require_ok = (cond);                                                   \
    CHECK(require_ok, __VA_ARGS__);                                             \
    if (!require_ok)                                                           \
      return;                                                                  \
  } while (0)

static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
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

static int mkindex(pathspace_t *kv, const char *key) {
  return pathspaceShmStorageMakeindex(kv, key);
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

static void list_free(char **ns, int32_t n) {
  if (!ns)
    return;
  for (int32_t i = 0; i < n; i++)
    free(ns[i]);
  free(ns);
}

static int list_has(char **ns, int32_t n, const char *s) {
  for (int32_t i = 0; i < n; i++)
    if (strcmp(ns[i], s) == 0)
      return 1;
  return 0;
}

static int do_list(pathspace_t *kv, const char *pfx, char ***on, int32_t *n) {
  *on = NULL;
  *n = 0;
  return pathspaceShmList(kv, pfx, false, 0, on, n);
}

static void t_correct(const char *db) {
  printf("[correct] isolation, nested, compressed prefix, miss, deltree, cptree\n");
  pathspace_t *kv = pathspaceShmOpen(db, SBO_DATA_SIZE);
  REQUIRE(kv != NULL, "open failed");

  CHECK(set_int32(kv, "/lib/a", 1) == 0, "set /lib/a");
  CHECK(set_int32(kv, "/lib/b", 2) == 0, "set /lib/b");
  CHECK(set_int32(kv, "/lib/mod/x", 3) == 0, "set /lib/mod/x");
  CHECK(set_int32(kv, "/d/f/p", 4) == 0, "set /d/f/p");
  CHECK(set_int32(kv, "/d/f/q", 5) == 0, "set /d/f/q");
  CHECK(set_int32(kv, "/d/g", 6) == 0, "set /d/g");
  CHECK(set_int32(kv, "/foo/bar1", 7) == 0, "set /foo/bar1");
  CHECK(set_int32(kv, "/foo/bar2", 8) == 0, "set /foo/bar2");
  CHECK(set_int32(kv, "/foo/zzz", 9) == 0, "set /foo/zzz");
  uint8_t raw[4] = {7};
  uint8_t *permitted = NULL;
  uint64_t permitted_len = 0;
  CHECK(pathspace_test_wire_scalar(raw, 4, "", &permitted, &permitted_len) == 0 &&
            pathspaceShmSetValue(kv, "/private", permitted,
                               (int32_t)permitted_len) == 0,
        "set permitted value");
  free(permitted);
  CHECK(pathspaceShmCp(kv, "/private", "/private_copy") == 0,
        "copy permitted value");
  CHECK(pathspaceShmDel(kv, "/private_copy") == 0, "delete copied value");

  uint8_t *ptr = NULL;
  uint64_t ptr_len = 0;
  CHECK(pathspace_test_wire_ptr((const uint8_t *)"/private", 8, "",
                                &ptr, &ptr_len) == 0 &&
            pathspaceShmSetValue(kv, "/link", ptr, (int32_t)ptr_len) == 0,
        "set pointer");
  free(ptr);
  CHECK(pathspaceShmCp(kv, "/link", "/link_copy") == 0,
        "copy pointer");

  char **ns;
  int32_t n;

  REQUIRE(do_list(kv, "/d/", &ns, &n) == 0, "list /d/");
  CHECK(n == 2, "list /d/ count %d", n);
  CHECK(list_has(ns, n, "f") && list_has(ns, n, "g"), "list /d/ names");
  list_free(ns, n);

  REQUIRE(do_list(kv, "/d/f/", &ns, &n) == 0, "list /d/f/");
  CHECK(n == 2, "list /d/f/ count %d", n);
  CHECK(list_has(ns, n, "p") && list_has(ns, n, "q"), "list /d/f/ names");
  list_free(ns, n);

  REQUIRE(do_list(kv, "/lib/", &ns, &n) == 0, "list /lib/");
  CHECK(n == 3, "list /lib/ count %d", n);
  CHECK(list_has(ns, n, "a") && list_has(ns, n, "b") && list_has(ns, n, "mod"),
        "list /lib/ names");
  list_free(ns, n);

  REQUIRE(do_list(kv, "/foo/", &ns, &n) == 0, "list /foo/");
  CHECK(n == 3, "list /foo/ count %d", n);
  CHECK(list_has(ns, n, "bar1") && list_has(ns, n, "bar2") &&
            list_has(ns, n, "zzz"),
        "list /foo/ names");
  list_free(ns, n);

  REQUIRE(do_list(kv, "/nope/", &ns, &n) == 0, "list /nope/");
  CHECK(n == 0, "list /nope/ count %d", n);
  list_free(ns, n);

  REQUIRE(do_list(kv, "/food/", &ns, &n) == 0, "list /food/");
  CHECK(n == 0, "list /food/ count %d", n);
  list_free(ns, n);

  REQUIRE(do_list(kv, "/", &ns, &n) == 0, "list /");
  CHECK(list_has(ns, n, "lib") && list_has(ns, n, "d") && list_has(ns, n, "foo"),
        "list / names");
  CHECK(!list_has(ns, n, ".pathspace-meta"), "hide metadata root");
  list_free(ns, n);

  CHECK(mkindex(kv, "/frames/[1]/") == 0, "frame directory");
  CHECK(set_int32(kv, "/frames/[1]/value", 7) == 0, "frame child");
  REQUIRE(do_list(kv, "/frames/", &ns, &n) == 0, "list frames");
  CHECK(n == 1 && list_has(ns, n, "[1]/"), "deduplicate directory child");
  list_free(ns, n);

  CHECK(set_int32(kv, "/private", 8) == 0, "overwrite permitted value");
  uint8_t *body = NULL;
  CHECK(write_new_body(kv, "/private", 4, &body) == 0,
        "write new permitted place");
  if (body)
    memcpy(body, raw, 4);
  CHECK(pathspaceShmDel(kv, "/private") == 0, "delete private value");

  permitted = NULL;
  permitted_len = 0;
  CHECK(pathspace_test_wire_scalar(raw, 4, "",
                                   &permitted, &permitted_len) == 0 &&
            pathspaceShmSetValue(kv, "/tree/a", permitted,
                               (int32_t)permitted_len) == 0,
        "set tree value");
  free(permitted);
  CHECK(pathspaceShmCptree(kv, "/tree", "/tree_copy") == 0,
        "copy tree");
  CHECK(pathspaceShmDeltree(kv, "/tree_copy") == 0,
        "delete copied tree");

  CHECK(mkindex(kv, "/lib/") == 0, "new directory value");
  {
    uint8_t *ext_wire = NULL;
    uint64_t ext_len = 0;
    CHECK(pathspace_test_wire_ext((const uint8_t *)"/lib/", 5, "", &ext_wire,
                                 &ext_len) == 0,
          "build ext wire");
    CHECK(pathspaceShmSetValue(kv, "/e/", ext_wire, (int32_t)ext_len) == 0,
          "mount /lib/ at /e/");
    free(ext_wire);
  }
  REQUIRE(pathspaceShmList(kv, "/e/", true, 0, &ns, &n) == 0, "list /e/ ex");
  CHECK(list_has(ns, n, "a") && list_has(ns, n, "b") && list_has(ns, n, "mod"),
        "extindex children");
  list_free(ns, n);

  CHECK(pathspaceShmCptree(kv, "/d/f", "/cp") == 0, "cptree");
  REQUIRE(do_list(kv, "/cp/", &ns, &n) == 0, "list /cp/");
  CHECK(n == 2 && list_has(ns, n, "p") && list_has(ns, n, "q"),
        "cptree children");
  list_free(ns, n);
  CHECK(get_int32(kv, "/cp/p") == 4 && get_int32(kv, "/cp/q") == 5,
        "cptree values");

  CHECK(pathspaceShmDeltree(kv, "/d") == 0, "deltree /d");
  REQUIRE(do_list(kv, "/d/", &ns, &n) == 0, "list /d/ after deltree");
  CHECK(n == 0, "deltree left children %d", n);
  list_free(ns, n);
  CHECK(get_int32(kv, "/lib/a") == 1 && get_int32(kv, "/foo/bar1") == 7,
        "deltree escaped sibling trees");
  CHECK(get_int32(kv, "/d/f/p") == -1, "deltree did not remove target");

  pathspaceShmClose(kv);
}

static int fill_noise(pathspace_t *kv, int from, int to) {
  char key[32];
  int fail = 0;
  for (int i = from; i < to; i++) {
    snprintf(key, sizeof key, "/n/%02d/%03d", i / 100, i % 100);
    fail += set_int32(kv, key, i) != 0;
  }
  return fail;
}

static double time_list(pathspace_t *kv, const char *pfx, int reps) {
  for (int i = 0; i < 20; i++) {
    char **ns;
    int32_t n;
    do_list(kv, pfx, &ns, &n);
    list_free(ns, n);
  }
  double t0 = now_s();
  for (int i = 0; i < reps; i++) {
    char **ns;
    int32_t n;
    do_list(kv, pfx, &ns, &n);
    list_free(ns, n);
  }
  return now_s() - t0;
}

static void t_scale(const char *db) {
  const int lo = 1000, hi = 8000, reps = 400;
  printf("[scale] List(/t/) vs sibling tree size %d -> %d, %d reps\n", lo, hi,
         reps);
  pathspace_t *kv = pathspaceShmOpen(db, SBO_DATA_SIZE);
  REQUIRE(kv != NULL, "open failed");

  CHECK(set_int32(kv, "/t/a", 1) == 0, "set /t/a");
  CHECK(set_int32(kv, "/t/b", 2) == 0, "set /t/b");
  CHECK(set_int32(kv, "/t/c/x", 3) == 0, "set /t/c/x");
  CHECK(fill_noise(kv, 0, lo) == 0, "fill lo noise");

  char **ns;
  int32_t n;
  REQUIRE(do_list(kv, "/t/", &ns, &n) == 0, "list /t/");
  CHECK(n == 3 && list_has(ns, n, "a") && list_has(ns, n, "b") &&
            list_has(ns, n, "c"),
        "list /t/ names");
  list_free(ns, n);

  double t_lo = time_list(kv, "/t/", reps);
  CHECK(fill_noise(kv, lo, hi) == 0, "fill hi noise");
  REQUIRE(do_list(kv, "/t/", &ns, &n) == 0, "list /t/ after noise");
  CHECK(n == 3, "list /t/ count after noise %d", n);
  list_free(ns, n);
  double t_hi = time_list(kv, "/t/", reps);
  double ratio = t_lo > 0 ? t_hi / t_lo : 0;

  printf("  list /t/  %d noise: %.4fs\n", lo, t_lo);
  printf("  list /t/  %d noise: %.4fs  ratio=%.2f (must not track noise)\n", hi,
         t_hi, ratio);
  CHECK(ratio < 2.5, "List(/t/) scaled with sibling size: %.2fx", ratio);

  pathspaceShmClose(kv);
}

static void t_wide(const char *db) {
  pathspace_t *kv = pathspaceShmOpen(db, SBO_DATA_SIZE);
  REQUIRE(kv != NULL, "open wide failed");
  for (int i = 0; i < 4097; i++) {
    char key[32];
    snprintf(key, sizeof key, "/wide/%04d", i);
    REQUIRE(set_int32(kv, key, i) == 0, "set wide %d", i);
  }
  char **names;
  int32_t count;
  REQUIRE(do_list(kv, "/wide/", &names, &count) == 0, "list wide");
  CHECK(count == 4097, "wide count %d", count);
  if (count == 4097)
    CHECK(strcmp(names[0], "0000") == 0 &&
              strcmp(names[4096], "4096") == 0, "wide order");
  list_free(names, count);
  pathspaceShmClose(kv);
}

static void expect_children(pathspace_t *kv, const char *prefix, bool ex,
                            const char **expected, int32_t count) {
  char **names = NULL;
  int32_t n = 0;
  REQUIRE(pathspaceShmList(kv, prefix, ex, 0, &names, &n) == 0,
          "list %s", prefix);
  CHECK(n == count, "%s count %d, expected %d", prefix, n, count);
  for (int32_t i = 0; i < n && i < count; i++)
    CHECK(strcmp(names[i], expected[i]) == 0, "%s child %d: %s != %s",
          prefix, i, names[i], expected[i]);
  list_free(names, n);
  int32_t length = -1;
  CHECK(pathspaceShmListLen(kv, prefix, ex, 0, &length) == 0 && length == count,
        "%s ListLen %d, expected %d", prefix, length, count);
}

static void expect_children_set(pathspace_t *kv, const char *prefix, bool ex,
                                const char **expected, int32_t count) {
  char **names = NULL;
  int32_t n = 0;
  REQUIRE(pathspaceShmList(kv, prefix, ex, 0, &names, &n) == 0, "list %s", prefix);
  CHECK(n == count, "%s count %d, expected %d", prefix, n, count);
  
  for (int32_t i = 0; i < count; i++) {
    CHECK(list_has(names, n, expected[i]), "%s missing child %s", prefix,
          expected[i]);
  }
  list_free(names, n);
}

static void t_direct_children(const char *db) {
  printf("[children] markers, tombstones, split UTF-8, fanout, extindex, reopen\n");
  pathspace_t *kv = pathspaceShmOpen(db, SBO_DATA_SIZE);
  REQUIRE(kv != NULL, "open children failed");
  
  CHECK(set_int32(kv, "/abc/name·field/value", 5) == 0, "split separator");
  const char *utf8[] = {"name"};
  expect_children(kv, "/abc/", false, utf8, 1);
  CHECK(set_int32(kv, "/case/a", 1) == 0, "scalar child");
  CHECK(set_int32(kv, "/case/ab", 2) == 0, "shared name prefix");
  CHECK(mkindex(kv, "/case/a/") == 0, "directory marker");
  CHECK(set_int32(kv, "/case/a/deep/value", 3) == 0, "nested child");
  CHECK(set_int32(kv, "/case/a·field", 4) == 0, "attribute child");
  const char *marked[] = {"a/", "ab"};
  expect_children(kv, "/case/", false, marked, 2);
  CHECK(pathspaceShmDel(kv, "/case/a/") == 0, "delete marker");
  const char *unmarked[] = {"a", "ab"};
  expect_children(kv, "/case/", false, unmarked, 2);

  CHECK(set_int32(kv, "/中文/子项·属性/值", 6) == 0, "UTF-8 child");
  const char *chinese[] = {"子项"};
  expect_children(kv, "/中文/", false, chinese, 1);

  CHECK(set_int32(kv, "/coords/[10]/x", 10) == 0, "coord 10");
  CHECK(set_int32(kv, "/coords/[2]/x", 2) == 0, "coord 2");
  CHECK(set_int32(kv, "/coords/[-1]/x", -1) == 0, "coord -1");
  CHECK(mkindex(kv, "/coords/[10]/") == 0, "coord marker");
  
  const char *coords[] = {"[-1]", "[2]", "[10]/"};
  expect_children_set(kv, "/coords/", false, coords, 3);

  CHECK(mkindex(kv, "/remote/") == 0, "remote index");
  CHECK(mkindex(kv, "/remote/a/") == 0, "remote marker");
  CHECK(set_int32(kv, "/remote/a/deep/value", 7) == 0, "remote child");
  CHECK(set_int32(kv, "/remote/z/value", 8) == 0, "remote z");
  CHECK(set_int32(kv, "/local/a/value", 9) == 0, "local child");
  {
    uint8_t *ext_wire = NULL;
    uint64_t ext_len = 0;
    CHECK(pathspace_test_wire_ext((const uint8_t *)"/remote/", 8, "", &ext_wire,
                                 &ext_len) == 0,
          "build ext wire for /local/");
    CHECK(pathspaceShmSetValue(kv, "/local/", ext_wire, (int32_t)ext_len) == 0,
          "mount /remote/ at /local/");
    free(ext_wire);
  }
  
  const char *extended[] = {"a/", "z"};
  expect_children(kv, "/local/", true, extended, 2);
  {
    char **names = NULL;
    int32_t n = 0;
    REQUIRE(pathspaceShmList(kv, "/local/", true, 0, &names, &n) == 0,
            "list /local/ ext");
    CHECK(n == 2, "/local/ ext 只有 a 与 z 两项（a 的两个形态合成一项），实得 %d",
          n);
    
    CHECK(list_has(names, n, "a/"), "跨段冲突应保留目录形态 a/");
    CHECK(!list_has(names, n, "a"), "跨段冲突不应同时留下值形态 a");
    list_free(names, n);
  }
  const char *local[] = {"a"};
  expect_children(kv, "/local/", false, local, 1);

  for (int c = 1; c < 256; c++) {
    if (c == '/' || c == 0xC2)
      continue;
    char key[32];
    snprintf(key, sizeof key, "/dead/child/%c/value", c);
    REQUIRE(set_int32(kv, key, c) == 0, "set fanout %d", c);
    REQUIRE(pathspaceShmDel(kv, key) == 0, "delete fanout %d", c);
  }
  expect_children(kv, "/dead/", false, NULL, 0);
  CHECK(set_int32(kv, "/dead/child/last/value", 11) == 0, "revive branch");
  const char *alive[] = {"child"};
  expect_children(kv, "/dead/", false, alive, 1);
  CHECK(pathspaceShmDel(kv, "/dead/child/last/value") == 0, "delete revived branch");
  expect_children(kv, "/dead/", false, NULL, 0);

  for (int i = 0; i < 3000; i++) {
    char key[64];
    snprintf(key, sizeof key, "/many/child/deep/%04d/value", i);
    REQUIRE(set_int32(kv, key, i) == 0, "set descendant %d", i);
  }
  expect_children(kv, "/many/", false, alive, 1);
  for (int i = 0; i < 3000; i++) {
    char key[64];
    snprintf(key, sizeof key, "/many/child/deep/%04d/value", i);
    REQUIRE(pathspaceShmDel(kv, key) == 0, "delete descendant %d", i);
  }
  expect_children(kv, "/many/", false, NULL, 0);
  CHECK(mkindex(kv, "/many/child/") == 0, "marker on dead branch");
  const char *marker_only[] = {"child/"};
  expect_children(kv, "/many/", false, marker_only, 1);
  pathspaceShmClose(kv);
  kv = pathspaceShmOpen(db, SBO_DATA_SIZE);
  REQUIRE(kv != NULL, "reopen children failed");
  expect_children(kv, "/many/", false, marker_only, 1);
  expect_children(kv, "/local/", true, extended, 2);
  expect_children(kv, "/dead/", false, NULL, 0);
  pathspaceShmClose(kv);
}

static int write_new_body(pathspace_t *kv, const char *key, uint64_t body_len,
                          uint8_t **body) {
    uint64_t offset = 0;
    if (pathspaceShmStorageRealloc(kv, key, body_len, 0, &offset) != 0)
        return -1;
    *body = pathspaceShmStorageDataBase(kv) + offset + PATHSPACE_XVALUE_HEAD_LEN;
    return 0;
}

int main(int argc, char **argv) {
  char tmpl[] = "/tmp/pathspace-artscan-XXXXXX";
  const char *dir = argc > 1 ? argv[1] : mkdtemp(tmpl);
  if (!dir) {
    perror("mkdtemp");
    return 2;
  }
  char db[512], db2[512], db3[512], db4[512];
  snprintf(db, sizeof db, "%s/db", dir);
  snprintf(db2, sizeof db2, "%s/db2", dir);
  snprintf(db3, sizeof db3, "%s/db3", dir);
  snprintf(db4, sizeof db4, "%s/db4", dir);

  t_correct(db);
  t_scale(db2);
  t_wide(db3);
  t_direct_children(db4);

  if (argc <= 1) {
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
      fprintf(stderr, "cleanup failed: %s\n", dir);
  }
  printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
