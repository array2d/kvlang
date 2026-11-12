#ifndef KVSPACE_CONST_H
#define KVSPACE_CONST_H

#define KVSPACE_PATH_SEP           "/"
#define KVSPACE_DIR_INDEX_SUF       "/"
#define KVSPACE_MEMBER_SEP          "·"
#define KVSPACE_RUNTIME_MEMBER_SEP  "\xE2\x80\xA5"

#define KVSPACE_ERR_DIR_MUST_END_WITH_SLASH "kvspace: index must end with /"
#define KVSPACE_ERR_INVALID_PATH            "kvspace: path must be absolute and canonical"

#define KVSPACE_KV(X) \
    X(KVSPACE_PATH_SEP) \
    X(KVSPACE_DIR_INDEX_SUF) \
    X(KVSPACE_MEMBER_SEP) \
    X(KVSPACE_RUNTIME_MEMBER_SEP) \
    X(KVSPACE_ERR_DIR_MUST_END_WITH_SLASH) \
    X(KVSPACE_ERR_INVALID_PATH)

const char *kvspaceConstStorage(const char *name);

#endif
