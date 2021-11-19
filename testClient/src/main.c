#include <dc_application/command_line.h>
#include <dc_application/config.h>
#include <dc_application/defaults.h>
#include <dc_application/environment.h>
#include <dc_application/options.h>
#include <dc_fsm/fsm.h>
#include <dc_network/client.h>
#include <dc_network/common.h>
#include <dc_network/options.h>
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
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// #include "common.h"
// #include "dbstuff.h"
// #include "http_.h"

/**
 * @brief client info
 *
 */

struct client
{
    const char *host_name;
    struct addrinfo hints;
    struct addrinfo *result;
    int backlog;
    int client_socket_fd;
    WINDOW *menu_window;
    WINDOW *display_window;
    int highlight;
    // TODO: these structs should be valid
    // struct http_request req;
    // struct http_response res;
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
int _setup_window(const struct dc_posix_env *env, struct dc_error *err,
                  void *arg);
int _setup(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int _await_input(const struct dc_posix_env *env, struct dc_error *err,
                 void *arg);
int _get_all(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int _by_key(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int _build_request(const struct dc_posix_env *env, struct dc_error *err,
                   void *arg);
int _await_response(const struct dc_posix_env *env, struct dc_error *err,
                    void *arg);
int _parse_response(const struct dc_posix_env *env, struct dc_error *err,
                    void *arg);
int _display_response(const struct dc_posix_env *env, struct dc_error *err,
                      void *arg);
enum application_states
{
    SETUP_WINDOW = DC_FSM_USER_START,  // 2
    SETUP,
    AWAIT_INPUT,
    GET_ALL,
    BY_KEY,
    BUILD_REQUEST,
    AWAIT_RESPONSE,
    PARSE_RESPONSE,
    DISPLAY_RESPONSE
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
        {DC_FSM_INIT, SETUP_WINDOW, _setup_window},
        {SETUP_WINDOW, SETUP, _setup},
        {SETUP, AWAIT_INPUT, _await_input},
        {AWAIT_INPUT, GET_ALL, _get_all},
        {AWAIT_INPUT, BY_KEY, _by_key},
        {GET_ALL, BUILD_REQUEST, _build_request},
        {BY_KEY, BUILD_REQUEST, _build_request},
        {BUILD_REQUEST, AWAIT_RESPONSE, _await_response},
        {AWAIT_RESPONSE, PARSE_RESPONSE, _parse_response},
        {PARSE_RESPONSE, DISPLAY_RESPONSE, _display_response},
        {DISPLAY_RESPONSE, AWAIT_INPUT, _await_input}};

    reporter = error_reporter;
    tracer = trace_reporter;
    tracer = NULL;
    dc_error_init(&err, reporter);
    dc_posix_env_init(&env, tracer);
    ret_val = EXIT_SUCCESS;

    // FSM setup
    fsm_info = dc_fsm_info_create(&env, &err, "iBeaconclient");
    // dc_fsm_info_set_will_change_state(fsm_info, will_change_state);
    // dc_fsm_info_set_did_change_state(fsm_info, did_change_state);
    // dc_fsm_info_set_bad_change_state(fsm_info, bad_change_state);

    if (dc_error_has_no_error(&err))
    {
        int from_state;
        int to_state;

        struct client *client =
            (struct client *)dc_malloc(&env, &err, sizeof(struct client));

        ret_val = dc_fsm_run(&env, &err, fsm_info, &from_state, &to_state,
                             client, transitions);
        dc_fsm_info_destroy(&env, &fsm_info);
        free(client->response);
        free(client);
    }

    return ret_val;
}
int _setup_window(const struct dc_posix_env *env, struct dc_error *err,
                  void *arg)
{
    struct client *client = (struct client *)arg;
    int next_state;

    client->highlight = 0;
    initscr();
    noecho();
    cbreak();

    int yMax, xMax;
    getmaxyx(stdscr, yMax, xMax);

    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);
    // Input window

    WINDOW *menu_window = newwin(4, xMax / 2, yMax - 8, xMax / 4);
    client->menu_window = menu_window;
    wattron(menu_window, COLOR_PAIR(1));
    box(menu_window, 0, 0);

    // num rows, num columns, begin y, begin x
    WINDOW *display_window = newwin(7, xMax / 2, yMax - 15, xMax / 4);
    client->display_window = display_window;
    wattron(display_window, COLOR_PAIR(2));
    box(display_window, 0, 0);

    wattroff(menu_window, COLOR_PAIR(1));
    wattroff(display_window, COLOR_PAIR(2));
    refresh();
    wrefresh(menu_window);
    wrefresh(display_window);
    keypad(menu_window, true);

    if (dc_error_has_no_error(err))
    {
        // go to next state
        next_state = SETUP;
        return next_state;
    }
}
int _setup(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    // set up socket
    struct client *client = (struct client *)arg;
    int next_state;

    client->host_name = "localhost";
    dc_memset(env, &(client->hints), 0, sizeof(client->hints));
    client->hints.ai_family = PF_INET;  // PF_INET6;
    client->hints.ai_socktype = SOCK_STREAM;
    client->hints.ai_flags = AI_CANONNAME;
    dc_getaddrinfo(env, err, client->host_name, NULL, &(client->hints),
                   &(client->result));

    if (dc_error_has_no_error(err))
    {
        // create socket
        client->client_socket_fd =
            dc_socket(env, err, client->result->ai_family,
                      client->result->ai_socktype, client->result->ai_protocol);

        if (dc_error_has_no_error(err))
        {
            struct sockaddr *sockaddr;
            in_port_t port;
            in_port_t converted_port;
            socklen_t sockaddr_size;

            sockaddr = client->result->ai_addr;

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
                dc_connect(env, err, client->client_socket_fd, sockaddr,
                           sockaddr_size);

                // go to next state
                next_state = AWAIT_INPUT;
                return next_state;
            }
        }
    }
}
int _await_input(const struct dc_posix_env *env, struct dc_error *err,
                 void *arg)
{
    struct client *client = (struct client *)arg;
    int next_state;
    char *choices[2] = {"GET_ALL", "GET_BY_KEY"};
    int choice;

    int display_window_ymax, display_window_xmax;
    getmaxyx(client->display_window, display_window_ymax, display_window_xmax);
    while (1)
    {
        for (size_t i = 0; i < 2; i++)
        {
            if (i == client->highlight)
            {
                wattron(client->menu_window, A_REVERSE);
            }
            mvwprintw(client->menu_window, i + 1, 1, choices[i]);
            wattroff(client->menu_window, A_REVERSE);
        }
        choice = wgetch(client->menu_window);
        switch (choice)
        {
            case KEY_UP:
                client->highlight--;
                if (client->highlight == -1)
                {
                    client->highlight = 0;
                }
                break;
            case KEY_DOWN:
                client->highlight++;
                if (client->highlight == 2)
                {
                    client->highlight = 1;
                }
                break;
                // enter
            case 10:
                mvwprintw(client->display_window, display_window_ymax / 2,
                          (display_window_xmax / 2) - 2, "                   ");
                mvwprintw(client->display_window, display_window_ymax / 2,
                          (display_window_xmax / 2) - 2, "%s",
                          choices[client->highlight]);
                if (choices[client->highlight] == "GET_ALL")
                {
                    next_state = GET_ALL;
                }
                if (choices[client->highlight] == "GET_BY_KEY")
                {
                    next_state = BY_KEY;
                }
                wrefresh(client->display_window);
                return next_state;
                break;
            default:
                break;
        }
    }
    return next_state;
}
//  SETUP_WINDOW = DC_FSM_USER_START,  // 2
// SETUP,
// AWAIT_INPUT,
// GET_ALL,
// BY_KEY,
// BUILD_REQUEST,
// AWAIT_RESPONSE,
// PARSE_RESPONSE,
// DISPLAY_RESPONSE
int _get_all(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int next_state;
    char data[1024];
    sprintf(data, " GET /ibeacons/data?all HTTP/1.0");
    dc_write(env, err, client->client_socket_fd, data, dc_strlen(env, data));
    next_state = BUILD_REQUEST;
    return next_state;
}
int _by_key(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int next_state;

    next_state = BUILD_REQUEST;
    return next_state;
}
int _build_request(const struct dc_posix_env *env, struct dc_error *err,
                   void *arg)
{
    struct client *client = (struct client *)arg;
    int next_state;

    next_state = AWAIT_RESPONSE;
    return next_state;
}
int _await_response(const struct dc_posix_env *env, struct dc_error *err,
                    void *arg)
{
    struct client *client = (struct client *)arg;
    int next_state;

    next_state = PARSE_RESPONSE;
    return next_state;
}
int _parse_response(const struct dc_posix_env *env, struct dc_error *err,
                    void *arg)
{
    struct client *client = (struct client *)arg;
    int next_state;
    int display_window_ymax, display_window_xmax;
    char response[1024];
    getmaxyx(client->display_window, display_window_ymax, display_window_xmax);

    strcpy(response, "MAJ-MIN : ");
    mvwprintw(client->display_window, 1, 1, "%s", response);

    wrefresh(client->display_window);
    next_state = DISPLAY_RESPONSE;
    return next_state;
}
int _display_response(const struct dc_posix_env *env, struct dc_error *err,
                      void *arg)
{
    struct client *client = (struct client *)arg;
    int next_state;

    next_state = AWAIT_INPUT;
    return next_state;
}
// int _listen(const struct dc_posix_env *env, struct dc_error *err, void *arg)
// {
//     struct client *client = (struct client *)arg;
//     int next_state;

//     if (dc_error_has_error(err))
//     {
//         // some error handling
//     }

//     client->backlog = 5;
//     // listen tells socket it should be capable of accepting incoming
//     // connections
//     dc_listen(env, err, client->client_socket_fd, client->backlog);

//     if (dc_error_has_no_error(err))
//     {
//         struct sigaction old_action;

//         dc_sigaction(env, err, SIGINT, NULL, &old_action);

//         if (old_action.sa_handler != SIG_IGN)
//         {
//             struct sigaction new_action;

//             exit_flag = 0;
//             new_action.sa_handler = quit_handler;
//             sigemptyset(&new_action.sa_mask);
//             new_action.sa_flags = 0;
//             dc_sigaction(env, err, SIGINT, &new_action, NULL);

//             next_state = ACCEPT;
//             return next_state;
//         }
//     }
// }

// int _accept(const struct dc_posix_env *env, struct dc_error *err, void *arg)
// {
//     struct client *client = (struct client *)arg;
//     int next_state;

//     client->client_socket_fd =
//         dc_accept(env, err, client->client_socket_fd, NULL, NULL);
//     next_state = PROCESS;
//     return next_state;
// }

// int process(const struct dc_posix_env *env, struct dc_error *err, void *arg)
// {
//     struct client *client = (struct client *)arg;
//     int next_state;

//     if (dc_error_has_error(err))
//     {
//         // some error handling
//     }

//     // read file and print to std out
//     receive_data(env, err, client->client_socket_fd, 1024);
//     // construct an http struct instance

//     next_state = HANDLE;
//     return next_state;
// }

// // TODO: fix this state. it doesn't operate properly
// // flow of control doesn't reach this state in expected order
// int handle(const struct dc_posix_env *env, struct dc_error *err, void *arg)
// {
//     struct client *client = (struct client *)arg;
//     int next_state;

//     char *response =
//         "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: "
//         "12\n\nHello World!";
//     dc_write(env, err, client->client_socket_fd, response, strlen(response));
//     if (dc_error_has_no_error(err))
//     {
//         dc_close(env, err, client->client_socket_fd);
//     }

//     next_state = LISTEN;
//     return next_state;
// }

// /**
//  * @brief Reads from fd into buffer until no more bytes, and writes to
//  * STD_OUT as bytes are read.
//  *
//  * @param env
//  * @param err
//  * @param fd
//  * @param size
//  */
// void receive_data(const struct dc_posix_env *env, struct dc_error *err, int
// fd,
//                   size_t size)
// {
//     // more efficient would be to allocate the buffer in the caller (main)
//     // so we don't have to keep mallocing and freeing the same data over and
//     // over again.
//     char *data;
//     ssize_t count;

//     data = dc_malloc(env, err, size);

//     while (!(exit_flag) && (count = dc_read(env, err, fd, data, size)) > 0 &&
//            dc_error_has_no_error(err))
//     {
//         dc_write(env, err, STDOUT_FILENO, data, (size_t)count);
//     }

//     dc_free(env, data, size);
// }

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
