#include "common.h"
#include "dbstuff.h"
#include "http_.h"
#include <dc_application/command_line.h>
#include <dc_application/config.h>
#include <dc_application/defaults.h>
#include <dc_application/environment.h>
#include <dc_application/options.h>
#include <dc_fsm/fsm.h>
#include <dc_network/common.h>
#include <dc_network/options.h>
#include <dc_network/server.h>
#include <dc_posix/dc_fcntl.h>
#include <dc_posix/dc_netdb.h>
#include <dc_posix/dc_posix_env.h>
#include <dc_posix/dc_signal.h>
#include <dc_posix/dc_stdlib.h>
#include <dc_posix/dc_string.h>
#include <dc_posix/dc_time.h>
#include <dc_posix/dc_unistd.h>
#include <dc_posix/sys/dc_socket.h>
#include <dc_util/dump.h>
#include <dc_util/streams.h>
#include <dc_util/types.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_REQUEST_SIZE 8000

/**
 * @brief Server info
 *
 */
struct server
{
    int client_socket_fd;
    struct http_request req;
    struct http_response res;
};

struct application_settings
{
    struct dc_opt_settings opts;
    struct dc_setting_bool *verbose;
    struct dc_setting_string *hostname;
    struct dc_setting_regex *ip_version;
    struct dc_setting_uint16 *port;
    struct dc_setting_bool *reuse_address;
    struct addrinfo *address;
    int server_socket_fd;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_signal = 0;

static struct dc_application_settings *create_settings(const struct dc_posix_env *env, struct dc_error *err);

static int
destroy_settings(const struct dc_posix_env *env, struct dc_error *err, struct dc_application_settings **psettings);

static int run(const struct dc_posix_env *env, struct dc_error *err, struct dc_application_settings *settings);

static void signal_handler(int signnum);

static void do_create_settings(const struct dc_posix_env *env, struct dc_error *err, void *arg);

static void do_create_socket(const struct dc_posix_env *env, struct dc_error *err, void *arg);

static void do_set_sockopts(const struct dc_posix_env *env, struct dc_error *err, void *arg);

static void do_bind(const struct dc_posix_env *env, struct dc_error *err, void *arg);

static void do_listen(const struct dc_posix_env *env, struct dc_error *err, void *arg);

static void do_setup(const struct dc_posix_env *env, struct dc_error *err, void *arg);

static bool do_accept(const struct dc_posix_env *env, struct dc_error *err, int *client_socket_fd, void *arg);

static void do_shutdown(const struct dc_posix_env *env, struct dc_error *err, void *arg);

static void do_destroy_settings(const struct dc_posix_env *env, struct dc_error *err, void *arg);

static void error_reporter(const struct dc_error *err);

void echo(const struct dc_posix_env *env, struct dc_error *err, int client_socket_fd);

static void trace(const struct dc_posix_env *env, const char *file_name, const char *function_name, size_t line_number);

static void write_displayer(const struct dc_posix_env *env, struct dc_error *err, const uint8_t *data, size_t count,
                            size_t file_position, void *arg);

static void read_displayer(const struct dc_posix_env *env, struct dc_error *err, const uint8_t *data, size_t count,
                           size_t file_position, void *arg);

void freeServerStruct(struct server *server);
int startProcessingFSM(const struct dc_posix_env *env, struct dc_error *err, int client_socket_fd);
int process(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int handle(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int get(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int put(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int invalid (const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief Reads from fd into char* until no more bytes
 *
 * @param env
 * @param err
 * @param fd
 * @param size
 */
void receive_data(  const struct dc_posix_env *env, struct dc_error *err, 
                    int fd,
                    char* dest,
                    char* buf,
                    size_t bufSize);
/**
 * @brief Incomplete key extraction with string parsing
 * 
 * @param input 
 * @param keydest 
 * @param sep1 
 * @param sep2 
 */
void extract_key(char *input, char *keydest, char *sep1, char *sep2);

static void trace_reporter(__attribute__((unused)) const struct dc_posix_env *env,
                           const char *file_name,
                           const char *function_name,
                           size_t line_number);


static void will_change_state(const struct dc_posix_env *env,
                              struct dc_error           *err,
                              const struct dc_fsm_info  *info,
                              int                        from_state_id,
                              int                        to_state_id);
static void did_change_state(const struct dc_posix_env *env,
                             struct dc_error           *err,
                             const struct dc_fsm_info  *info,
                             int                        from_state_id,
                             int                        to_state_id,
                             int                        next_id);
static void bad_change_state(const struct dc_posix_env *env,
                             struct dc_error           *err,
                             const struct dc_fsm_info  *info,
                             int                        from_state_id,
                             int                        to_state_id);

enum processing_states
{
    PROCESS = DC_FSM_USER_START,    // 2
    _GET,                           // 3
    _PUT,                           // 4
    INVALID                         // 5
};

int main (int argc, char *argv[])
{
    dc_error_reporter reporter;
    dc_posix_tracer tracer;
    struct dc_posix_env env;
    struct dc_error err;
    struct dc_application_info *info;
    int ret_val;
    struct sigaction sa;

    reporter = error_reporter;
    tracer = trace_reporter;
    tracer = NULL;
    dc_error_init(&err, reporter);
    dc_posix_env_init(&env, tracer);
    dc_memset(&env, &sa, 0, sizeof(sa));
    sa.sa_handler = &signal_handler;
    dc_sigaction(&env, &err, SIGINT, &sa, NULL);
    dc_sigaction(&env, &err, SIGTERM, &sa, NULL);

    info = dc_application_info_create(&env, &err, "iBeaconServer");
    ret_val = dc_application_run(&env, &err, info, create_settings, destroy_settings, run, dc_default_create_lifecycle, dc_default_destroy_lifecycle,
                                 NULL,
                                 argc, argv);
    dc_application_info_destroy(&env, &info);
    dc_error_reset(&err);

    return ret_val;
}

// TODO: add setting for location of beacon database
static struct dc_application_settings *create_settings (const struct dc_posix_env *env, struct dc_error *err)
{
    static const bool default_verbose = false;
    static const char *default_hostname = "localhost";
    static const char *default_ip = "IPv4";
    static const uint16_t default_port = DEFAULT_PORT;
    static const bool default_reuse = false;
    struct application_settings *settings;

    settings = dc_malloc(env, err, sizeof(struct application_settings));

    if(settings == NULL)
    {
        return NULL;
    }

    settings->opts.parent.config_path = dc_setting_path_create(env, err);
    settings->verbose = dc_setting_bool_create(env, err);
    settings->hostname = dc_setting_string_create(env, err);
    settings->ip_version = dc_setting_regex_create(env, err, "^IPv[4|6]");
    settings->port = dc_setting_uint16_create(env, err);
    settings->reuse_address = dc_setting_bool_create(env, err);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
    struct options opts[] =
            {
                    {(struct dc_setting *)settings->opts.parent.config_path, dc_options_set_path,   "config",  required_argument, 'c', "CONFIG",        dc_string_from_string, NULL,            dc_string_from_config, NULL},
                    {(struct dc_setting *)settings->verbose,                 dc_options_set_bool,   "verbose", no_argument,       'v', "VERBOSE",       dc_flag_from_string,   "verbose",       dc_flag_from_config,   &default_verbose},
                    {(struct dc_setting *)settings->hostname,                dc_options_set_string, "host",    required_argument, 'h', "HOST",          dc_string_from_string, "host",          dc_string_from_config, default_hostname},
                    {(struct dc_setting *)settings->ip_version,              dc_options_set_regex,  "ip",      required_argument, 'i', "IP",            dc_string_from_string, "ip",            dc_string_from_config, default_ip},
                    {(struct dc_setting *)settings->port,                    dc_options_set_uint16, "port",    required_argument, 'p', "PORT",          dc_uint16_from_string, "port",          dc_uint16_from_config, &default_port},
                    {(struct dc_setting *)settings->reuse_address,           dc_options_set_bool,   "force",   no_argument,       'f', "REUSE_ADDRESS", dc_flag_from_string,   "reuse_address", dc_flag_from_config,   &default_reuse},
            };
#pragma GCC diagnostic pop

    // note the trick here - we use calloc and add 1 to ensure the last line is all 0/NULL
    settings->opts.opts = dc_calloc(env, err, (sizeof(opts) / sizeof(struct options)) + 1, sizeof(struct options));
    dc_memcpy(env, settings->opts.opts, opts, sizeof(opts));
    settings->opts.flags = "c:vh:i:p:f";
    settings->opts.env_prefix = "iBeaconServer";

    return (struct dc_application_settings *)settings;
}

static int destroy_settings (const struct dc_posix_env *env, __attribute__ ((unused)) struct dc_error *err,
                            struct dc_application_settings **psettings)
{
    struct application_settings *app_settings;

    app_settings = (struct application_settings *)*psettings;
    dc_setting_bool_destroy(env, &app_settings->verbose);
    dc_setting_string_destroy(env, &app_settings->hostname);
    dc_setting_uint16_destroy(env, &app_settings->port);
    dc_free(env, app_settings->opts.opts, app_settings->opts.opts_size);
    dc_free(env, app_settings, sizeof(struct application_settings));

    if(env->null_free)
    {
        *psettings = NULL;
    }

    return 0;
}


static struct dc_server_lifecycle *create_server_lifecycle (const struct dc_posix_env *env, struct dc_error *err)
{
    struct dc_server_lifecycle *lifecycle;

    lifecycle = dc_server_lifecycle_create(env, err);
    dc_server_lifecycle_set_create_settings(env, lifecycle, do_create_settings);
    dc_server_lifecycle_set_create_socket(env, lifecycle, do_create_socket);
    dc_server_lifecycle_set_set_sockopts(env, lifecycle, do_set_sockopts);
    dc_server_lifecycle_set_bind(env, lifecycle, do_bind);
    dc_server_lifecycle_set_listen(env, lifecycle, do_listen);
    dc_server_lifecycle_set_setup(env, lifecycle, do_setup);
    dc_server_lifecycle_set_accept(env, lifecycle, do_accept);
    dc_server_lifecycle_set_shutdown(env, lifecycle, do_shutdown);
    dc_server_lifecycle_set_destroy_settings(env, lifecycle, do_destroy_settings);

    return lifecycle;
}

static void destroy_server_lifecycle (const struct dc_posix_env *env, struct dc_server_lifecycle **plifecycle)
{
    DC_TRACE(env);
    dc_server_lifecycle_destroy(env, plifecycle);
}


static int run (const struct dc_posix_env *env, __attribute__ ((unused)) struct dc_error *err,
               struct dc_application_settings *settings)
{
    int ret_val;
    struct dc_server_info *info;

    info = dc_server_info_create(env, err, "iBeaconServer", NULL, settings);

    if(dc_error_has_no_error(err))
    {
        dc_server_run(env, err, info, create_server_lifecycle, destroy_server_lifecycle);
        dc_server_info_destroy(env, &info);
    }

    if(dc_error_has_no_error(err))
    {
        ret_val = 0;
    }
    else
    {
        ret_val = -1;
    }

    return ret_val;
}

static void do_create_settings (const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct application_settings *app_settings;
    const char *ip_version;
    int family;

    DC_TRACE(env);
    app_settings = arg;
    ip_version = dc_setting_regex_get(env, app_settings->ip_version);

    if(dc_strcmp(env, ip_version, "IPv4") == 0)
    {
        family = PF_INET;
    }
    else
    {
        if(dc_strcmp(env, ip_version, "IPv6") == 0)
        {
            family = PF_INET6;
        }
        else
        {
            DC_ERROR_RAISE_USER(err, "Invalid ip_version", -1);
            family = 0;
        }
    }

    if(dc_error_has_no_error(err))
    {
        const char *hostname;

        hostname = dc_setting_string_get(env, app_settings->hostname);
        dc_network_get_addresses(env, err, family, SOCK_STREAM, hostname, &app_settings->address);
    }
}

static void do_create_socket (const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct application_settings *app_settings;
    int socket_fd;

    DC_TRACE(env);
    app_settings = arg;
    socket_fd = dc_network_create_socket(env, err, app_settings->address);

    if(dc_error_has_no_error(err))
    {
        app_settings = arg;
        app_settings->server_socket_fd = socket_fd;
    }
    else
    {
        socket_fd = -1;
    }
}

static void do_set_sockopts (const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct application_settings *app_settings;
    bool reuse_address;

    DC_TRACE(env);
    app_settings = arg;
    reuse_address = dc_setting_bool_get(env, app_settings->reuse_address);
    dc_network_opt_ip_so_reuse_addr(env, err, app_settings->server_socket_fd, reuse_address);
}

static void do_bind (const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct application_settings *app_settings;
    uint16_t port;

    DC_TRACE(env);
    app_settings = arg;
    port = dc_setting_uint16_get(env, app_settings->port);

    dc_network_bind(env,
                    err,
                    app_settings->server_socket_fd,
                    app_settings->address->ai_addr,
                    port);
}

static void do_listen (const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct application_settings *app_settings;
    int backlog;

    DC_TRACE(env);
    app_settings = arg;
    backlog = 5;
    dc_network_listen(env, err, app_settings->server_socket_fd, backlog);
}

static void do_setup (const struct dc_posix_env *env, __attribute__ ((unused)) struct dc_error *err,
                     __attribute__ ((unused)) void *arg)
{
    DC_TRACE(env);
}

static bool do_accept (const struct dc_posix_env *env, struct dc_error *err, int *client_socket_fd, void *arg)
{
    struct application_settings *app_settings;
    bool ret_val;

    DC_TRACE(env);
    app_settings = arg;
    ret_val = false;
    *client_socket_fd = dc_network_accept(env, err, app_settings->server_socket_fd);

    if(dc_error_has_error(err))
    {
        if(exit_signal == true && dc_error_is_errno(err, EINTR))
        {
            ret_val = true;
        }
    }
    else
    {
        // echo(env, err, *client_socket_fd);
        startProcessingFSM(env, err, *client_socket_fd);
    }

    return ret_val;
}

static void do_shutdown (const struct dc_posix_env *env, __attribute__ ((unused)) struct dc_error *err, __attribute__ ((unused)) void *arg)
{
    DC_TRACE(env);
}

static void
do_destroy_settings (const struct dc_posix_env *env, __attribute__ ((unused)) struct dc_error *err, void *arg)
{
    struct application_settings *app_settings;

    DC_TRACE(env);
    app_settings = arg;
    dc_freeaddrinfo(env, app_settings->address);
}

int startProcessingFSM (const struct dc_posix_env *env, struct dc_error *err, int client_socket_fd) {
    int ret_val;
    struct dc_fsm_info *fsm_info;
    static struct dc_fsm_transition transitions[] = {
        {DC_FSM_INIT, PROCESS, process},
        {PROCESS, _GET, get}, 
        {PROCESS, _PUT, put},
        {PROCESS, INVALID, invalid},
        {_GET, DC_FSM_EXIT, NULL},
        {_PUT, DC_FSM_EXIT, NULL},
        {INVALID, DC_FSM_EXIT, NULL},
    };

    ret_val = EXIT_SUCCESS;
    fsm_info = dc_fsm_info_create(env, err, "ProcessingFSM");
    // dc_fsm_info_set_will_change_state(fsm_info, will_change_state);
    dc_fsm_info_set_did_change_state(fsm_info, did_change_state);
    dc_fsm_info_set_bad_change_state(fsm_info, bad_change_state);

    if (dc_error_has_no_error(err)) {
        int from_state;
        int to_state;

        struct server *server = (struct server *)dc_malloc(env, err, sizeof(struct server));
        server->req.req_line = (struct request_line *)dc_malloc(env, err, sizeof(struct request_line));
        server->res.res_line = (struct response_line *)dc_malloc(env, err, sizeof(struct response_line));
        server->client_socket_fd = client_socket_fd;

        ret_val = dc_fsm_run(env, err, fsm_info, &from_state, &to_state, server, transitions);
        dc_fsm_info_destroy(env, &fsm_info);

        freeServerStruct(server);
    }
    else {
        printf("error");
    }
    return ret_val;
}

void freeServerStruct(struct server *server) {
    free(server->req.req_line->HTTP_VER);
    free(server->req.req_line->path);
    free(server->req.req_line->req_method);
    free(server->req.req_line);
    free(server->res.res_line);
    free(server);
}


void echo (const struct dc_posix_env *env, struct dc_error *err, int client_socket_fd)
{
    struct dc_dump_info *dump_info;
    struct dc_stream_copy_info *copy_info;

    dump_info = dc_dump_info_create(env, err, STDOUT_FILENO, dc_max_off_t(env));

    if(dc_error_has_no_error(err))
    {
        copy_info = dc_stream_copy_info_create(env, err, NULL, dc_dump_dumper, dump_info, NULL, NULL);

        if(dc_error_has_no_error(err))
        {
            dc_stream_copy(env, err, client_socket_fd, client_socket_fd, 1024, copy_info);

            if(dc_error_has_no_error(err))
            {
                dc_stream_copy_info_destroy(env, &copy_info);
            }
        }
        dc_dump_info_destroy(env, &dump_info);
    }
}



int process (const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    display("process");
    struct server *server = (struct server *)arg;
    int next_state;
    char* request;
    char* buffer;

    if(dc_error_has_error(err))
    {
        // some error handling
    }

    // allocate max size for request
    request = (char*)dc_calloc(env, err, MAX_REQUEST_SIZE, sizeof(char));
    buffer = (char*)dc_malloc(env, err, 1024);

    // read from client_socket_fd up to max size in request
    receive_data(env, err, server->client_socket_fd, request, buffer, 1024);

    dc_write(env, err, STDOUT_FILENO, request, strlen(request));

    // this will process the request and store in the server struct
    process_request(request, &server->req);

    free(request);
    free(buffer);

    printf("%s\n%s\n%s\n",  server->req.req_line->req_method, server->req.req_line->path, server->req.req_line->HTTP_VER);
    if ( strcmp(server->req.req_line->req_method, "GET") == 0 )
        next_state = _GET;
    else if ( strcmp(server->req.req_line->req_method, "PUT") == 0 )
        next_state = _PUT;
    else
        next_state = INVALID;

    return next_state;

    // display("in the fsm");
    // return _GET;
}

int get (const struct dc_posix_env *env, struct dc_error *err, void *arg) {
    display("get");
    struct server *server = (struct server *) arg;
    int next_state;
    char *val = (char*)calloc(1024, sizeof(char));

    // get from dbm, construct response, write response

    if (strstr(server->req.req_line->path, "all")) {
        // get all
        // some db_fetch call
        display("get all");
        db_fetch_all(env, err, val);
        printf("%s\n", val);
    }
    else if (strstr(server->req.req_line->path, "?")) {
        // get by id
        // construct id
        display("get by id baby");
        char *path = strdup(server->req.req_line->path);
        char *key;
        
        // extract_key(path, key, "?")
        key = strtok(path, "?"); // returns piece before "?"
        key = strtok(NULL, " "); // NOW we have key. strtok is weird

        printf("%s\n", key);

        db_fetch(env, err, key, val);
        printf("%s\n", val);
        free(path);
    } else {
        char *basicHTTPMessage = "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: 6\n\nHello\n\r\n\r\n";
        dc_write(env, err, server->client_socket_fd, basicHTTPMessage, strlen(basicHTTPMessage));
    }

    char* response = (char*)calloc(1024, sizeof(char));
    
    // char *basicHTTPMessage = "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: 23\n\nPut your response here\n\r\n\r\n";
    // TODO: structure this in proper http
    if (val) {
        char *start = "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: "; 
        sprintf(response, "%s%d\n\n%s\n\r\n\r\n", start, (strlen(val)+1), val);
        printf("%s", response);
        dc_write(env, err, server->client_socket_fd, response, strlen(response));
    }

    // exit
    if (dc_error_has_no_error(err))
    {
        dc_close(env, err, server->client_socket_fd);
    }

    free(val);
    free(response);
    next_state = DC_FSM_EXIT;
    return next_state;

    // return DC_FSM_EXIT;
}

int put (const struct dc_posix_env *env, struct dc_error *err, void *arg) {
    struct server *server = (struct server *)arg;
    int next_state;
    char *path = strdup(server->req.req_line->path);
    char *key;
    char *val;
        

    // extract_key(path, key, "?")
    key = strtok(path, "?"); // returns piece before "?"
    key = strtok(NULL, ":"); // now we have key. strtok is weird
    val = strtok(NULL, "");
    printf("%s%s", key, val);

    db_store(env, err, key, val);

    if (dc_error_has_no_error(err))
    {
        dc_close(env, err, server->client_socket_fd);
    }

    // TODO: respond with success/failure
    const char *basicHTTPMessage = "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: 6\n\nHello\n\r\n\r\n";
    dc_write(env, err, server->client_socket_fd, basicHTTPMessage, strlen(basicHTTPMessage));

    free(path);
    next_state = DC_FSM_EXIT;
    return next_state;
}

int invalid (const struct dc_posix_env *env, struct dc_error *err, void *arg) {
    struct server *server = (struct server *)arg;
    int next_state;

    const char *basicHTTPMessage = "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: 6\n\nHello\n\r\n\r\n";
    dc_write(env, err, server->client_socket_fd, basicHTTPMessage, strlen(basicHTTPMessage));

    if (dc_error_has_no_error(err))
    {
        dc_close(env, err, server->client_socket_fd);
    }

    next_state = DC_FSM_EXIT;
    return next_state;
}

int handle (const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int next_state;
    const char *response;

    response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
        "12\r\n\r\nHello World!\n";
    
    dc_write(env, err, server->client_socket_fd, response, strlen(response));
    if(dc_error_has_no_error(err))
    {
        dc_close(env, err, server->client_socket_fd);
    }

    next_state = DC_FSM_EXIT;
    return next_state;
}

void receive_data ( const struct dc_posix_env *env, struct dc_error *err, 
                    int fd,
                    char* dest,
                    char* buf,
                    size_t bufSize)
{
    ssize_t count;
    ssize_t totalWritten = 0;
    const char *EndOfHeaderDelimiter = "\r\n\r\n";

    while (((count = dc_read(env, err, fd, buf, bufSize)) > 0))
    {   
        dc_memcpy(env, (dest + totalWritten), buf, (size_t)count);
        totalWritten += count;

        char *endOfHeader = strstr(buf, EndOfHeaderDelimiter);
        if (endOfHeader) {
            printf("break");
            break;
        }
        // TODO: handle overflow problem
    }

}

void extract_key (char *input, char *keydest, char *sep1, char *sep2) {
    char *target = NULL;
    char *start, *end;

    if ( start == strstr( input, sep1 ) )
    {
        start += strlen( sep1 );
        if ( end == strstr( start, sep2 ) )
        {
            keydest = ( char * )malloc( end - start + 1 );
            memcpy( keydest, start, end - start );
            keydest[end - start] = '\0';
        }
    }

    if ( keydest ) printf( "%s\n", keydest );

}

void signal_handler (__attribute__ ((unused)) int signnum)
{
    printf("CAUGHT!\n");
    exit_signal = 1;
}

static void error_reporter (const struct dc_error *err)
{
    if(err->type == DC_ERROR_ERRNO)
    {
        fprintf(stderr, "ERROR: %s : %s : @ %zu : %d\n", err->file_name, err->function_name, err->line_number,
                err->errno_code);
    }
    else
    {
        fprintf(stderr, "ERROR: %s : %s : @ %zu : %d\n", err->file_name, err->function_name, err->line_number,
                err->err_code);
    }

    fprintf(stderr, "ERROR: %s\n", err->message);
}


static void trace_reporter (__attribute__((unused)) const struct dc_posix_env *env,
                           const char *file_name,
                           const char *function_name,
                           size_t line_number)
{
    fprintf(stdout, "TRACE: %s : %s : @ %zu\n", file_name, function_name, line_number);
}

static void trace ( const struct dc_posix_env *env, const char *file_name, const char *function_name,
      size_t line_number)
{
    fprintf(stdout, "TRACE: %s : %s : @ %zu\n", file_name, function_name, line_number);
}


static void will_change_state (const struct dc_posix_env *env,
                              struct dc_error           *err,
                              const struct dc_fsm_info  *info,
                              int                        from_state_id,
                              int                        to_state_id)
{
    printf("%s: will change %d -> %d\n", dc_fsm_info_get_name(info), from_state_id, to_state_id);
}

static void did_change_state (const struct dc_posix_env *env,
                             struct dc_error           *err,
                             const struct dc_fsm_info  *info,
                             int                        from_state_id,
                             int                        to_state_id,
                             int                        next_id)
{
    printf("%s: did change %d -> %d moving to %d\n", dc_fsm_info_get_name(info), from_state_id, to_state_id, next_id);
}

static void bad_change_state (const struct dc_posix_env *env,
                             struct dc_error           *err,
                             const struct dc_fsm_info  *info,
                             int                        from_state_id,
                             int                        to_state_id)
{
    printf("%s: bad change %d -> %d\n", dc_fsm_info_get_name(info), from_state_id, to_state_id);
}
