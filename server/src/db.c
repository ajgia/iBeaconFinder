
#include "dbstuff.h"
#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_ndbm.h>
#include <dc_posix/dc_posix_env.h>
#include <dc_posix/dc_stdlib.h>
#include <dc_posix/dc_string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void db_store(const struct dc_posix_env *env, struct dc_error *err, const char *key_str, const char *val_str)
{
    DBM *db;
    if(dc_error_has_no_error(err))
    {
        db = dc_dbm_open(env, err, "beacons", DC_O_RDWR | DC_O_CREAT, DC_S_IRUSR | DC_S_IWUSR | DC_S_IWGRP | DC_S_IRGRP | DC_S_IROTH | DC_S_IWOTH); 
    }
    datum key = {key_str, dc_strlen(env, key_str)};
    datum val = {val_str, dc_strlen(env, val_str)};
    if(dc_error_has_no_error(err))
    {
        dc_dbm_store(env, err, db, key, val, 1);
        dc_dbm_close(env, err, db);
    }
}

void db_fetch(const struct dc_posix_env *env, struct dc_error *err, const char *key_str, const char *val_str)
{
    DBM *db;
    if(dc_error_has_no_error(err))
    {
        db = dc_dbm_open(env, err, "beacons", DC_O_RDWR | DC_O_CREAT, DC_S_IRUSR | DC_S_IWUSR | DC_S_IWGRP | DC_S_IRGRP | DC_S_IROTH | DC_S_IWOTH); 
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

void db_fetch_all(const struct dc_posix_env *env, struct dc_error *err, const char *val_str) {
    DBM *db;
    datum key;
    datum val;
    char *return_str = (char *)calloc(1024, sizeof(char));

    if (dc_error_has_no_error(err)) {
        db = dc_dbm_open(env, err, "beacons", DC_O_RDWR | DC_O_CREAT, DC_S_IRUSR | DC_S_IWUSR | DC_S_IWGRP | DC_S_IRGRP | DC_S_IROTH | DC_S_IWOTH); 
    }
    
    for (key = dc_dbm_firstkey(env, err, db); key.dptr != NULL; key = dc_dbm_nextkey(env, err, db) ) {
        val = dc_dbm_fetch(env, err, db, key);
        strcat(return_str, val.dptr);
        strcat(return_str, ",");
    }

    if(dc_error_has_no_error(err)) {
        dc_dbm_close(env, err, db);
    }

    dc_strcpy(env, val_str, return_str);
    free(return_str);
}