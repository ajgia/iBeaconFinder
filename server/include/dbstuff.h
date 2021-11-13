#ifndef TEMPLATE_COMMON_H
#define TEMPLATE_COMMON_H
#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_ndbm.h>

void db_store(const struct dc_error *err, const struct dc_posix_env *env,
              const char *key_str, const char *val_str);

void db_fetch(const struct dc_error *err, const struct dc_posix_env *env,
              const char *key_str, const char *val_str);
#endif
