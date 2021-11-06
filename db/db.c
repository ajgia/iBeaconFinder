// generic db functions
#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_ndbm.h>
#include <dc_posix/dc_posix_env.h>
#include <sqlite3.h>

#include "ndbm.c"
typedef struct
{
    int type;
    char *name;  // db name
} database;
typedef struct
{
    database *super;
    DBM *db;
} db_nmdb;
void db_store(const struct dc_error *err, const struct dc_posix_env *env,
              const char *key, const char *val, database *db_info);

void db_store(const struct dc_error *err, const struct dc_posix_env *env,
              const char *key, const char *val, database *db_info)
{
    ndbm_ibeacons_store(err, env, key, val, db_info);
    // sqlite3_exec()
};

// db_fetch() { ndbm_ibeacons_fetch(); }
// db_open() { ndbm_ibeacons_open(); };
// db_close() { ndbm_ibeacons_close(); };

int main()
{
    db_nmdb *d;
    // cast to base type
    database *data = (database *)d;
    // foo((database *)d);
    // cast to derived type
    db_nmdb *b = (db_nmdb *)data;
}