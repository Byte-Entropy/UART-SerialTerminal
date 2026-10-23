/**
 * @file serial_terminal.cpp
 * @brief Implementation of SOAR CLI Serial Terminal functions.
 */

#include "serial_terminal.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <thread>
#include <regex>
#include <cctype>
#include <conio.h>

bool DEBUG_MODE = false;  // Set to true to enable MOCK_BOARD for testing without hardware.

Config load_config() {
    Config cfg;
    std::ifstream file("config.txt");
    if (file.is_open()) {
        file >> cfg.default_port >> cfg.default_baud;
        file.close();
    }
    return cfg;
}

void save_config(const Config& config) {
    std::ofstream file("config.txt");
    if (file.is_open()) {
        file << config.default_port << "\n" << config.default_baud << "\n";
        file.close();
    }
}

void clear_input_buffer() {
    std::cin.clear();
    while (std::cin.rdbuf()->in_avail() > 0 && std::cin.get() != '\n');
}

std::string colorize_telemetry(const std::string& line) {
    std::string formatted = line;

    // Red: SOAR ERR_* enums, HAL_ERROR, HAL_TIMEOUT, FAIL*, CRITICAL
    formatted = std::regex_replace(formatted, 
        std::regex("\\b(ERR_[A-Z0-9_]+|HAL_ERROR|HAL_TIMEOUT|FAIL|FAILED|CRITICAL)\\b"), 
        "\033[1;31m$1\033[0m");

    // Yellow: Warnings and HAL_BUSY
    formatted = std::regex_replace(formatted, 
        std::regex("\\b(WARN|WARNING|HAL_BUSY)\\b"), 
        "\033[1;33m$1\033[0m");

    // Green: Standard OK, HAL_OK, SUCCESS
    formatted = std::regex_replace(formatted, 
        std::regex("\\b(OK|HAL_OK|SUCCESS)\\b"), 
        "\033[1;32m$1\033[0m");

    // Cyan: Hex addresses and values
    formatted = std::regex_replace(formatted, 
        std::regex("\\b(0x[0-9a-fA-F]+)\\b"), 
        "\033[36m$1\033[0m");

    return formatted;
}

void read_worker(serial::Serial* my_serial,
                 std::atomic<bool>& running,
                 std::atomic<bool>& is_paused,
                 std::atomic<bool>& silent_discovery) {
    while (running) {
        if (!is_paused && !silent_discovery && my_serial->isOpen()) {
            try {
                if (my_serial->available()) {
                    std::string rx_data = my_serial->read(my_serial->available());
                    std::cout << colorize_telemetry(rx_data) << std::flush;
                }
            } catch (...) {
                // Handle disconnects or unplug gracefully
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void scrape_soar_commands(const std::string& rx_buffer, std::set<std::string>& command_list) {
    std::regex print_regex(R"regex(SOAR_PRINT\(\s*"([^"]*)"\s*\))regex");
    auto print_begin = std::sregex_iterator(rx_buffer.begin(), rx_buffer.end(), print_regex);
    auto print_end = std::sregex_iterator();

    auto learn_commands = [&command_list](const std::string& printed_text) {
        std::istringstream lines(printed_text);
        std::string line;

        while (std::getline(lines, line)) {
            const size_t separator = line.find('-');
            if (separator != std::string::npos) {
                line.erase(separator);
            }

            const size_t first = line.find_first_not_of(" \t\r\n");
            const size_t last = line.find_last_not_of(" \t\r\n");
            if (first == std::string::npos || last < first) {
                continue;
            }

            std::string command = line.substr(first, last - first + 1);
            if (command.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                std::string::npos) {
                continue;
            }

            if (command_list.insert(command).second) {
#if 1
                std::cout << "\033[36m[SYSTEM] Learned command: " << command << "\033[0m\n";
#endif
            }
        }
    };

    bool found_print_literal = false;
    for (std::sregex_iterator i = print_begin; i != print_end; ++i) {
        found_print_literal = true;
        std::string printed_text = (*i)[1].str();
        printed_text = std::regex_replace(printed_text, std::regex(R"(\\n)"), "\n");
        printed_text = std::regex_replace(printed_text, std::regex(R"(\\r)"), "\r");
        learn_commands(printed_text);
    }

    // Board responses contain the rendered text rather than the SOAR_PRINT
    // source, so also accept the same command lines directly.
    if (!found_print_literal) {
        learn_commands(rx_buffer);
    }
}

std::string readline_with_completion(const std::set<std::string>& command_list,
                                     std::vector<std::string>& history,
                                     bool local_echo) {
    std::string buffer;
    std::string draft;
    std::string suggestion_prefix;
    size_t history_index = history.size();
    size_t suggestion_index = 0;
    size_t cursor = 0;
    int ch;

    auto redraw = [&]() {
        if (!local_echo) return;
        std::cout << "\r\033[K" << buffer
                  << "\r\033[" << cursor << "C" << std::flush;
    };

    auto handle_navigation = [&](int key) {
        if (key == 72 || key == 'A' || key == 'H') { // Up.
            if (history.empty()) return;
            if (history_index == history.size()) {
                draft = buffer;
            }
            if (history_index > 0) {
                --history_index;
                suggestion_prefix.clear();
                buffer = history[history_index];
                cursor = buffer.length();
                redraw();
            }
        } else if (key == 80 || key == 'B') { // Down: suggestions.
            if (suggestion_prefix.empty()) {
                suggestion_prefix = buffer;
                suggestion_index = 0;
            }
            std::vector<std::string> matches;
            for (const auto& command : command_list) {
                if (command.rfind(suggestion_prefix, 0) == 0) {
                    matches.push_back(command);
                }
            }
            if (!matches.empty()) {
                buffer = matches[suggestion_index++ % matches.size()];
                cursor = buffer.length();
                redraw();
            }
        } else if (key == 75 || key == 'D') { // Left.
            if (cursor > 0) {
                --cursor;
                redraw(); 
            }
        } else if (key == 77 || key == 'C') { // Right.
            if (cursor < buffer.length()) {
                ++cursor;
                redraw();
            }
        } else if (key == 83) { // Delete (Next character)
            if (cursor < buffer.length()) {
                buffer.erase(cursor, 1);
                suggestion_prefix.clear();
                redraw();
            }
        } else if (key == 115) { // Ctrl + Left (Previous word)
            if (cursor > 0) {
                // Skip preceding spaces, then word characters
                while (cursor > 0 && buffer[cursor - 1] == ' ') --cursor;
                while (cursor > 0 && buffer[cursor - 1] != ' ') --cursor;
                redraw();
            }
        } else if (key == 116) { // Ctrl + Right (Next word)
            if (cursor < buffer.length()) {
                // Skip current word characters, then trailing spaces
                while (cursor < buffer.length() && buffer[cursor] != ' ') ++cursor;
                while (cursor < buffer.length() && buffer[cursor] == ' ') ++cursor;
                redraw();
            }
        } else if (key == 147) { // Ctrl + Delete (Next word)
            if (cursor < buffer.length()) {
                size_t end = cursor;
                // Skip current word characters, then trailing spaces
                while (end < buffer.length() && buffer[end] != ' ') ++end;
                while (end < buffer.length() && buffer[end] == ' ') ++end;
                buffer.erase(cursor, end - cursor);
                suggestion_prefix.clear();
                redraw();
            }
        }
    };

    while (true) {
        ch = _getch();

        // Enter key
        if (ch == '\r' || ch == '\n') {
            if (local_echo) std::cout << "\n";
            if (!buffer.empty()) {
                history.push_back(buffer);
            }
            break;
        }
        // Backspace
        else if (ch == '\b') {
            if (cursor > 0) {
                buffer.erase(cursor - 1, 1);
                --cursor;
                suggestion_prefix.clear();
                redraw();
            }
        }
        // Extended keys (arrow keys on Windows [ugh, windows]).
        else if (ch == 0 || ch == 224) {
            handle_navigation(_getch());
        }
        // ANSI escape sequences
        else if (ch == 27) {
            const int sequence_start = _getch();
            if (sequence_start == '[' || sequence_start == 'O') {
                int key = _getch();
                while ((key >= '0' && key <= '9') || key == ';') {
                    key = _getch();
                }
                handle_navigation(key);
            }
        }
        // Tab key (Auto-complete)
        else if (ch == '\t') {
            if (buffer.empty()) continue;

            std::vector<std::string> matches;
            for (const auto& cmd : command_list) {
                if (cmd.rfind(buffer, 0) == 0) {
                    matches.push_back(cmd);
                }
            }

            if (matches.size() == 1) {
                std::string completion = matches[0].substr(buffer.length());
                if (local_echo) std::cout << completion << std::flush;
                buffer = matches[0];
                cursor = buffer.length();
            } else if (matches.size() > 1) {
                std::cout << "\n\033[36m";
                for (const auto& m : matches) std::cout << m << "  ";
                std::cout << "\033[0m\n" << (local_echo ? buffer : "") << std::flush;
            }
        }
        // Normal Printable Character
        else if (isprint(static_cast<unsigned char>(ch))) {
            buffer.insert(cursor, 1, static_cast<char>(ch));
            ++cursor;
            history_index = history.size();
            suggestion_prefix.clear();
            redraw();
        }
        // Backspace
        else if (ch == '\b') {
            if (cursor > 0) {
                buffer.erase(cursor - 1, 1);
                --cursor;
                suggestion_prefix.clear();
                redraw();
            }
        }
        // Ctrl + Backspace (127) for deleting a word backward
        else if (ch == 127) {
            if (cursor > 0) {
                size_t start = cursor;
                // Skip over any immediate preceding spaces
                while (start > 0 && buffer[start - 1] == ' ') {
                    --start;
                }
                // Skip over the word characters until the next space
                while (start > 0 && buffer[start - 1] != ' ') {
                    --start;
                }
                buffer.erase(start, cursor - start);
                cursor = start;
                suggestion_prefix.clear();
                redraw();
            }
        }
    }
    return buffer;
}

std::string selectPort() {
    Config cfg = load_config();
    std::vector<serial::PortInfo> ports = serial::list_ports();
    
    std::cout << "Available COM Ports:\n";
    if (DEBUG_MODE) {
        std::cout << "[0] MOCK_BOARD (Virtual Simulator)\n";
    }

    int default_idx = -1;
    for (size_t i = 0; i < ports.size(); ++i) {
        std::cout << "[" << i + 1 << "] " << ports[i].port 
                  << " - " << ports[i].description;
        if (ports[i].port == cfg.default_port) {
            std::cout << " (default)";
            default_idx = static_cast<int>(i) + 1;
        }
        std::cout << "\n";
    }

    if (cfg.default_port == "MOCK_BOARD" && DEBUG_MODE) {
        default_idx = 0;
    }

    if (default_idx != -1) {
        std::cout << "\nSelect port index [" << default_idx << "]: ";
    } else {
        std::cout << "\nSelect port index: ";
    }

    std::string input;
    std::getline(std::cin, input);

    if (input.empty() && default_idx != -1) {
        if (default_idx == 0) return "MOCK_BOARD";
        return ports[default_idx - 1].port;
    }

    size_t choice = 0;

    while (true) {
        std::stringstream ss(input);
        if ((ss >> choice) && ((choice == 0 && DEBUG_MODE) || (choice > 0 && choice <= ports.size()))) {
            break;
        }
        std::cerr << "Invalid selection. Please try again: ";
        std::getline(std::cin, input);
        if (input.empty() && default_idx != -1) {
            if (default_idx == 0) return "MOCK_BOARD";
            return ports[default_idx - 1].port;
        }
    }

    if (choice == 0 && DEBUG_MODE) return "MOCK_BOARD";
    return ports[choice - 1].port;
}

int selectBaudRate() {
    Config cfg = load_config();
    int default_baud = cfg.default_baud > 0 ? cfg.default_baud : 115200;

    std::cout << "\nSelect baud rate (default [" << default_baud << "]):\n";
    std::cout << "[1] 9600   [2] 19200   [3] 38400\n";
    std::cout << "[4] 57600  [5] 115200  [6] Custom\n";
    std::cout << "Enter choice: ";

    std::string input;
    std::getline(std::cin, input);

    if (input.empty()) {
        return default_baud;
    }

    int choice = 5;

    while (true) {
        std::stringstream ss(input);
        if ((ss >> choice) && choice >= 1 && choice <= 6) {
            break;
        }
        std::cout << "Invalid choice. Enter a number between 1 and 6: ";
        std::getline(std::cin, input);
        if (input.empty()) {
            return default_baud;
        }
    }

    switch (choice) {
        case 1: return 9600;
        case 2: return 19200;
        case 3: return 38400;
        case 4: return 57600;
        case 5: return 115200;
        case 6: {
            int rate = 0;
            std::string custom_input;
            while (true) {
                std::cout << "Custom baud rate: ";
                std::getline(std::cin, custom_input);
                std::stringstream ss_custom(custom_input);
                
                if ((ss_custom >> rate) && rate > 0) {
                    return rate;
                }
                std::cerr << "Invalid baud rate.\n";
            }
        }
        default: return default_baud;
    }
}

void print_help_menu() {
    std::cout << "\n================ Terminal Controls ================\n";
    std::cout << "  :p, :pause       - Pause port\n";
    std::cout << "  :r, :resume      - Resume port connection\n";
    std::cout << "  :e, :echo        - Toggle local echo (on/off)\n";
    std::cout << "  :c, :cls, :clear - Clear screen\n";
    std::cout << "  :h, :help        - Display this menu\n";
    std::cout << "  :q, :quit        - Exit terminal session\n";
    std::cout << "===================================================\n\n";
}

void run_mock_terminal_session() {
    std::cout << "\033[2J\033[1;1H" << std::flush;
    std::cout << "=============================================\n";
    std::cout << "SO\033[1;31mA\033[0mR Terminal Connected: MOCK_BOARD\n";
    std::cout << " Running in virtual simulation mode.\n";
    std::cout << "=============================================\n\n";

    // Standard SOAR command set (removed 'help')
    std::set<std::string> soar_commands = {"status", "sysinfo", "sysreset"};
    std::vector<std::string> history;
    bool local_echo = true;

    // Silent background auto-discovery
    std::string mock_help_output = 
        "\n-- DEBUG COMMANDS --\n"
        "sysinfo  - System information\n"
        "sysreset - System reset\n"
        "fs_test  - Run file system tests\n"
        "fs_log   - Log sample sensor data\n"
        "fs_cleanup - Run file system cleanup\n"
        "bq_connect - BQ76942: probe I2C and verify device ID\n"
        "bq_cells   - BQ76942: read cell voltages\n"
        "bq_stack   - BQ76942: read stack voltage\n"
        "bq_current - BQ76942: read pack current\n"
        "bq_alarm   - BQ76942: read alarm status\n"
        "bq_safety  - BQ76942: read safety status A/B/C\n"
        "bq_all     - BQ76942: read cells, stack and current\n"
        "test_mosfet_chg - BQ76942: test pack charge mosfet\n"
        "test_mosfet_dsg - BQ76942: test pack discharge mosfet\n"
        "read_fet_status - BQ76942: read pack mosfet status\n"
        "bq_reset   - BQ76942: reset the device\n"
        "h        - Show this help\n\n";

    scrape_soar_commands(mock_help_output, soar_commands);

    std::string input;
    while (true) {
        input = readline_with_completion(soar_commands, history, local_echo);

        if (input == ":q" || input == ":quit") break;

        if (input == ":h" || input == ":help") {
            print_help_menu();
            continue;
        }

        if (input == ":e" || input == ":echo") {
            local_echo = !local_echo;
            std::cout << "\033[33m[TERMINAL] Local echo " << (local_echo ? "ENABLED" : "DISABLED") << "\033[0m\n";
            continue;
        }

        if (input == ":c" || input == ":cls" || input == ":clear") {
            std::cout << "\033[2J\033[1;1H" << std::flush;
            continue;
        }

        if (input == ":p" || input == ":pause") {
            std::cout << "\033[1;33m[PORT PAUSED] MOCK handle released.\033[0m\n";
            continue;
        }

        if (input == ":r" || input == ":resume") {
            std::cout << "\033[1;32m[PORT RESUMED] MOCK re-opened.\033[0m\n";
            continue;
        }

        if (input == "h") {
            std::cout << colorize_telemetry(mock_help_output) << std::flush;
            continue;
        }

        if (input == "bq_all") {
            std::string mock_data = "[INFO] Cell 1: 3700 mV | Cell 2: 3705 mV | Stack: 7405 mV | Status: HAL_OK\n";
            std::cout << colorize_telemetry(mock_data) << std::flush;
        } else if (input == "fs_test") {
            std::string mock_err = "[ERROR] ERR_I2C Flash communication timeout! Status: HAL_ERROR\n";
            std::cout << colorize_telemetry(mock_err) << std::flush;
        } else {
            std::string mock_unk = "Debug, unknown command: " + input + " (type 'h' for help)\n";
            std::cout << colorize_telemetry(mock_unk) << std::flush;
        }
    }
}

void run_terminal_session(serial::Serial& my_serial, const std::string& port_name) {
    std::cout << "\033[2J\033[1;1H" << std::flush;
    std::cout << "=============================================\n";
    std::cout << " SO\033[1;31mA\033[0mR Terminal Connected: " << port_name << "\n";
    std::cout << " Type ':h' for terminal control options.\n";
    std::cout << " Press [TAB] to auto-complete target commands.\n";
    std::cout << "=============================================\n\n";

    std::atomic<bool> running{true};
    std::atomic<bool> is_paused{false};

    std::atomic<bool> silent_discovery{true};
    std::thread rx_thread(read_worker,
                          &my_serial,
                          std::ref(running),
                          std::ref(is_paused),
                          std::ref(silent_discovery));

    std::set<std::string> soar_commands = {"status", "sysinfo", "sysreset"};
    std::vector<std::string> history;
    bool local_echo = true;

    // --- Silent Auto-Discovery Setup ---
    my_serial.write("h\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    if (my_serial.available()) {
        std::string discovery_rx = my_serial.read(my_serial.available());
        scrape_soar_commands(discovery_rx, soar_commands);
    }
    silent_discovery = false;

    std::string input;
    while (true) {
        input = readline_with_completion(soar_commands, history, local_echo);

        if (input == ":q" || input == ":quit") break;

        if (input == ":h" || input == ":help") {
            print_help_menu();
            continue;
        }

        if (input == ":e" || input == ":echo") {
            local_echo = !local_echo;
            std::cout << "\033[33m[TERMINAL] Local echo " << (local_echo ? "ENABLED" : "DISABLED") << "\033[0m\n";
            continue;
        }

        if (input == ":c" || input == ":cls" || input == ":clear") {
            std::cout << "\033[2J\033[1;1H" << std::flush;
            continue;
        }

        if (input == ":p" || input == ":pause") {
            if (!is_paused) {
                is_paused = true;
                my_serial.close();
                std::cout << "\033[1;33m[PORT PAUSED] " << port_name << " handle released.\033[0m\n";
            }
            continue;
        }

        if (input == ":r" || input == ":resume") {
            if (is_paused) {
                try {
                    my_serial.open();
                    is_paused = false;
                    std::cout << "\033[1;32m[PORT RESUMED] " << port_name << " re-opened.\033[0m\n";
                } catch (const std::exception& e) {
                    std::cerr << "\033[1;31mFailed to re-open port: " << e.what() << "\033[0m\n";
                }
            }
            else {
                std::cout << "\033[33mPort is already active. No action taken.\033[0m\n";
            }
            continue;
        }

        if (is_paused) {
            std::cout << "\033[33mCannot send data: Port is currently paused. Type :r to resume.\033[0m\n";
            continue;
        }

        // --- Dynamic Re-Scraping on User 'h' Command ---
        bool trigger_rescrape = (input == "h");

        if (trigger_rescrape) {
            silent_discovery = true;
        }
        input += "\r\n";
        my_serial.write(input);

        if (trigger_rescrape) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (my_serial.available()) {
                std::string rx_h_response = my_serial.read(my_serial.available());
                std::cout << colorize_telemetry(rx_h_response) << std::flush;

                size_t prev_count = soar_commands.size();
                scrape_soar_commands(rx_h_response, soar_commands);
                if (soar_commands.size() > prev_count) {
                    std::cout << "\033[32m[SYSTEM] Discovered " << (soar_commands.size() - prev_count) 
                              << " new commands for Tab completion!\033[0m\n";
                }
            }
            silent_discovery = false;
        }
    }

    running = false;
    if (rx_thread.joinable()) {
        rx_thread.join();
    }
}