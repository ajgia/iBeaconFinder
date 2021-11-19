#ifndef TEMPLATE_DBSTUFF_H
#define TEMPLATE_DBSTUFF_H
#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_ndbm.h>

void db_store(const struct dc_posix_env *env, struct dc_error *err,
              const char *key_str, const char *val_str);

void db_fetch(const struct dc_posix_env *env, struct dc_error *err,
              const char *key_str, const char *val_str);
void db_fetch_all(const struct dc_posix_env *env, struct dc_error *err, 
    const char*val_str);
#endif  // TEMPLATE_DBSTUFF_H
