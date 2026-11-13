#ifndef PATHSPACE_CONST_H
#define PATHSPACE_CONST_H

#define PATHSPACE_PATH_SEP           "/"
#define PATHSPACE_DIR_INDEX_SUF       "/"
#define PATHSPACE_MEMBER_SEP          "·"
#define PATHSPACE_RUNTIME_MEMBER_SEP  "\xE2\x80\xA5"

#define PATHSPACE_ERR_DIR_MUST_END_WITH_SLASH "pathspace: index must end with /"
#define PATHSPACE_ERR_INVALID_PATH            "pathspace: path must be absolute and canonical"

#define PATHSPACE_KV(X) \
    X(PATHSPACE_PATH_SEP) \
    X(PATHSPACE_DIR_INDEX_SUF) \
    X(PATHSPACE_MEMBER_SEP) \
    X(PATHSPACE_RUNTIME_MEMBER_SEP) \
    X(PATHSPACE_ERR_DIR_MUST_END_WITH_SLASH) \
    X(PATHSPACE_ERR_INVALID_PATH)

const char *pathspaceConstStorage(const char *name);

#endif
