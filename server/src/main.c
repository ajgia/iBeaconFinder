#include "common.h"
#include <dc_application/command_line.h>
#include <dc_application/config.h>
#include <dc_application/defaults.h>
#include <dc_application/environment.h>
#include <dc_application/options.h>
#include <dc_network/common.h>
#include <dc_network/options.h>
#include <dc_network/server.h>
#include <dc_posix/dc_netdb.h>
#include <dc_posix/dc_signal.h>
#include <dc_posix/dc_string.h>
#include <dc_posix/dc_unistd.h>
#include <dc_posix/dc_posix_env.h>
#include <dc_posix/dc_stdlib.h>
#include <dc_posix/sys/dc_socket.h>
#include <dc_posix/sys/dc_socket.h>
#include <dc_util/dump.h>
#include <dc_util/streams.h>
#include <dc_util/types.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void error_reporter(const struct dc_error *err);
static void trace_reporter(const struct dc_posix_env *env, const char *file_name,
                           const char *function_name, size_t line_number);
static void quit_handler(int sig_num);
void receive_data(struct dc_posix_env *env, struct dc_error *err, int fd, size_t size);


static volatile sig_atomic_t exit_flag;

int main(void)
{
    dc_error_reporter reporter;
    dc_posix_tracer tracer;
    struct dc_error err;
    struct dc_posix_env env;
    const char *host_name;
    struct addrinfo hints;
    struct addrinfo *result;

    reporter = error_reporter;
    tracer = trace_reporter;
    tracer = NULL;
    dc_error_init(&err, reporter);
    dc_posix_env_init(&env, tracer);

    host_name = "localhost";
    dc_memset(&env, &hints, 0, sizeof(hints));
    hints.ai_family =  PF_INET; // PF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    dc_getaddrinfo(&env, &err, host_name, NULL, &hints, &result);

    if(dc_error_has_no_error(&err))
    {
        int server_socket_fd;

        // create socket
        server_socket_fd = dc_socket(&env, &err, result->ai_family, result->ai_socktype, result->ai_protocol);

        if(dc_error_has_no_error(&err))
        {
            struct sockaddr *sockaddr;
            in_port_t port;
            in_port_t converted_port;
            socklen_t sockaddr_size;

            sockaddr = result->ai_addr;

            // find processes' PIDs on this port:
            // sudo lsof -i:7123
            // kill a process:
            // kill -9 PID
            port = 7123;
            converted_port = htons(port);

            // think this is ipv4 or ipv6 stuff
            if(sockaddr->sa_family == AF_INET)
            {
                struct sockaddr_in *addr_in;

                addr_in = (struct sockaddr_in *)sockaddr;
                addr_in->sin_port = converted_port;
                sockaddr_size = sizeof(struct sockaddr_in);
            }
            else
            {
                if(sockaddr->sa_family == AF_INET6)
                {
                    struct sockaddr_in6 *addr_in;

                    addr_in = (struct sockaddr_in6 *)sockaddr;
                    addr_in->sin6_port = converted_port;
                    sockaddr_size = sizeof(struct sockaddr_in6);
                }
                else
                {
                    DC_ERROR_RAISE_USER(&err, "sockaddr->sa_family is invalid", -1);
                    sockaddr_size = 0;
                }
            }


            if(dc_error_has_no_error(&err))
            {
                // bind address (port) to socket
                dc_bind(&env, &err, server_socket_fd, sockaddr, sockaddr_size);

                if(dc_error_has_no_error(&err))
                {
                    int backlog;

                    backlog = 5;

                    // listen tells socket it should be capable of accepting incoming connections
                    dc_listen(&env, &err, server_socket_fd, backlog);

                    if(dc_error_has_no_error(&err))
                    {
                        struct sigaction old_action;

                        dc_sigaction(&env, &err, SIGINT, NULL, &old_action);

                        if(old_action.sa_handler != SIG_IGN)
                        {
                            struct sigaction new_action;

                            exit_flag = 0;
                            new_action.sa_handler = quit_handler;
                            sigemptyset(&new_action.sa_mask);
                            new_action.sa_flags = 0;
                            dc_sigaction(&env, &err, SIGINT, &new_action, NULL);

                            // while server has no errors, we'll accept connections. each request is a new connection.
                            // so in this loop is: accept, read, respond, close
                            // possibly start an fsm here
                            while(!(exit_flag) && dc_error_has_no_error(&err))
                            {
                                int client_socket_fd;

                                // accept a new (client) connection on socket. gets first in queue.
                                // creates a new socket for the connection. old socket is just for accepting connections, NOT data.
                                client_socket_fd = dc_accept(&env, &err, server_socket_fd, NULL, NULL);

                                if(dc_error_has_no_error(&err))
                                {

                                    // do something. we can now process messages with read and write
             
                                    // need to process message byte  per byte, avoid buffer scheme
                                    // server must respond to valid http requests (GET and PUT)

                                    // http://localhost:7123/index.html
                                    const char *hello = "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello world!";
                                    
                                    receive_data(&env, &err, client_socket_fd, 1024);

                                    char response[5000] = {0};
                                    

                                    dc_write(&env, &err, client_socket_fd, hello, strlen(hello));

                                    dc_close(&env, &err, client_socket_fd);
                                }
                                else
                                {
                                    if(err.type == DC_ERROR_ERRNO && err.errno_code == EINTR)
                                    {
                                        dc_error_reset(&err);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if(dc_error_has_no_error(&err))
        {
            dc_close(&env, &err, server_socket_fd);
        }
    }

    return EXIT_SUCCESS;
}



/**
 * @brief Reads from fd into buffer until no more bytes, and writes to STD_OUT as bytes are read.
 * 
 * @param env 
 * @param err 
 * @param fd 
 * @param size 
 */
void receive_data(struct dc_posix_env *env, struct dc_error *err, int fd, size_t size)
{
    // more efficient would be to allocate the buffer in the caller (main) so we don't have to keep
    // mallocing and freeing the same data over and over again.
    char *data;
    ssize_t count;

    data = dc_malloc(env, err, size);

    while(!(exit_flag) && (count = dc_read(env, err, fd, data, size)) > 0 && dc_error_has_no_error(err))
    {
        dc_write(env, err, STDOUT_FILENO, data, (size_t)count);
    }

    dc_free(env, data, size);
}

static void quit_handler(int sig_num) 
{
    exit_flag = 1;
}

static void error_reporter(const struct dc_error *err)
{
    fprintf(stderr, "Error: \"%s\" - %s : %s : %d @ %zu\n", err->message, err->file_name, err->function_name, err->errno_code, err->line_number);
}

static void trace_reporter(const struct dc_posix_env *env, const char *file_name,
                           const char *function_name, size_t line_number)
{
    fprintf(stderr, "Entering: %s : %s @ %zu %d\n", file_name, function_name, line_number, env->null_free);
}
