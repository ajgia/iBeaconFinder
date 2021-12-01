#ifndef TEMPLATE_DBSTUFF_H
#define TEMPLATE_DBSTUFF_H
#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_ndbm.h>

/**
 * @brief Stores a key-value pair in the db
 * 
 * @param env 
 * @param err 
 * @param key_str 
 * @param val_str 
 * @param dbLocation 
 */
void db_store(const struct dc_posix_env *env, struct dc_error *err,
              const char *key_str, const char *val_str, const char *dbLocation);
/**
 * @brief Returns a value matching to key from the db
 * 
 * @param env 
 * @param err 
 * @param key_str 
 * @param val_str 
 * @param dbLocation 
 */
void db_fetch(const struct dc_posix_env *env, struct dc_error *err,
              const char *key_str, const char *val_str, const char *dbLocation);
/**
 * @brief Returns all values from the db
 * 
 * @param env 
 * @param err 
 * @param val_str 
 * @param dbLocation 
 */
void db_fetch_all(const struct dc_posix_env *env, struct dc_error *err, 
    const char*val_str, const char *dbLocation);
#endif  // TEMPLATE_DBSTUFF_H
