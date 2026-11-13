
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <sys/stat.h>

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
    CHECK(cond, __VA_ARGS__);                                                  \
    if (!(cond))                                                               \
      return;                                                                  \
  } while (0)

static inline off_t fsize(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 ? st.st_size : -1;
}

static inline off_t fphys(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 ? (off_t)st.st_blocks * 512 : -1;
}

#endif
