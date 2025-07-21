#include "HomeVPNCore.h"
#include <ncurses.h>
#include <memory>
#include <signal.h>
#include <thread>
#include <atomic>

class HomeVPN_TUI {
private:
    std::unique_ptr<HomeVPNCore> core_;
    WINDOW *main_win_, *log_win_;
    int selected_item_ = 0;
    std::atomic<bool> running_{true};
    std::atomic<bool> minimized_{false};
    std::atomic<bool> status_changed_{false};
    std::atomic<bool> new_log_{false};

public:
    HomeVPN_TUI() {
        core_ = std::make_unique<HomeVPNCore>();
        core_->loadConfig();
        
        // Set up callbacks
        core_->setStatusCallback([this](const HomeVPNCore::Status& status) {
            status_changed_.store(true);
        });
        
        core_->setLogCallback([this](const std::string& message) {
            new_log_.store(true);
        });
        
        initCurses();
        core_->updateStatus();
        core_->startStatusMonitor();
    }
    
    ~HomeVPN_TUI() {
        core_->stopStatusMonitor();
        if (!minimized_.load()) {
            endwin();
        }
    }
    
    void run() {
        while (running_.load()) {
            if (!minimized_.load()) {
                drawInterface();
                handleInput();
            } else {
                // When minimized, just wait
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    
    void restore() {
        if (minimized_.load()) {
            minimized_.store(false);
            initCurses();
        }
    }

private:
    void initCurses() {
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);
        timeout(100); // Non-blocking getch with 100ms timeout
        
        // Initialize colors
        if (has_colors()) {
            start_color();
            init_pair(1, COLOR_GREEN, COLOR_BLACK);   // Connected
            init_pair(2, COLOR_RED, COLOR_BLACK);     // Disconnected
            init_pair(3, COLOR_YELLOW, COLOR_BLACK);   // Warnings
            init_pair(4, COLOR_CYAN, COLOR_BLACK);     // Info
        }
        clear();
        refresh();
        int height, width;
        getmaxyx(stdscr, height, width);
        int log_height = height / 2;
        main_win_ = newwin(height - log_height - 2, width, 0, 0);
        log_win_ = newwin(log_height, width, height - log_height - 1, 0);
        box(main_win_, 0, 0);
        box(log_win_, 0, 0);
    }

    void drawInterface() {
        werase(main_win_);
        werase(log_win_);
        int height, width;
        getmaxyx(stdscr, height, width);
        box(main_win_, 0, 0);
        box(log_win_, 0, 0);

        // Status
        const auto& status = core_->getStatus();
        int y = 1;
        wattron(main_win_, A_BOLD);
        mvwprintw(main_win_, y++, 2, "HomeVPN TUI");
        wattroff(main_win_, A_BOLD);
        y++;
        // VPN status
        wattron(main_win_, COLOR_PAIR(status.vpn_connected ? 1 : 2));
        mvwprintw(main_win_, y, 2, "[1] VPN: %s", status.vpn_connected ? "Connected" : "Disconnected");
        wattroff(main_win_, COLOR_PAIR(status.vpn_connected ? 1 : 2));
        if (selected_item_ == 0) mvwprintw(main_win_, y, width - 10, "<--");
        y++;
        // Mount status
        wattron(main_win_, COLOR_PAIR(status.share_mounted ? 1 : 2));
        mvwprintw(main_win_, y, 2, "[2] Share: %s", status.share_mounted ? "Mounted" : "Unmounted");
        wattroff(main_win_, COLOR_PAIR(status.share_mounted ? 1 : 2));
        if (selected_item_ == 1) mvwprintw(main_win_, y, width - 10, "<--");
        y++;
        // IP
        wattron(main_win_, COLOR_PAIR(4));
        mvwprintw(main_win_, y++, 2, "IP: %s", status.current_ip.c_str());
        wattroff(main_win_, COLOR_PAIR(4));
        // Error
        if (!status.last_error.empty()) {
            wattron(main_win_, COLOR_PAIR(3));
            mvwprintw(main_win_, y++, 2, "Error: %s", status.last_error.c_str());
            wattroff(main_win_, COLOR_PAIR(3));
        }
        y++;
        // Help
        mvwprintw(main_win_, y++, 2, "[Up/Down] Select  [Enter/Space] Toggle  [q] Quit  [m] Minimize");
        wrefresh(main_win_);

        // Logs
        const auto& logs = core_->getLogs();
        int log_lines = getmaxy(log_win_) - 2;
        int start = logs.size() > log_lines ? logs.size() - log_lines : 0;
        for (int i = 0; i < log_lines && (i + start) < (int)logs.size(); ++i) {
            mvwprintw(log_win_, i + 1, 2, "%s", logs[i + start].c_str());
        }
        mvwprintw(log_win_, 0, 2, "Log");
        wrefresh(log_win_);
    }

    void handleInput() {
        int ch = getch();
        switch (ch) {
            case KEY_UP:
                selected_item_ = (selected_item_ + 1) % 2;
                break;
            case KEY_DOWN:
                selected_item_ = (selected_item_ + 1) % 2;
                break;
            case '\n':
            case ' ':
                if (selected_item_ == 0) {
                    // Toggle VPN
                    if (core_->getStatus().vpn_connected)
                        core_->disconnectVPN();
                    else
                        core_->connectVPN();
                } else if (selected_item_ == 1) {
                    // Toggle mount
                    if (!core_->getStatus().vpn_connected) {
                        core_->addLog("Cannot mount/unmount: VPN not connected");
                    } else if (core_->getStatus().share_mounted) {
                        core_->unmountShare();
                    } else {
                        core_->mountShare();
                    }
                }
                break;
            case 'q':
            case 'Q':
                running_.store(false);
                break;
            case 'm':
            case 'M':
                minimized_.store(true);
                endwin();
                break;
            default:
                break;
        }
    }
};

// Main function
int main() {
    signal(SIGTSTP, SIG_IGN); // Ignore Ctrl+Z
    HomeVPN_TUI tui;
    tui.run();
    return 0;
}
