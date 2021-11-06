#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_ndbm.h>
#include <dc_posix/dc_posix_env.h>
ndbm_ibeacons_store(const struct dc_error *err, const struct dc_posix_env *env,
                    void *db, const char *key_str, const char *val_str);
ndbm_ibeacons_store(const struct dc_error *err, const struct dc_posix_env *env,
                    void *db, const char *key_str, const char *val_str)
{
    datum key = {key_str, dc_strlen(key)};
    datum val = {val_str, dc_strlen(key)};
    if (dc_error_has_no_error)
    {
        dc_dbm_store(env, err, db, key, val, 1);
    }
};
// ndbm_ibeacons_fetch() { dc_dbm_fetch(); };
// ndbm_ibeacons_open() { dc_dbm_open(); };
// ndbm_ibeacons_close() {dc_dbm_close(; };