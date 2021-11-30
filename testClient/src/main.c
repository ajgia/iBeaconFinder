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

#include "common.h"
#include "http_.h"
#define MAX_SIZE 8192
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
    struct http_response res;
    // these may need to be defined size arrays for mallocing.
    char *request;
    char *response;
};
int receive_data(const struct dc_posix_env *env, struct dc_error *err, int fd,
                 char *dest, size_t bufSize, void *arg);
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
        {DISPLAY_RESPONSE, SETUP, _await_input}};

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
        client->res.stat_line = (struct status_line *)dc_malloc(
            &env, &err, sizeof(struct status_line));
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
    curs_set(0);
    int yMax, xMax;
    getmaxyx(stdscr, yMax, xMax);

    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);
    // Input window

    WINDOW *menu_window = newwin(4, xMax / 2, yMax - 8, xMax / 4);
    client->menu_window = menu_window;
    int menu_maxx, menu_maxy;
    getmaxyx(menu_window, menu_maxx, menu_maxy);

    WINDOW *menu_window_border =
        newwin(6, (xMax / 2) + 2, yMax - 9, (xMax / 4) - 1);
    wattron(menu_window_border, COLOR_PAIR(1));
    box(menu_window_border, 0, 0);

    // num rows, num columns, begin y, begin x
    WINDOW *display_window = newwin(7, xMax / 2, yMax - 20, xMax / 4);
    client->display_window = display_window;
    WINDOW *display_window_border =
        newwin(9, (xMax / 2) + 2, yMax - 21, (xMax / 4) - 1);

    wattron(display_window_border, COLOR_PAIR(2));
    box(display_window_border, 0, 0);

    wattroff(menu_window_border, COLOR_PAIR(1));
    wattroff(display_window_border, COLOR_PAIR(2));
    refresh();
    wrefresh(display_window_border);
    wrefresh(menu_window_border);
    wrefresh(display_window);
    wrefresh(menu_window);
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
        wclear(client->menu_window);
        for (size_t i = 0; i < 2; i++)
        {
            if (i == client->highlight)
            {
                wattron(client->menu_window, A_REVERSE);
            }
            mvwprintw(client->menu_window, i, 0, choices[i]);
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
                wclear(client->display_window);

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
    char input[1024];
    char data[1024];

    mvwprintw(client->menu_window, 2, 0, "ENTER KEY: ");
    curs_set(1);
    wrefresh(client->menu_window);
    echo();

    wgetstr(client->menu_window, input);
    curs_set(0);
    noecho();

    sprintf(data, " GET /ibeacons/data?%s HTTP/1.0\r\n\r\n", input);
    dc_write(env, err, client->client_socket_fd, data, dc_strlen(env, data));
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
    char *buffer;
    char *response = client->response;

    response = (char *)dc_calloc(env, err, MAX_SIZE, sizeof(char));
    buffer = (char *)dc_malloc(env, err, 1024);

    wclear(client->display_window);
    mvwprintw(client->display_window, 1, 1, "Waiting for data...", response);

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
    mvwprintw(client->display_window, 1, 1, "K:V", response);
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
int receive_data(const struct dc_posix_env *env, struct dc_error *err, int fd,
                 char *dest, size_t bufSize, void *arg)
{
    struct client *client = (struct client *)arg;
    ssize_t count;
    ssize_t totalWritten = 0;
    const char *endOfHeadersDelimiter = "\r\n\r\n";
    char *endOfHeaders;
    ssize_t spaceInDest;
    int totalLength = MAX_REQUEST_SIZE;

    bool foundEndOfHeaders = false;

    while (totalWritten < totalLength &&
           ((count = dc_read(env, err, fd, dest + totalWritten, bufSize)) != 0))
    {
        // check space remaining. if going over, abort.
        spaceInDest = MAX_REQUEST_SIZE - 1 - totalWritten;
        if (count > spaceInDest)
        {
            return EXIT_FAILURE;
        }

        totalWritten += count;

        if (!foundEndOfHeaders)
        {
            endOfHeaders = strstr(dest, endOfHeadersDelimiter);
            if (endOfHeaders)
            {
                foundEndOfHeaders = true;
                int headerLength =
                    endOfHeaders - dest + strlen(endOfHeadersDelimiter);
                process_content_length(dest, client->response);
                totalLength = headerLength + client->res.content_length;
            }
        }
    }
    return EXIT_SUCCESS;
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
