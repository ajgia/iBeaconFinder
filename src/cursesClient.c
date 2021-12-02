#include "common.h"
#include "http_.h"
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

// freebsd libraries
// #include <netinet/in.h>



#define MAX_SIZE 8192

/**
 * @brief Client info
 *
 */
struct client
{
    const char          *host_name;
    struct addrinfo      hints;
    struct addrinfo     *result;
    int                  backlog;
    int                  client_socket_fd;
    WINDOW              *menu_window;
    WINDOW              *display_window;
    int                  highlight;
    // TODO: these structs should be valid
    // struct http_request req;
    struct http_response res;
    // these may need to be defined size arrays for mallocing.
    char                *request;
    char                *response;
};

static void error_reporter(const struct dc_error *err);
static void
trace_reporter(const struct dc_posix_env *env, const char *file_name, const char *function_name, size_t line_number);

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

/**
 * @brief Reads an HTTP response from int file descriptor into destination
 *
 * @param env
 * @param err
 * @param fd
 * @param dest
 * @param bufSize
 * @param arg
 * @return int
 */
int receive_data(const struct dc_posix_env *env, struct dc_error *err, int fd, char *dest, size_t bufSize, void *arg);
/**
 * @brief SETUP_WINDOW state of FSM calls this - initializes curses GUI
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int setup_window(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief SETUP state of FSM calls this - performs network setup
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int setup(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief AWAIT_INPUT state of FSM calls this - displays choices to user and
 * awaits input
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int await_input(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief GET_ALL state of FSM calls this - makes database get_all request to
 * server
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int get_all(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief BY_KEY state of FSM calls this - makes database request with key to
 * server
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int by_key(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief BUILD_REQUEST state of FSM calls this - constructs HTTP request
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int build_request(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief AWAIT_RESPONSE state of FSM calls this - reads from server file
 * descriptor
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int await_response(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief PARSE_RESPONSE state of FSM calls this - processes server response
 * string into a struct
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int parse_response(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief DISPLAY_RESPONSE state of FSM calls this - dislays response in curses
 * GUI
 *
 * @param env
 * @param err
 * @param arg
 * @return int
 */
int display_response(const struct dc_posix_env *env, struct dc_error *err, void *arg);
/**
 * @brief Close connection
 * 
 * @param env 
 * @param err 
 * @param arg 
 * @return int 
 */
int close_(const struct dc_posix_env *env, struct dc_error *err, void *arg);
int quit_(const struct dc_posix_env *env, struct dc_error *err, void *arg);


/**
 * @brief FSM application states
 *
 */
enum application_states
{
    SETUP_WINDOW = DC_FSM_USER_START,    // 2
    SETUP,
    AWAIT_INPUT,
    GET_ALL,
    BY_KEY,
    AWAIT_RESPONSE,
    PARSE_RESPONSE,
    DISPLAY_RESPONSE,
    CLOSE, // 10
    QUIT
};

/**
 * @brief Atomic signal exit flag
 *
 */
static volatile sig_atomic_t exit_flag;

int                          main(void)
{
    dc_error_reporter               reporter;
    dc_posix_tracer                 tracer;
    struct dc_error                 err;
    struct dc_posix_env             env;
    int                             ret_val;

    struct dc_fsm_info             *fsm_info;
    static struct dc_fsm_transition transitions[] = {{DC_FSM_INIT, SETUP_WINDOW, setup_window},
                                                     {SETUP_WINDOW, AWAIT_INPUT, await_input},
                                                     {AWAIT_INPUT, GET_ALL, get_all},
                                                     {AWAIT_INPUT, BY_KEY, by_key},
                                                     {GET_ALL, AWAIT_RESPONSE, await_response},
                                                     {BY_KEY, AWAIT_RESPONSE, await_response},
                                                     {AWAIT_RESPONSE, PARSE_RESPONSE, parse_response},
                                                     {PARSE_RESPONSE, DISPLAY_RESPONSE, display_response},
                                                     {DISPLAY_RESPONSE, CLOSE, close_},
                                                     {CLOSE, AWAIT_INPUT, await_input},
                                                     {AWAIT_INPUT, QUIT, quit_},
                                                     };


    reporter                                      = error_reporter;
    tracer                                        = trace_reporter;
    tracer                                        = NULL;
    dc_error_init(&err, reporter);
    dc_posix_env_init(&env, tracer);
    ret_val  = EXIT_SUCCESS;

    // FSM setup
    fsm_info = dc_fsm_info_create(&env, &err, "iBeaconClient");
    // dc_fsm_info_set_will_change_state(fsm_info, will_change_state);
    // dc_fsm_info_set_did_change_state(fsm_info, did_change_state);
    // dc_fsm_info_set_bad_change_state(fsm_info, bad_change_state);

    if(dc_error_has_no_error(&err))
    {
        int            from_state;
        int            to_state;

        struct client *client = (struct client *)dc_malloc(&env, &err, sizeof(struct client));
        client->res.stat_line = (struct status_line *)dc_malloc(&env, &err, sizeof(struct status_line));
        ret_val               = dc_fsm_run(&env, &err, fsm_info, &from_state, &to_state, client, transitions);
        dc_fsm_info_destroy(&env, &fsm_info);

        free(client->response);
        free(client->res.message_body);
        free(client->res.stat_line->reason_phrase);
        free(client->res.stat_line->HTTP_VER);

        free(client);
    }

    return ret_val;
}

int setup_window(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int            next_state;

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

    WINDOW *menu_window_border = newwin(6, (xMax / 2) + 2, yMax - 9, (xMax / 4) - 1);
    wattron(menu_window_border, COLOR_PAIR(1));
    box(menu_window_border, 0, 0);

    // num rows, num columns, begin y, begin x
    WINDOW *display_window        = newwin(7, xMax / 2, yMax - 20, xMax / 4);
    client->display_window        = display_window;
    WINDOW *display_window_border = newwin(9, (xMax / 2) + 2, yMax - 21, (xMax / 4) - 1);

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

    if(dc_error_has_no_error(err))
    {
        // go to next state
        next_state = AWAIT_INPUT;
        return next_state;
    }
}

int setup(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    // set up socket
    struct client *client = (struct client *)arg;
    int            next_state;

    client->host_name = "localhost";
    dc_memset(env, &(client->hints), 0, sizeof(client->hints));
    client->hints.ai_family   = AF_INET;    // PF_INET6;
    client->hints.ai_socktype = SOCK_STREAM;
    client->hints.ai_flags    = AI_CANONNAME;
    dc_getaddrinfo(env, err, client->host_name, NULL, &(client->hints), &(client->result));

    if(dc_error_has_no_error(err))
    {
        // create socket
        client->client_socket_fd =
            dc_socket(env, err, client->result->ai_family, client->result->ai_socktype, client->result->ai_protocol);

        if(dc_error_has_no_error(err))
        {
            struct sockaddr *sockaddr;
            in_port_t        port;
            in_port_t        converted_port;
            socklen_t        sockaddr_size;

            sockaddr       = client->result->ai_addr;

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
                dc_connect(env, err, client->client_socket_fd, sockaddr, sockaddr_size);
                // go to next state
                next_state = AWAIT_INPUT;
                return next_state;
            }
        }
    }
}

int await_input(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int            next_state;
    char          *choices[3] = {"GET_ALL", "GET_BY_KEY", "QUIT"};
    int            choice;

    int            display_window_ymax, display_window_xmax;
    getmaxyx(client->display_window, display_window_ymax, display_window_xmax);

    while(1)
    {
        wclear(client->menu_window);
        for(size_t i = 0; i < 3; i++)
        {
            if(i == client->highlight)
            {
                wattron(client->menu_window, A_REVERSE);
            }
            mvwprintw(client->menu_window, i, 0, choices[i]);
            wattroff(client->menu_window, A_REVERSE);
        }
        choice = wgetch(client->menu_window);
        switch(choice)
        {
            case KEY_UP:
                client->highlight--;
                if(client->highlight == -1)
                {
                    client->highlight = 0;
                }
                break;
            case KEY_DOWN:
                client->highlight++;
                if(client->highlight == 3)
                {
                    client->highlight = 2;
                }
                break;
                // enter
            case 10:
                wclear(client->display_window);

                if(choices[client->highlight] == "GET_ALL")
                {
                    next_state = GET_ALL;
                }
                if(choices[client->highlight] == "GET_BY_KEY")
                {
                    next_state = BY_KEY;
                }
                if(choices[client->highlight] == "QUIT")
                {
                    next_state = QUIT;
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

int get_all(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int            next_state;
    char           data[1024];

    setup(env, err, client);
    sprintf(data, "GET /ibeacons/data?all HTTP/1.0\r\n\r\n");
    dc_write(env, err, client->client_socket_fd, data, dc_strlen(env, data));
    next_state = AWAIT_RESPONSE;
    return next_state;
}

int by_key(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int            next_state;
    char           input[1024];
    char           data[1024];

    setup(env, err, client);

    mvwprintw(client->menu_window, 2, 0, "ENTER KEY: ");
    curs_set(1);
    wrefresh(client->menu_window);
    echo();

    wgetstr(client->menu_window, input);
    curs_set(0);
    noecho();

    sprintf(data, "GET /ibeacons/data?%s HTTP/1.0\r\n\r\n", input);
    dc_write(env, err, client->client_socket_fd, data, dc_strlen(env, data));
    next_state = AWAIT_RESPONSE;
    return next_state;
}

int await_response(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int            next_state;
    char          *buffer;

    buffer           = (char *)dc_calloc(env, err, MAX_SIZE, sizeof(char));
    client->response = (char *)dc_calloc(env, err, MAX_SIZE, sizeof(char));
    wclear(client->display_window);
    mvwprintw(client->display_window, 1, 1, "Waiting for data...");
    receive_data(env, err, client->client_socket_fd, buffer, MAX_SIZE, client);
    memmove(client->response, buffer, dc_strlen(env, buffer));
    free(buffer);
    wrefresh(client->display_window);
    next_state = PARSE_RESPONSE;
    return next_state;
}

int parse_response(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int            next_state;
    process_response(client->response, &client->res);

    next_state = DISPLAY_RESPONSE;
    return next_state;
}

int display_response(const struct dc_posix_env *env, struct dc_error *err, void *arg)
{
    struct client *client = (struct client *)arg;
    int            next_state;

    wclear(client->display_window);
    mvwprintw(client->display_window, 1, 1, "%s", client->res.message_body);
    wrefresh(client->display_window);
    next_state = CLOSE;
    return next_state;
}

int close_(const struct dc_posix_env *env, struct dc_error *err, void *arg) {
    struct client *client = (struct client *)arg;
    int            next_state;

    dc_close(env, err, client->client_socket_fd);
    next_state = AWAIT_INPUT;
    return next_state;
}

int quit_(const struct dc_posix_env *env, struct dc_error *err, void *arg) {
    struct client *client = (struct client *)arg;
    int            next_state;

    next_state = DC_FSM_EXIT;
    return next_state;
}

int receive_data(const struct dc_posix_env *env, struct dc_error *err, int fd, char *dest, size_t bufSize, void *arg)
{
    struct client *client = (struct client *)arg;
    ssize_t        count;
    ssize_t        totalWritten          = 0;
    const char    *endOfHeadersDelimiter = "\r\n\r\n";
    char          *endOfHeaders;
    ssize_t        spaceInDest;
    int            totalLength       = MAX_REQUEST_SIZE;

    bool           foundEndOfHeaders = false;

    while(totalWritten < totalLength && ((count = dc_read(env, err, fd, dest + totalWritten, bufSize)) != 0))
    {
        // check space remaining. if going over, abort.
        spaceInDest = MAX_REQUEST_SIZE - 1 - totalWritten;
        if(count > spaceInDest)
        {
            return EXIT_FAILURE;
        }

        totalWritten += count;

        if(!foundEndOfHeaders)
        {
            endOfHeaders = strstr(dest, endOfHeadersDelimiter);
            if(endOfHeaders)
            {
                foundEndOfHeaders = true;
                int headerLength  = endOfHeaders - dest + strlen(endOfHeadersDelimiter);
                process_content_length(dest, &client->res);
                totalLength = headerLength + client->res.content_length;
            }
        }
    }
    return EXIT_SUCCESS;
}

int quit(const struct dc_posix_env *env, struct dc_error *err, void *arg) {

}

static void quit_handler(int sig_num)
{
    exit_flag = 1;
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
