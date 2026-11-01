#include "ui.h"

#include <curses.h>
#include <wchar.h>

int ui_run(App *app)
{
    wint_t input;

    (void)app;
    if (initscr() == NULL) {
        return 1;
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

    erase();
    mvaddstr(0, 0, "terminal-editor prototype — Ctrl+X exits");
    mvaddstr(2, 0, "Build skeleton ready. Editor panels are added in the next tasks.");
    refresh();

    while (get_wch(&input) != ERR) {
        if (input == 24) {
            break;
        }
    }

    endwin();
    return 0;
}
