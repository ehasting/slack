#include "Chat.hpp"

void Chat::draw(Session& current_session) {
    if (current_session.messages.empty()) {
        return;
    }

    int line = current_session.chat_line;
    int i = current_session.messages.size() - 1;
    int delta = current_session.delta;
    auto content = current_session.messages[i].content;
    
    if (delta > 0) {
        wborder(win, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ');
        int size = content.size();
        while (size > 0) {
            wscrl(win, 1);
            size--;
        }
        draw_borders();
    }

    for (auto it = content.cbegin(); it != content.cend(); ++it, line--) {
        mvwprintw(win, line - delta, 1, "%s", (*it).c_str());
    }

    wrefresh(win);
}

void Chat::change_session(const Session& current_session) {
    wclear(win);

    int line = current_session.chat_line;
    int i = current_session.messages.size() - 1;

    for (; i >= current_session.delta; i--) {
        auto content = current_session.messages[i].content;
        
        for (auto it = content.rbegin(); it != content.rend(); ++it, line--) {
            mvwprintw(win, line  - current_session.delta, 1, "%s", (*it).c_str());
        }
    }
    draw_borders();
}

void Chat::print_starting_message() {
    const std::string hello_mesg = "Welcome to slack++";
    const std::string start_mesg = "Please select an user to chat with using arrows (up/down) and enter.";

    wattron(win, A_BOLD);
    
    mvwprintw(win, (LINES - 6) / 2 - 1,
                   (COLS - 24 - hello_mesg.length()) / 2,
                   hello_mesg.c_str());
    
    mvwprintw(win, (LINES - 6) / 2 + 1,
                   (COLS - 24 - start_mesg.length()) / 2,
                   start_mesg.c_str());
    
    wrefresh(win);
    wattroff(win, A_BOLD);

}