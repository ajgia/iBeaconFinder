#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_ndbm.h>
#include <dc_posix/dc_posix_env.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DB_NAME "beacons"
datum fetch(DBM *db, const char *maj, const char *min,
            const struct dc_error *err, const struct dc_posix_env *env);
static void error_reporter(const struct dc_error *err);
static void trace_reporter(const struct dc_posix_env *env,
                           const char *file_name, const char *function_name,
                           size_t line_number);
void store(DBM *db, const char *maj, const char *min, const char *gps,
           const char *timestamp, int type, const struct dc_error *err,
           const struct dc_posix_env *env);
int main(int argc, char *argv[])
{
    dc_error_reporter reporter;
    dc_posix_tracer tracer;
    struct dc_error err;
    struct dc_posix_env env;

    reporter = error_reporter;
    tracer = trace_reporter;
    tracer = NULL;
    dc_error_init(&err, reporter);
    dc_posix_env_init(&env, tracer);
    DBM *db;
    db = dc_dbm_open(&env, &err, DB_NAME, DC_O_RDWR | DC_O_CREAT, 0600);
    if (dc_error_has_error(&err))
    {
        return EXIT_FAILURE;
    }
    // store(db, "major", "minor", "12.45-12.53", "35.325", DBM_INSERT, &err,
    //       &env);
    datum get_data;
    get_data = fetch(db, "major", "minor", &err, &env);
    printf("%s", get_data.dptr);
    dc_dbm_close(&env, &err, db);
    return EXIT_SUCCESS;
}
datum fetch(DBM *db, const char *maj, const char *min,
            const struct dc_error *err, const struct dc_posix_env *env)
{
    datum dat;
    char keystr[50];
    sprintf(keystr, "%s-%s", maj, min);
    datum key = {keystr, strlen(keystr)};
    dat = dc_dbm_fetch(env, err, db, key);
    printf("fetched: %s\n", dat.dptr);
    return key;
}
void store(DBM *db, const char *maj, const char *min, const char *gps,
           const char *timestamp, int type, const struct dc_error *err,
           const struct dc_posix_env *env)
{
    char keystr[50];
    char valstr[50];
    sprintf(keystr, "%s-%s", maj, min);
    sprintf(valstr, "%s-%s", gps, timestamp);
    datum key = {keystr, strlen(keystr)};
    datum val = {valstr, strlen(valstr)};

    dc_dbm_store(env, err, db, key, val, type);
    printf("Stored succesfully\n");
}
static void error_reporter(const struct dc_error *err)
{
    fprintf(stderr, "Error: \"%s\" - %s : %s : %d @ %zu\n", err->message,
            err->file_name, err->function_name, err->errno_code,
            err->line_number);
}

static void trace_reporter(const struct dc_posix_env *env,
                           const char *file_name, const char *function_name,
                           size_t line_number)
{
    fprintf(stderr, "Entering: %s : %s @ %zu\n", file_name, function_name,
            line_number);
}