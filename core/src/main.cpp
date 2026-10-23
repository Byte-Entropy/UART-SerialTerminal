/**
 * @file main.cpp
 * @brief Application entry point for SOAR Serial Terminal.
 */

#include "serial_terminal.h"
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

void enable_ansi_colors() {
#ifdef _WIN32
    HANDLE output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output_handle == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD console_mode = 0;
    if (!GetConsoleMode(output_handle, &console_mode)) {
        return;
    }

    SetConsoleMode(output_handle, console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

void print_logo() {
std::cout << "                   @@@@@@@@                     \033[1;31m                \033[0m      \n";
std::cout << "         @@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[1;31m@@@@@@@@@@@@@@@@\033[0m@@@@@@@@@@@@@@  \n";
std::cout << "      @@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[1;31m@@@@@@@@@@@@@@@@\033[0m@@@@@@@@@@@@@@  \n";
std::cout << "     @@@@@@       @@@@@@@@@                     \033[1;31m                \033[0m                \n";
std::cout << "    @@@@@                                       \033[1;31m                \033[0m                \n";
std::cout << "    @@@@                                        \033[1;31m                \033[0m                \n";
std::cout << "    @@@@                            @@@@@       \033[1;31m     @@@@@      \033[0m                \n";
std::cout << "    @@@@@                        @@@@@@@@@@@    \033[1;31m  @@@@@@@@@@@   \033[0m    @@@@@@@@@@  \n";
std::cout << "     @@@@@                      @@@@@@@@@@@@@@  \033[1;31m @@@@@@@@@@@@@@ \033[0m  @@@@@@@@@@@@@ \n";
std::cout << "      @@@@@@@@@@@@@@@@@@       @@@@       @@@@  \033[1;31m@@@@       @@@@ \033[0m @@@@@      @@@@\n";
std::cout << "        @@@@@@@@@@@@@@@@@@     @@@@       @@@@  \033[1;31m@@@@        @@@@\033[0m @@@@        @@@\n";
std::cout << "           @@@@@@@@@@@@@@@@@   @@@@       @@@@  \033[1;31m@@@@ @@@@@  @@@@\033[0m @@@@      @@@@@\n";
std::cout << "                        @@@@@  @@@@       @@@@  \033[1;31m@@@@@@@@@@@@@@@@\033[0m @@@@@@@@@@@@@@@\n";
std::cout << "                         @@@@@ @@@@       @@@@  \033[1;31m@@@@@@@@@@@@@@@@\033[0m @@@@@@@@@@@@   \n";
std::cout << "                         @@@@@ @@@@       @@@@  \033[1;31m@@@@ @@@@@  @@@@\033[0m @@@@@@@@@@@@@@@\n";
std::cout << "                         @@@@@ @@@@       @@@@  \033[1;31m@@@@   @    @@@@\033[0m @@@@    @@@@@@@\n";
std::cout << "                        @@@@@@ @@@@       @@@@  \033[1;31m@@@@        @@@@\033[0m @@@@        @@@\n";
std::cout << "                      @@@@@@@  @@@@@      @@@@  \033[1;31m@@@@        @@@ \033[0m @@@@        @@@\n";
std::cout << "   @@@@@@@@@@@@@@@@@@@@@@@@     @@@@@@@@@@@@@   \033[1;31m @@@        @@@ \033[0m  @@@        @@@\n";
std::cout << "@@@@@@@@@@@@@@@@@@@@@@@@@         @@@@@@@@@                        @@     \033[0m   @  \n";
std::cout << "\033[0m\n";
std::cout << "\033[0m\n";
std::cout << "\033[0m\n";
}


int main(int argc, char* argv[]) {
    enable_ansi_colors();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug" || arg == "-d") {
            DEBUG_MODE = true;
        }
    }

    print_logo();


    Config cfg = load_config();

    

    std::string port_name = selectPort();
    if (port_name.empty()) return 1;

    if (port_name == "MOCK_BOARD") {
        cfg.default_port = port_name;
        save_config(cfg);
        run_mock_terminal_session();
        return 0;
    }

    int baudrate = selectBaudRate();
    if (baudrate <= 0) return 1;

    cfg.default_port = port_name;
    cfg.default_baud = baudrate;
    save_config(cfg);

    serial::Serial my_serial;
    my_serial.setPort(port_name);
    my_serial.setBaudrate(baudrate);
    serial::Timeout timeout = serial::Timeout::simpleTimeout(1000);
    my_serial.setTimeout(timeout);

    try {
        my_serial.open();
    } catch (const std::exception& e) {
        std::cerr << "Failed to open " << port_name << ": " << e.what() << "\n";
        return 1;
    }

    run_terminal_session(my_serial, port_name);

    if (my_serial.isOpen()) {
        my_serial.close();
    }

    std::cout << "Terminal session ended. Goodbye!\n";
    return 0;
}