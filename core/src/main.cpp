/**
 * @file main.cpp
 * @brief Application entry point for SOAR Serial Terminal.
 */

#include "serial_terminal.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug" || arg == "-d") {
            DEBUG_MODE = true;
        }
    }

    Config cfg = load_config();

    std::string port_name = selectPort();
    if (port_name.empty()) return 1;

    if (port_name == "MOCK_SOAR_BOARD") {
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