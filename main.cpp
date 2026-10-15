#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <regex>
#include <serial/serial.h>

// Helper to flush input buffer
void clear_input_buffer() {
    std::cin.clear();
    while (std::cin.rdbuf()->in_avail() > 0 && std::cin.get() != '\n');
}

// Telemetry line colorizer using ANSI escape codes
std::string colorize_telemetry(const std::string& line) {
    std::string formatted = line;

    // Red for Errors / Failures
    formatted = std::regex_replace(formatted, std::regex("(ERROR|ERR|FAIL|CRITICAL)"), "\033[1;31m$1\033[0m");
    // Yellow for Warnings
    formatted = std::regex_replace(formatted, std::regex("(WARN|WARNING)"), "\033[1;33m$1\033[0m");
    // Green for OK / Success / Info
    formatted = std::regex_replace(formatted, std::regex("(OK|SUCCESS|INFO)"), "\033[1;32m$1\033[0m");
    // Cyan for Hex addresses/values
    formatted = std::regex_replace(formatted, std::regex("(0x[0-9a-fA-F]+)"), "\033[36m$1\033[0m");

    return formatted;
}

// Background RX thread
void read_worker(serial::Serial* my_serial, std::atomic<bool>& running, std::atomic<bool>& is_paused) {
    while (running) {
        if (!is_paused && my_serial->isOpen()) {
            try {
                if (my_serial->available()) {
                    std::string rx_data = my_serial->read(my_serial->available());
                    std::cout << colorize_telemetry(rx_data) << std::flush;
                }
            } catch (...) {
                // Handle sudden USB unplug gracefully
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::string selectPort() {
    std::vector<serial::PortInfo> ports = serial::list_ports();
    if (ports.empty()) {
        std::cerr << "No COM ports found.\n";
        return "";
    }

    std::cout << "Available COM Ports:\n";
    for (size_t i = 0; i < ports.size(); ++i) {
        std::cout << "[" << i << "] " << ports[i].port 
                  << " - " << ports[i].description << "\n";
    }

    std::cout << "\nSelect port index [0]: ";
    std::string input;
    std::getline(std::cin, input);

    size_t choice = 0;
    if (!input.empty()) {
        std::stringstream ss(input);
        if (!(ss >> choice) || choice >= ports.size()) {
            std::cerr << "Invalid selection.\n";
            return "";
        }
    }

    return ports[choice].port;
}

int selectBaudRate() {
    std::cout << "\nSelect baud rate (default [5] 115200):\n";
    std::cout << "[1] 9600   [2] 19200   [3] 38400\n";
    std::cout << "[4] 57600  [5] 115200  [6] Custom\n";
    std::cout << "Enter choice: ";

    std::string input;
    std::getline(std::cin, input);

    int choice = 5;
    if (!input.empty()) {
        std::stringstream ss(input);
        if (!(ss >> choice) || choice < 1 || choice > 6) return -1;
    }

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
        default: return 115200;
    }
}

void run_terminal_session(serial::Serial& my_serial, const std::string& port_name) {
    std::cout << "\n=============================================\n";
    std::cout << " SOAR Terminal Connected: " << port_name << "\n";
    std::cout << " Commands:\n";
    std::cout << "   :p  - Pause/Release port (allows flashing MCU)\n";
    std::cout << "   :r  - Resume port connection\n";
    std::cout << "   :h  - Send SOAR 'h' auto-discovery command\n";
    std::cout << "   :q  - Quit session\n";
    std::cout << "=============================================\n\n";

    std::atomic<bool> running{true};
    std::atomic<bool> is_paused{false};
    std::thread rx_thread(read_worker, &my_serial, std::ref(running), std::ref(is_paused));

    // Send initial SOAR help auto-discovery command
    my_serial.write("h\r\n");

    std::string input;
    while (true) {
        std::getline(std::cin, input);

        if (input == ":q") break;

        // Command: Pause / Release Port Handle
        if (input == ":p") {
            if (!is_paused) {
                is_paused = true;
                my_serial.close();
                std::cout << "\033[1;33m[PORT PAUSED] " << port_name << " released for MCU flashing.\033[0m\n";
            } else {
                std::cout << "Port is already paused. Type :r to resume.\n";
            }
            continue;
        }

        // Command: Resume Port Connection
        if (input == ":r") {
            if (is_paused) {
                try {
                    my_serial.open();
                    is_paused = false;
                    std::cout << "\033[1;32m[PORT RESUMED] " << port_name << " re-opened.\033[0m\n";
                } catch (const std::exception& e) {
                    std::cerr << "\033[1;31mFailed to re-open port: " << e.what() << "\033[0m\n";
                }
            } else {
                std::cout << "Port is actively running.\n";
            }
            continue;
        }

        // Command: Trigger SOAR help command explicitly
        if (input == ":h") {
            input = "h";
        }

        if (is_paused) {
            std::cout << "\033[33mCannot send data: Port is currently paused. Type :r to resume.\033[0m\n";
            continue;
        }

        // Append CRLF for standard serial transmission
        input += "\r\n";
        my_serial.write(input);
    }

    running = false;
    if (rx_thread.joinable()) {
        rx_thread.join();
    }
}

int main() {
    std::string port_name = selectPort();
    if (port_name.empty()) return 1;

    int baudrate = selectBaudRate();
    if (baudrate <= 0) return 1;

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
    std::cout << "Disconnected from " << port_name << ".\n";

    return 0;
}