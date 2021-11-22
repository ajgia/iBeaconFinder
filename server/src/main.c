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

#include "common.h"
#include "dbstuff.h"
#include "http_.h"

#define MAX_SIZE 8192

/**
 * @brief Server info
 *
 */

struct server
{
    const char *host_name;
    struct addrinfo hints;
    struct addrinfo *result;
    int backlog;
    int server_socket_fd;
    int client_socket_fd;
    struct http_request req;
    struct http_response res;

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
    } settings;
};



static struct dc_application_settings *create_settings(const struct dc_posix_env *env, struct dc_error *err);

static int
destroy_settings(const struct dc_posix_env *env, struct dc_error *err, struct dc_application_settings **psettings);

static void error_reporter(const struct dc_error *err);

static void trace_reporter(const struct dc_posix_env *env, const char *file_name, const char *function_name, size_t line_number);

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

static void quit_handler(int sig_num);
static void signal_handler(int signnum);


int setup(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int _listen(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int _accept(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int process(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int handle(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int get(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int put(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int invalid (const struct dc_posix_env *env, struct dc_error *err, void *arg);
void receive_data(  const struct dc_posix_env *env, struct dc_error *err, 
                    int fd,
                    char* dest,
                    char* buf,
                    size_t bufSize);

enum application_states
{
    SETUP = DC_FSM_USER_START,  // 2
    LISTEN,     // 3
    ACCEPT,     // 4
    PROCESS,    // 5
    _GET,        // 6
    _PUT,       // 7
    INVALID     // 8
};

static volatile sig_atomic_t exit_flag;
static volatile sig_atomic_t exit_signal = 0;


int main(void)
{
    dc_error_reporter               reporter;
    dc_posix_tracer                 tracer;
    struct dc_posix_env             env;
    struct dc_error                 err;
    struct dc_application_info      *info;
    int                             ret_val;
    struct sigaction                sa;

    reporter                        = error_reporter;
    tracer                          = trace_reporter;
    tracer                          = NULL;
    dc_error_init(&err, reporter);
    dc_posix_env_init(&env, tracer);
    dc_memset(&env, &sa, 0, sizeof(sa));
    sa.sa_handler = &signal_handler;
    dc_sigaction(&env, &err, SIGINT, &sa, NULL);
    dc_sigaction(&env, &err, SIGTERM, &sa, NULL);
    ret_val  = EXIT_SUCCESS;

    struct dc_fsm_info *fsm_info;
    static struct dc_fsm_transition transitions[] = {
        {DC_FSM_INIT, SETUP, setup},
        {SETUP, LISTEN, _listen},
        {LISTEN, ACCEPT, _accept},
        {ACCEPT, PROCESS, process},
        {PROCESS, _GET, get}, 
        {PROCESS, _PUT, put},
        {PROCESS, INVALID, invalid},
        {_GET, LISTEN, _listen},
        {_PUT, LISTEN, _listen},
        {INVALID, LISTEN, _listen}
        };

    // FSM setup
    fsm_info = dc_fsm_info_create(&env, &err, "iBeaconServer");
    // dc_fsm_info_set_will_change_state(fsm_info, will_change_state);
    dc_fsm_info_set_did_change_state(fsm_info, did_change_state);
    dc_fsm_info_set_bad_change_state(fsm_info, bad_change_state);

    if(dc_error_has_no_error(&err))
    {
        int            from_state;
        int            to_state;

        struct server *server =
            (struct server *)dc_malloc(&env, &err, sizeof(struct server));
        server->req.req_line = dc_malloc(&env, &err, sizeof(struct request_line));
        server->res.res_line = dc_malloc(&env, &err, sizeof(struct response_line));

        ret_val               = dc_fsm_run(&env, &err, fsm_info, &from_state, &to_state, server, transitions);
        dc_fsm_info_destroy(&env, &fsm_info);

        free(server->req.req_line);
        free(server->res.res_line);
        free(server);
    }

    return ret_val;
}

static struct dc_application_settings *create_settings(const struct dc_posix_env *env, struct dc_error *err)
{
    static const bool default_verbose = false;
    static const char *default_hostname = "localhost";
    static const char *default_ip = "IPv4";
    static const uint16_t default_port = 80; // TODO: replace this with header value
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
    settings->opts.env_prefix = "DC_ECHO_";

    return (struct dc_application_settings *)settings;
}

static int destroy_settings(const struct dc_posix_env *env, __attribute__ ((unused)) struct dc_error *err,
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

int setup(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int            next_state;

    // server->host_name = "Alexanders-MacBook-Pro.local";
    server->host_name = "localhost";
    dc_memset(env, &(server->hints), 0, sizeof(server->hints));
    server->hints.ai_family   = PF_INET;    // PF_INET6;
    server->hints.ai_socktype = SOCK_STREAM;
    server->hints.ai_flags = AI_CANONNAME;
    dc_getaddrinfo(env, err, server->host_name, NULL, &(server->hints),
                   &(server->result));

    server->server_socket_fd = dc_network_create_socket(env, err, server->result);
    if (dc_error_has_no_error(err))
    {
        // create socket
        // server->server_socket_fd =
        //     dc_socket(env, err, server->result->ai_family, server->result->ai_socktype, server->result->ai_protocol);

        if(dc_error_has_no_error(err))
        {
            struct sockaddr *sockaddr;
            in_port_t        port;
            in_port_t        converted_port;
            socklen_t        sockaddr_size;

            sockaddr       = server->result->ai_addr;

            // find processes' PIDs on this port:
            // sudo lsof -i:7123
            // kill a process:
            // kill -9 PID
            port           = 80;
            converted_port = htons(port);

            // think this is ipv4 or ipv6 stuff
            if(sockaddr->sa_family == AF_INET)
            {
                struct sockaddr_in *addr_in;

                addr_in           = (struct sockaddr_in *)sockaddr;
                addr_in->sin_port = converted_port;
                sockaddr_size     = sizeof(struct sockaddr_in);
            }
            else
            {
                if(sockaddr->sa_family == AF_INET6)
                {
                    struct sockaddr_in6 *addr_in;

                    addr_in            = (struct sockaddr_in6 *)sockaddr;
                    addr_in->sin6_port = converted_port;
                    sockaddr_size      = sizeof(struct sockaddr_in6);
                }
                else
                {
                    DC_ERROR_RAISE_USER(err, "sockaddr->sa_family is invalid", -1);
                    sockaddr_size = 0;
                }
            }

            if(dc_error_has_no_error(err))
            {
                // bind address (port) to socket
                dc_bind(env, err, server->server_socket_fd, sockaddr, sockaddr_size);

                // go to next state
                next_state = LISTEN;
                return next_state;
            }
        }
    }
}

int _listen(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int            next_state;

    if(dc_error_has_error(err))
    {
        // some error handling
    }

    server->backlog = 5;
    // listen tells socket it should be capable of accepting incoming
    // connections
    dc_network_listen(env, err, server->server_socket_fd, server->backlog);

    if(dc_error_has_no_error(err))
    {
        struct sigaction old_action;

        dc_sigaction(env, err, SIGINT, NULL, &old_action);

        if(old_action.sa_handler != SIG_IGN)
        {
            struct sigaction new_action;

            exit_flag             = 0;
            new_action.sa_handler = quit_handler;
            sigemptyset(&new_action.sa_mask);
            new_action.sa_flags = 0;
            dc_sigaction(env, err, SIGINT, &new_action, NULL);

            next_state = ACCEPT;
            return next_state;
        }
    }
}

int _accept(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int            next_state;

    server->client_socket_fd = dc_network_accept(env, err, server->server_socket_fd);
    next_state               = PROCESS;
    return next_state;
}

int process(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int next_state;
    char* request;
    char* buffer;

    if(dc_error_has_error(err))
    {
        // some error handling
    }

    // allocate max size for request
    request = (char*)dc_calloc(env, err, MAX_SIZE, sizeof(char));
    buffer = (char*)dc_malloc(env, err, 1024);

    // read from client_socket_fd up to max size in request
    receive_data(env, err, server->client_socket_fd, request, buffer, 1024);

    // dc_write(env, err, STDOUT_FILENO, request, strlen(request));

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
}

int get(const struct dc_posix_env *env, struct dc_error *err, void *arg) {
    display("get baby");
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
    next_state = LISTEN;
    return next_state;
}

int put(const struct dc_posix_env *env, struct dc_error *err, void *arg) {
    display("put baby");
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
    char *basicHTTPMessage = "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: 6\n\nHello\n\r\n\r\n";
    dc_write(env, err, server->client_socket_fd, basicHTTPMessage, strlen(basicHTTPMessage));

    free(path);
    next_state = LISTEN;
    return next_state;
}

int invalid (const struct dc_posix_env *env, struct dc_error *err, void *arg) {
    display("invalid baby");
    struct server *server = (struct server *)arg;
    int next_state;

    char *basicHTTPMessage = "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: 6\n\nHello\n\r\n\r\n";
    dc_write(env, err, server->client_socket_fd, basicHTTPMessage, strlen(basicHTTPMessage));

    if (dc_error_has_no_error(err))
    {
        dc_close(env, err, server->client_socket_fd);
    }

    next_state = LISTEN;
    return next_state;
}

int handle(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int next_state;
    char *response;

    response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
        "12\r\n\r\nHello World!\n";
    
    dc_write(env, err, server->client_socket_fd, response, strlen(response));
    if(dc_error_has_no_error(err))
    {
        dc_close(env, err, server->client_socket_fd);
    }

    next_state = LISTEN;
    return next_state;
}

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
                    size_t bufSize)
{
    ssize_t count;
    ssize_t totalWritten = 0;
    const char *EndOfHeaderDelimiter = "\r\n\r\n";

    while (((count = dc_read(env, err, fd, buf, bufSize)) > 0))
    {   
        dc_memcpy(env, (dest + totalWritten), buf, count);
        totalWritten += count;

        char *endOfHeader = strstr(buf, EndOfHeaderDelimiter);
        if (endOfHeader) {
            break;
        }
        // TODO: handle overflow problem
    }

}

void extract_key(char *input, char *keydest, char *sep1, char *sep2) {
    char *target = NULL;
    char *start, *end;

    if ( start = strstr( input, sep1 ) )
    {
        start += strlen( sep1 );
        if ( end = strstr( start, sep2 ) )
        {
            keydest = ( char * )malloc( end - start + 1 );
            memcpy( keydest, start, end - start );
            keydest[end - start] = '\0';
        }
    }

    if ( keydest ) printf( "%s\n", keydest );

}

static void quit_handler(int sig_num)
{
    printf("CAUGHT!\n");
    exit_flag = 1;
}

void signal_handler(__attribute__ ((unused)) int signnum)
{
    printf("caught\n");
    exit_signal = 1;
}


static void error_reporter(const struct dc_error *err)
{
    fprintf(stderr,
            "Error: \"%s\" - %s : %s : %d @ %zu\n",
            err->message,
            err->file_name,
            err->function_name,
            err->errno_code,
            err->line_number);
}

static void
trace_reporter(const struct dc_posix_env *env, const char *file_name, const char *function_name, size_t line_number)
{
    fprintf(stderr, "Entering: %s : %s @ %zu %d\n", file_name, function_name, line_number, env->null_free);
}

static void will_change_state(const struct dc_posix_env *env,
                              struct dc_error           *err,
                              const struct dc_fsm_info  *info,
                              int                        from_state_id,
                              int                        to_state_id)
{
    printf("%s: will change %d -> %d\n", dc_fsm_info_get_name(info), from_state_id, to_state_id);
}

static void did_change_state(const struct dc_posix_env *env,
                             struct dc_error           *err,
                             const struct dc_fsm_info  *info,
                             int                        from_state_id,
                             int                        to_state_id,
                             int                        next_id)
{
    printf("%s: did change %d -> %d moving to %d\n", dc_fsm_info_get_name(info), from_state_id, to_state_id, next_id);
}

static void bad_change_state(const struct dc_posix_env *env,
                             struct dc_error           *err,
                             const struct dc_fsm_info  *info,
                             int                        from_state_id,
                             int                        to_state_id)
{
    printf("%s: bad change %d -> %d\n", dc_fsm_info_get_name(info), from_state_id, to_state_id);
}
