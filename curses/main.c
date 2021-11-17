#include <ncurses.h>
#include <string.h>

int main(int argc, char *argv[])
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

    wattroff(menu_window, COLOR_PAIR(1));
    wattroff(display_window, COLOR_PAIR(2));
    refresh();
    wrefresh(menu_window);
    wrefresh(display_window);
    keypad(menu_window, true);

    char *choices[2] = {"GET_ALL", "GET_BY_KEY"};
    int choice;
    int highlight = 0;

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
                mvwprintw(display_window, (display_window->_maxy / 2),
                          (display_window->_maxx / 2) - 2, "%s",
                          choices[highlight]);
                wrefresh(display_window);
                break;
            default:
                break;
        }
    }

    getch();
    endwin();
    return 0;
}
