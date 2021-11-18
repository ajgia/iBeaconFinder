#include <dc_posix/dc_netdb.h>
#include <dc_posix/dc_posix_env.h>
#include <dc_posix/dc_unistd.h>
#include <dc_posix/dc_signal.h>
#include <dc_posix/dc_string.h>
#include <dc_posix/sys/dc_socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>

static void error_reporter(const struct dc_error *err);
static void trace_reporter(const struct dc_posix_env *env, const char *file_name,
                           const char *function_name, size_t line_number);
static void quit_handler(int sig_num);

void build_request(char *option);

static volatile sig_atomic_t exit_flag;

int main(void)
{
    // init_curses();
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
    hints.ai_family = PF_INET; // PF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    dc_getaddrinfo(&env, &err, host_name, NULL, &hints, &result);

    if (dc_error_has_no_error(&err))
    {
        int socket_fd;

        socket_fd = dc_socket(&env, &err, result->ai_family, result->ai_socktype, result->ai_protocol);

        if (dc_error_has_no_error(&err))
        {
            struct sockaddr *sockaddr;
            in_port_t port;
            in_port_t converted_port;
            socklen_t sockaddr_size;

            sockaddr = result->ai_addr;
            port = 7123;
            converted_port = htons(port);

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
                    DC_ERROR_RAISE_USER(&err, "sockaddr->sa_family is invalid", -1);
                    sockaddr_size = 0;
                }
            }

            if (dc_error_has_no_error(&err))
            {
                dc_connect(&env, &err, socket_fd, sockaddr, sockaddr_size);
                // init_curses();
                if (dc_error_has_no_error(&err))
                {
                    struct sigaction old_action;

                    dc_sigaction(&env, &err, SIGINT, NULL, &old_action);

                    if (old_action.sa_handler != SIG_IGN)
                    {
                        struct sigaction new_action;
                        char data[1024];

                        exit_flag = 0;
                        new_action.sa_handler = quit_handler;
                        sigemptyset(&new_action.sa_mask);
                        new_action.sa_flags = 0;
                        dc_sigaction(&env, &err, SIGINT, &new_action, NULL);

                        if (dc_error_has_no_error(&err))
                        {
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
                            wattron(menu_window, COLOR_PAIR(1));
                            box(menu_window, 0, 0);

                            // num rows, num columns, begin y, begin x
                            WINDOW *display_window = newwin(7, xMax / 2, yMax - 15, xMax / 4);
                            wattron(display_window, COLOR_PAIR(2));
                            box(display_window, 0, 0);
                            int display_window_ymax, display_window_xmax;
                            getmaxyx(display_window, display_window_ymax, display_window_xmax);

                            wattroff(menu_window, COLOR_PAIR(1));
                            wattroff(display_window, COLOR_PAIR(2));
                            refresh();
                            wrefresh(menu_window);
                            wrefresh(display_window);
                            keypad(menu_window, true);

                            char *choices[2] = {"GET_ALL", "GET_BY_KEY"};
                            int choice;
                            int highlight = 0;
                            strcpy(data, "GET / HTTP/1.0\r\n\r\n");

                            while (1)
                            {
                                for (size_t i = 0; i < 2; i++)
                                {
                                    if (i == highlight)
                                    {
                                        wattron(menu_window, A_REVERSE);
                                    }
                                    mvwprintw(menu_window, i + 1, 1, choices[i]);
                                    wattroff(menu_window, A_REVERSE);
                                }
                                choice = wgetch(menu_window);
                                switch (choice)
                                {
                                case KEY_UP:
                                    highlight--;
                                    if (highlight == -1)
                                    {
                                        highlight = 0;
                                    }
                                    break;
                                case KEY_DOWN:
                                    highlight++;
                                    if (highlight == 2)
                                    {
                                        highlight = 1;
                                    }
                                    break;
                                    // enter
                                case 10:
                                    mvwprintw(display_window, display_window_ymax / 2,
                                              (display_window_xmax / 2) - 2, "                   ");
                                    // send GET request
                                    mvwprintw(display_window, display_window_ymax / 2,
                                              (display_window_xmax / 2) - 2, "%s",
                                              choices[highlight]);

                                    wrefresh(display_window);
                                    break;
                                default:
                                    break;
                                }
                                while (dc_read(&env, &err, socket_fd, data, 1024) > 0 && dc_error_has_no_error(&err))
                                {

                                    printf("READ %s\n", data);
                                }
                            }

                            // read response from server

                            if (err.type == DC_ERROR_ERRNO && err.errno_code == EINTR)
                            {
                                dc_error_reset(&err);
                            }
                            getch();
                            endwin();
                        }
                    }
                }
            }
        }

        if (dc_error_has_no_error(&err))
        {
            dc_close(&env, &err, socket_fd);
        }
    }

    return EXIT_SUCCESS;
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
    fprintf(stderr, "Entering: %s : %s @ %zu\n", file_name, function_name, line_number);
}

void build_request(char *option)
{
    dc_write(&env, &err, socket_fd, data, strlen(data));
}