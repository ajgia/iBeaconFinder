
#include "dbstuff.h"
#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_ndbm.h>
#include <dc_posix/dc_posix_env.h>
#include <dc_posix/dc_stdlib.h>
#include <dc_posix/dc_string.h>
void db_store(const struct dc_error *err, const struct dc_posix_env *env, const char *key_str, const char *val_str)
{
    DBM *db;
    if(dc_error_has_no_error(err))
    {
        db = dc_dbm_open(env, err, "beacons", DC_O_RDWR | DC_O_CREAT, 0600);
    }
    datum key = {key_str, dc_strlen(env, key_str)};
    datum val = {val_str, dc_strlen(env, val_str)};
    if(dc_error_has_no_error(err))
    {
        dc_dbm_store(env, err, db, key, val, 1);
        dc_dbm_close(env, err, db);
    }
}

void db_fetch(const struct dc_error *err, const struct dc_posix_env *env, const char *key_str, const char *val_str)
{
    DBM *db;
    if(dc_error_has_no_error(err))
    {
        db = dc_dbm_open(env, err, "beacons", DC_O_RDWR | DC_O_CREAT, 0600);
    }
    datum val;
    datum key = {key_str, dc_strlen(env, key_str)};
    if(dc_error_has_no_error(err))
    {
        val = dc_dbm_fetch(env, err, db, key);
    }
    val_str = val.dptr;
    if(dc_error_has_no_error(err))
    {
        dc_dbm_close(env, err, db);
    }
}
