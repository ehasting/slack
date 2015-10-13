//
//  SlackUI.cpp
//  slack++
//
//  Created by Gabriele Carrettoni on 04/10/15.
//
//

#include "SlackUI.hpp"

void SlackUI::set_client(SlackClient* client) {
    this->client = client;
}

void SlackUI::show() {
    using S = std::chrono::seconds;
    
    const std::string start_mesg = "The client is connecting...";
    const std::string choose_mesg = "Please select an user to chat with using arrow keys (up/down) and enter.";
    const std::string connection_issue = "Could not connect. Check your connection and retry.";

    setup_ncurses();

    roster = std::make_unique<Roster>(LINES, 22, 0, 0, "Roster");
    chat   = std::make_unique<Chat>(LINES-4, COLS-22, 0, 22, "Chat");
    input  = std::make_unique<Input>(4, COLS-22, LINES-4, 22, "InputBox", client);

    chat->print_starting_message(start_mesg);
    if (ui_lock.try_lock_for(S(30))) {
        chat->print_starting_message(choose_mesg);
        ui_lock.unlock();
        return main_ui_cycle();
    }
    chat->print_starting_message(connection_issue);
    sleep(2);
}

void SlackUI::main_ui_cycle() {
    int c;
    
    c = roster->wait();
    if (c != 27) {
        chat->chat_context_switch(get_session());
        ready = true;
    }
    while (c != 27) {
        c = input->wait(get_session().input_str, get_session().line, get_session().col);
        if (c == 9) {
            c = roster->wait();
            if ((c != 27) && (c != 0)) {
                chat->chat_context_switch(get_session());
                input->input_context_switch(get_session());
            }
        }
    }
}

Session& SlackUI::get_session() {
    return sessions[roster->get_active_channel()];
}

SlackUI::~SlackUI() {
    endwin();
    delwin(stdscr);
}

void SlackUI::setup_ncurses() {
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    ESCDELAY = 25;
    curs_set(0);
    start_color();
    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_CYAN, COLOR_BLACK);
}

void SlackUI::add_message(const RosterItem& item, const std::string& content) {
    int j = 0;
    std::vector <std::string> substr;

    do {
        substr.push_back(content.substr(j, COLS - 24).c_str());
        j += COLS - 24;
        sessions[item.channel].chat_line++;
        if (sessions[item.channel].chat_line > LINES - 6) {
            sessions[item.channel].delta++;
            sessions[item.channel].scroll_lines++;
        }
    } while (j < content.length());
    sessions[item.channel].messages.emplace_back(item, substr);
}

const std::string SlackUI::get_last_message_sender(const std::string& channel) {
    int size = sessions[channel].messages.size();
    try {
        return sessions[channel].messages.at(size - 1).item.id;
    } catch (std::out_of_range&) {
        return "";
    }
}