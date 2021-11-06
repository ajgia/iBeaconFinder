#include "ndbm.c"
// generic db functions
#include <sqlite3.h>
#include <stdlib.h>
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
              void *db, const char *key, const char *val, database *db_info);

void db_store(const struct dc_error *err, const struct dc_posix_env *env,
              void *db, const char *key, const char *val, database *db_info)
{
    ndbm_ibeacons_store(err, env, db, key, val);
    // sqlite3_exec()
};
void foo(database *data);
void foo(database *data) {}
// db_fetch() { ndbm_ibeacons_fetch(); }
// db_open() { ndbm_ibeacons_open(); };
// db_close() { ndbm_ibeacons_close(); };

int main()
{
    db_nmdb *d;
    database *data = (database *)d;
    foo((database *)d);
    db_nmdb *b = (db_nmdb *)data;
}