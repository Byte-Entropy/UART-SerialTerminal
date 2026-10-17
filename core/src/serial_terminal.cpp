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
#include <conio.h>

bool DEBUG_MODE = false;

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

void read_worker(serial::Serial* my_serial, std::atomic<bool>& running, std::atomic<bool>& is_paused) {
    while (running) {
        if (!is_paused && my_serial->isOpen()) {
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
    std::regex cmd_regex(R"(^\s*([a-zA-Z0-9_-]+)\s*(?:-|:|>|\t|\s{2,}))", std::regex::multiline);
    auto words_begin = std::sregex_iterator(rx_buffer.begin(), rx_buffer.end(), cmd_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        std::string cmd = match[1].str();
        if (cmd != "h" && cmd != "help" && cmd.length() > 1) {
            command_list.insert(cmd);
        }
    }
}

std::string readline_with_completion(const std::set<std::string>& command_list, bool local_echo) {
    std::string buffer;
    char ch;

    while (true) {
        ch = _getch();

        // Enter key
        if (ch == '\r' || ch == '\n') {
            if (local_echo) std::cout << "\n";
            break;
        }
        // Backspace
        else if (ch == '\b') {
            if (!buffer.empty()) {
                buffer.pop_back();
                if (local_echo) std::cout << "\b \b" << std::flush;
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
            } else if (matches.size() > 1) {
                std::cout << "\n\033[36m";
                for (const auto& m : matches) std::cout << m << "  ";
                std::cout << "\033[0m\n> " << (local_echo ? buffer : "") << std::flush;
            }
        }
        // Normal Printable Character
        else if (isprint(static_cast<unsigned char>(ch))) {
            buffer += ch;
            if (local_echo) std::cout << ch << std::flush;
        }
    }
    return buffer;
}

std::string selectPort() {
    Config cfg = load_config();
    std::vector<serial::PortInfo> ports = serial::list_ports();
    
    std::cout << "Available COM Ports:\n";
    if (DEBUG_MODE) {
        std::cout << "[0] MOCK_SOAR_BOARD (Virtual Hardware Simulator)\n";
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

    if (cfg.default_port == "MOCK_SOAR_BOARD" && DEBUG_MODE) {
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
        if (default_idx == 0) return "MOCK_SOAR_BOARD";
        return ports[default_idx - 1].port;
    }

    size_t choice = 0;
    std::stringstream ss(input);
    if (!(ss >> choice) || (choice == 0 && !DEBUG_MODE) || choice > ports.size()) {
        std::cerr << "Invalid selection.\n";
        return "";
    }

    if (choice == 0 && DEBUG_MODE) return "MOCK_SOAR_BOARD";
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
    std::stringstream ss(input);
    if (!(ss >> choice) || choice < 1 || choice > 6) return -1;

    switch (choice) {
        case 1: return 9600;
        case 2: return 19200;
        case 3: return 38400;
        case 4: return 57600;
        case 5: return 115200;
        case 6: {
            std::cout << "Custom baud rate: ";
            int rate = 0;
            if (!(std::cin >> rate) || rate <= 0) return -1;
            clear_input_buffer();
            return rate;
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
    std::cout << "SO\033[1;31mA\033[0mR Terminal Connected: MOCK_SOAR_BOARD\n";
    std::cout << " Running in virtual simulation mode.\n";
    std::cout << "=============================================\n\n";

    // Standard SOAR command set (removed 'help')
    std::set<std::string> soar_commands = {"status", "sysinfo", "sysreset"};
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
        input = readline_with_completion(soar_commands, local_echo);

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

    std::thread rx_thread(read_worker, &my_serial, std::ref(running), std::ref(is_paused));

    std::set<std::string> soar_commands = {"status", "sysinfo", "sysreset"};
    bool local_echo = true;

    // --- Silent Auto-Discovery Setup ---
    std::atomic<bool> silent_discovery{true};
    my_serial.write("h\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    if (my_serial.available()) {
        std::string discovery_rx = my_serial.read(my_serial.available());
        scrape_soar_commands(discovery_rx, soar_commands);
    }
    silent_discovery = false;

    std::string input;
    while (true) {
        input = readline_with_completion(soar_commands, local_echo);

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
        }
    }

    running = false;
    if (rx_thread.joinable()) {
        rx_thread.join();
    }
}