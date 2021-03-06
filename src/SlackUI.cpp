#include "SlackUI.hpp"

static bool quit = false;

void SlackUI::set_client(SlackClient* client) {
    this->client = client;
}

void SlackUI::show() {
    using S = std::chrono::seconds;
    
    const std::string start_mesg = "The client is connecting...";
    const std::string choose_mesg = "Please select an user to chat with using arrow keys (up/down) and enter.";
    const std::string connection_issue = "Could not connect. Check your connection and retry.";

    setup_ncurses();

    chat   = std::make_unique<Chat>(LINES - 4, 3 * COLS / 4, 0, COLS / 4, "Chat");
    input  = std::make_unique<Input>(4, 3 * COLS / 4, LINES - 4, COLS / 4, "InputBox", client);

    chat->print_starting_message(start_mesg);
    if (client->start()) {
        roster->draw();
        chat->print_starting_message(choose_mesg);
        return main_ui_cycle();
    }
    chat->print_starting_message(connection_issue);
    sleep(2);
}

#ifdef __linux__
static void sig_handler(int fd) {
    quit = true;
    
    struct signalfd_siginfo fdsi;
    ssize_t s;
    
    s = read(fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
        Log::d() << "an error occurred while getting signalfd data." << std::endl;
    } else {
        Log::d() << "received " << fdsi.ssi_signo << "signal. Leaving." << std::endl;
    }
}
#endif

void SlackUI::main_ui_cycle() {
    Window *active_win;
    const int KEY_ESC = 27;
    const int KEY_TAB = 9;
    const int KEY_ENT = 10;
    
    const int NCURSES_EVT = 0;
    const int SOCKET_EVT = 1;
#ifdef __linux__
    const int SIGNAL_EVT = 2;
    const int nfds = 3;
#else
    const int nfds = 2;
#endif
    
    struct pollfd main_p[nfds];
    
    main_p[NCURSES_EVT] = (struct pollfd) {
        .fd = STDIN_FILENO,
        .events = POLLIN,
    };
    
    main_p[SOCKET_EVT] = (struct pollfd) {
        .fd = client->get_socket(),
        .events = POLLIN,
    };

#ifdef LIBNOTIFY_FOUND
    notify_init("Slack++");
#endif

#ifdef __linux__
    sigset_t mask;
    
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    main_p[SIGNAL_EVT] = (struct pollfd) {
        .fd = signalfd(-1, &mask, 0),
        .events = POLLIN,
    };
#endif
    
    active_win = roster.get();
    int c;
    while (!quit) {
        int ret = poll(main_p, nfds, -1);
        for (int i = 0; i < nfds && ret > 0; i++) {
            if (main_p[i].revents & POLLIN) {
                switch (i) {
                case NCURSES_EVT:
                    // ncurses event
                    c = active_win->wait(get_session());
                    switch (c) {
                    case KEY_TAB:
                        change_context();
                        active_win = roster.get();
                        break;
                    case KEY_UP:
                        scroll_up(1);
                        break;
                    case KEY_DOWN:
                        scroll_down(1);
                        break;
                    case KEY_PPAGE:
                        scroll_up(chat->get_real_rows());
                        break;
                    case KEY_NPAGE:
                        scroll_down(chat->get_real_rows());
                        break;
                    case KEY_ENT:    // enter to select an user to chat with
                        switch_session();
                        active_win = input.get();
                        break;
                    case KEY_ESC:
                        quit = true;
                        break;
                    default:
                        break;
                    }
                    break;
                case SOCKET_EVT:
                    // socket event
                    client->receive();
                    break;
#ifdef __linux__
                case SIGNAL_EVT:
                    // signal event on linux
                    sig_handler(main_p[SIGNAL_EVT].fd);
                    break;
#endif
                }
                ret--;
            }
        }
    }
    
    // before leaving, make sure we updated current chat's mark
    // only if a chat had been selected (ie the user did not leave slack++ immediately)
    update_mark(get_session(),
                roster->get_active_channel(),
                roster->get_active_type());

#ifdef LIBNOTIFY_FOUND
    notify_uninit();
#endif
}

void SlackUI::switch_session() {
    if (get_session().first_time) {
        std::string type = roster->get_active_type();
        client->get_history(roster->get_active_channel(), type);
        get_session().first_time = false;
        get_session().latest_updated_msg = get_session().messages.size();
    }
    chat->set_title(roster->get_active_name());
    chat->chat_context_switch(get_session());
    if (get_session().scrolled_back == 0) {
        remove_notification();
    }
    input->input_context_switch(get_session());
}

void SlackUI::change_context() {
    input->clear();
    chat->set_title("Chat");
    chat->print_starting_message("Select an user to chat with.");
    // updating old chat mark while changing session
    start_mark_thread(get_session(),
                      roster->get_active_channel(),
                      roster->get_active_type());
    roster->set_current_active();
    roster->set_active(-1);
}

void SlackUI::scroll_up(int lines) {
    if (chat->scroll_back(get_session(), lines) == -1 && get_session().has_more) {
        int old_size = get_session().messages.size();
        std::string type = roster->get_active_type();
        client->get_history(roster->get_active_channel(), type);
        if (get_session().messages.size() > old_size) {
            chat->scroll_back(get_session(), lines);
        }
    }
    input->highlight(get_session().col);
}

void SlackUI::scroll_down(int lines) {
    if (chat->scroll_forward(get_session(), lines) == 0) {
        remove_notification();
    }
    input->highlight(get_session().col);
}

void SlackUI::remove_notification() {
    roster->remove_highlight();
    quit_notification();
}

Session& SlackUI::get_session() {
    return sessions[roster->get_active_channel()];
}

SlackUI::~SlackUI() {
    delwin(stdscr);
    endwin();
}

void SlackUI::setup_ncurses() {
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    ESCDELAY = 25;
    curs_set(0);
    notimeout(stdscr, TRUE);
    start_color();
    use_default_colors();
    init_pair(1, COLOR_BLUE, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, COLOR_CYAN, -1);
}

void SlackUI::add_message(const RosterItem& item, 
                          const std::string& content, 
                          bool sender, 
                          bool me, 
                          const std::string &channel) {
    int j = 0;
    bool check = false;

    do {
        sessions[channel].add_message(item, content.substr(j, chat->get_real_cols()), sender);
        j += chat->get_real_cols();
        if (sessions[channel].messages.size() > chat->get_real_rows()) {
            sessions[channel].delta++;
        }
        // check if user is the active one and if current session is not scrolled back
        if (channel == roster->get_active_channel() && sessions[channel].scrolled_back == 0) {
            chat->draw_next(sessions[channel]);
            check = true;
        }
    } while (j < content.length());
    
    if (!check) {
        /* Whether we are printing new sender (no libnotify notification needed) */
        if (!sender) {
            roster->highlight_user(channel);
#ifdef LIBNOTIFY_FOUND
            if (!me) {
                notify_send(item, content.substr(0, 40));
            }
#endif
        }
        if (sessions[channel].scrolled_back > 0) {
            sessions[channel].scrolled_back += j / (chat->get_real_cols());
        }
    }

    input->highlight(sessions[channel].col);
}

void SlackUI::add_history(const RosterItem& item, 
                          const std::string& content, 
                          bool sender, 
                          const std::string &channel) {
    int j = content.length();
    int length = j % chat->get_real_cols();
    do {
        sessions[channel].add_history(item, content.substr(j - length, length), sender);
        if (sessions[channel].messages.size() > chat->get_real_rows()) {
            sessions[channel].delta++;
        }
        j -= length;
        length = chat->get_real_cols();
    } while (j > 0);
}

#ifdef LIBNOTIFY_FOUND
void SlackUI::notify_send(const RosterItem &item, const std::string& mesg) {
    if (!item.muted) {
        std::string noti = "New message from " + item.name + ":\n" + mesg;
        n = notify_notification_new ("Slack++", noti.c_str(), 0);
        notify_notification_set_timeout(n, 5000);
        notify_notification_show(n, 0);
    }
}
#endif

void SlackUI::quit_notification() {
#ifdef LIBNOTIFY_FOUND
    if (n) {
        notify_notification_close(n, NULL);
    }
#endif
}

const std::string SlackUI::get_last_message_sender(const std::string& channel) {
    int size = sessions[channel].messages.size();
    try {
        return sessions[channel].messages.at(size - 1).item.id;
    } catch (std::out_of_range&) {
        return "";
    }
}

void SlackUI::set_company_name(const std::string &name) {
    roster = std::make_unique<Roster>(LINES, COLS / 4, 0, 0, name);
}

void SlackUI::update_mark(Session& sess, const std::string& channel, const std::string& type) {
    if (sess.scrolled_back == 0 && sess.messages.size() != sess.latest_updated_msg) {
        client->update_mark(channel, type);
        sess.latest_updated_msg = sess.messages.size();
    }
}

void SlackUI::start_mark_thread(Session &sess, const std::string& channel, const std::string& type) {
    std::thread t(&SlackUI::update_mark, this, std::ref(sess),
                  channel, type);
    t.detach();
}
