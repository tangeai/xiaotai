#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    EF_NO_ERR,
    EF_ENV_NAME_ERR,
    EF_WRITE_ERR,
} EfErrCode;

size_t ef_get_env_blob(const char *key, void *value, size_t capacity,
                       size_t *saved_size);
EfErrCode ef_set_env_blob(const char *key, const void *value, size_t size);
EfErrCode ef_del_env(const char *key);
uint32_t ef_calc_crc32(uint32_t crc, const void *data, size_t size);
