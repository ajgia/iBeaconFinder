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
    // TODO: these structs should be valid
    struct http_request req;
    struct http_response res;
    // these may need to be defined size arrays for mallocing.
    char *request;
    char *response;
};

static void error_reporter(const struct dc_error *err);
static void trace_reporter(const struct dc_posix_env *env,
                           const char *file_name, const char *function_name,
                           size_t line_number);

static void will_change_state(const struct dc_posix_env *env,
                              struct dc_error *err,
                              const struct dc_fsm_info *info, int from_state_id,
                              int to_state_id);
static void did_change_state(const struct dc_posix_env *env,
                             struct dc_error *err,
                             const struct dc_fsm_info *info, int from_state_id,
                             int to_state_id, int next_id);
static void bad_change_state(const struct dc_posix_env *env,
                             struct dc_error *err,
                             const struct dc_fsm_info *info, int from_state_id,
                             int to_state_id);

static void quit_handler(int sig_num);

int setup(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int _listen(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int _accept(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int process(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int handle(const struct dc_posix_env *env, struct dc_error *err, void *arg);
void receive_data(const struct dc_posix_env *env, struct dc_error *err, int fd,
                  size_t size);

enum application_states
{
    SETUP = DC_FSM_USER_START,  // 2
    LISTEN,
    ACCEPT,
    PROCESS,
    HANDLE
};

static volatile sig_atomic_t exit_flag;

int main(void)
{
    dc_error_reporter reporter;
    dc_posix_tracer tracer;
    struct dc_error err;
    struct dc_posix_env env;
    int ret_val;

    struct dc_fsm_info *fsm_info;
    static struct dc_fsm_transition transitions[] = {
        {DC_FSM_INIT, SETUP, setup}, {SETUP, LISTEN, _listen},
        {LISTEN, ACCEPT, _accept},   {ACCEPT, PROCESS, process},
        {PROCESS, HANDLE, handle},   {HANDLE, LISTEN, _listen}};

    reporter = error_reporter;
    tracer = trace_reporter;
    tracer = NULL;
    dc_error_init(&err, reporter);
    dc_posix_env_init(&env, tracer);
    ret_val = EXIT_SUCCESS;

    // FSM setup
    fsm_info = dc_fsm_info_create(&env, &err, "iBeaconServer");
    // dc_fsm_info_set_will_change_state(fsm_info, will_change_state);
    dc_fsm_info_set_did_change_state(fsm_info, did_change_state);
    dc_fsm_info_set_bad_change_state(fsm_info, bad_change_state);

    if (dc_error_has_no_error(&err))
    {
        int from_state;
        int to_state;

        struct server *server =
            (struct server *)dc_malloc(&env, &err, sizeof(struct server));

        ret_val = dc_fsm_run(&env, &err, fsm_info, &from_state, &to_state,
                             server, transitions);
        dc_fsm_info_destroy(&env, &fsm_info);
        free(server->response);
        free(server);
    }

    return ret_val;
}

int setup(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int next_state;

    server->host_name = "localhost";
    dc_memset(env, &(server->hints), 0, sizeof(server->hints));
    server->hints.ai_family = PF_INET;  // PF_INET6;
    server->hints.ai_socktype = SOCK_STREAM;
    server->hints.ai_flags = AI_CANONNAME;
    dc_getaddrinfo(env, err, server->host_name, NULL, &(server->hints),
                   &(server->result));
    server->response = (char *)calloc(1024, sizeof(char));
    strcat(server->response,
           "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: "
           "12\n\nHello World!");

    if (dc_error_has_no_error(err))
    {
        // create socket
        server->server_socket_fd =
            dc_socket(env, err, server->result->ai_family,
                      server->result->ai_socktype, server->result->ai_protocol);

        if (dc_error_has_no_error(err))
        {
            struct sockaddr *sockaddr;
            in_port_t port;
            in_port_t converted_port;
            socklen_t sockaddr_size;

            sockaddr = server->result->ai_addr;

            // find processes' PIDs on this port:
            // sudo lsof -i:7123
            // kill a process:
            // kill -9 PID
            port = 7123;
            converted_port = htons(port);

            // think this is ipv4 or ipv6 stuff
            if (sockaddr->sa_family == AF_INET)
            {
                struct sockaddr_in *addr_in;

                addr_in = (struct sockaddr_in *)sockaddr;
                addr_in->sin_port = converted_port;
                sockaddr_size = sizeof(struct sockaddr_in);
            }
            else
            {
                if (sockaddr->sa_family == AF_INET6)
                {
                    struct sockaddr_in6 *addr_in;

                    addr_in = (struct sockaddr_in6 *)sockaddr;
                    addr_in->sin6_port = converted_port;
                    sockaddr_size = sizeof(struct sockaddr_in6);
                }
                else
                {
                    DC_ERROR_RAISE_USER(err, "sockaddr->sa_family is invalid",
                                        -1);
                    sockaddr_size = 0;
                }
            }

            if (dc_error_has_no_error(err))
            {
                // bind address (port) to socket
                dc_bind(env, err, server->server_socket_fd, sockaddr,
                        sockaddr_size);

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
    int next_state;

    if (dc_error_has_error(err))
    {
        // some error handling
    }

    server->backlog = 5;
    // listen tells socket it should be capable of accepting incoming
    // connections
    dc_listen(env, err, server->server_socket_fd, server->backlog);

    if (dc_error_has_no_error(err))
    {
        struct sigaction old_action;

        dc_sigaction(env, err, SIGINT, NULL, &old_action);

        if (old_action.sa_handler != SIG_IGN)
        {
            struct sigaction new_action;

            exit_flag = 0;
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
    int next_state;

    server->client_socket_fd =
        dc_accept(env, err, server->server_socket_fd, NULL, NULL);
    next_state = PROCESS;
    return next_state;
}

int process(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int next_state;

    if (dc_error_has_error(err))
    {
        // some error handling
    }

    // read file and print to std out
    receive_data(env, err, server->client_socket_fd, 1024);
    // construct an http struct instance

    next_state = HANDLE;
    return next_state;
}

// TODO: fix this state. it doesn't operate properly
// flow of control doesn't reach this state in expected order
int handle(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct server *server = (struct server *)arg;
    int next_state;

    char *response =
        "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: "
        "12\n\nHello World!";
    dc_write(env, err, server->client_socket_fd, response, strlen(response));
    if (dc_error_has_no_error(err))
    {
        dc_close(env, err, server->client_socket_fd);
    }

    next_state = LISTEN;
    return next_state;
}

/**
 * @brief Reads from fd into buffer until no more bytes, and writes to STD_OUT
 * as bytes are read.
 *
 * @param env
 * @param err
 * @param fd
 * @param size
 */
void receive_data(const struct dc_posix_env *env, struct dc_error *err, int fd,
                  size_t size)
{
    // more efficient would be to allocate the buffer in the caller (main) so we
    // don't have to keep mallocing and freeing the same data over and over
    // again.
    char *data;
    ssize_t count;

    data = dc_malloc(env, err, size);

    while (!(exit_flag) && (count = dc_read(env, err, fd, data, size)) > 0 &&
           dc_error_has_no_error(err))
    {
        dc_write(env, err, STDOUT_FILENO, data, (size_t)count);
    }

    dc_free(env, data, size);
}

static void quit_handler(int sig_num) { exit_flag = 1; }

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
    fprintf(stderr, "Entering: %s : %s @ %zu %d\n", file_name, function_name,
            line_number, env->null_free);
}

static void will_change_state(const struct dc_posix_env *env,
                              struct dc_error *err,
                              const struct dc_fsm_info *info, int from_state_id,
                              int to_state_id)
{
    printf("%s: will change %d -> %d\n", dc_fsm_info_get_name(info),
           from_state_id, to_state_id);
}

static void did_change_state(const struct dc_posix_env *env,
                             struct dc_error *err,
                             const struct dc_fsm_info *info, int from_state_id,
                             int to_state_id, int next_id)
{
    printf("%s: did change %d -> %d moving to %d\n", dc_fsm_info_get_name(info),
           from_state_id, to_state_id, next_id);
}

static void bad_change_state(const struct dc_posix_env *env,
                             struct dc_error *err,
                             const struct dc_fsm_info *info, int from_state_id,
                             int to_state_id)
{
    printf("%s: bad change %d -> %d\n", dc_fsm_info_get_name(info),
           from_state_id, to_state_id);
}
