#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <serial/serial.h>

// Helper to flush remaining input stream characters (e.g. leftover \n)
void clear_input_buffer() {
    std::cin.clear();
    while (std::cin.rdbuf()->in_avail() > 0 && std::cin.get() != '\n');
}

// Background RX worker thread
void read_worker(serial::Serial* my_serial, std::atomic<bool>& running) {
    while (running) {
        if (my_serial->available()) {
            std::string rx_data = my_serial->read(my_serial->available());
            // Print incoming telemetry in green ANSI text
            std::cout << "\033[32m" << rx_data << "\033[0m" << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Prompts user and returns chosen COM port name or empty string on failure
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
            std::cerr << "Invalid port selection.\n";
            return "";
        }
    }

    return ports[choice].port;
}

// Prompts user and returns valid baud rate integer, or -1 on failure
int selectBaudRate() {
    std::cout << "\nSelect baud rate (default [5] 115200):\n";
    std::cout << "[1] 9600\n";
    std::cout << "[2] 19200\n";
    std::cout << "[3] 38400\n";
    std::cout << "[4] 57600\n";
    std::cout << "[5] 115200\n";
    std::cout << "[6] Custom\n";
    std::cout << "Enter choice: ";

    std::string input;
    std::getline(std::cin, input);

    int choice = 5; // Default to 115200
    if (!input.empty()) {
        std::stringstream ss(input);
        if (!(ss >> choice) || choice < 1 || choice > 6) {
            std::cerr << "Invalid baud choice.\n";
            return -1;
        }
    }

    switch (choice) {
        case 1: return 9600;
        case 2: return 19200;
        case 3: return 38400;
        case 4: return 57600;
        case 5: return 115200;
        case 6: {
            std::cout << "Enter custom baud rate: ";
            int custom_rate = 0;
            if (!(std::cin >> custom_rate) || custom_rate <= 0) {
                std::cerr << "Invalid custom baud rate.\n";
                return -1;
            }
            clear_input_buffer();
            return custom_rate;
        }
        default: return 115200;
    }
}

// Manages session TX loop and RX worker thread lifetime
void run_terminal_session(serial::Serial& my_serial) {
    std::cout << "\n--- Terminal Session Started (Type ':q' to exit) ---\n\n";

    std::atomic<bool> running{true};
    std::thread rx_thread(read_worker, &my_serial, std::ref(running));

    std::string input;
    while (true) {
        std::getline(std::cin, input);
        if (input == ":q") break;

        // Append CR LF for terminal standard
        input += "\r\n";
        my_serial.write(input);
    }

    // Clean thread shutdown
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

    serial::Timeout timeout = serial::Timeout::simpleTimeout(1000);


    // Configure and open serial session
    serial::Serial my_serial;
    my_serial.setPort(port_name);
    my_serial.setBaudrate(baudrate);
    my_serial.setTimeout(timeout);

    try {
        my_serial.open();
    } catch (const std::exception& e) {
        std::cerr << "Failed to open port " << port_name << ": " << e.what() << "\n";
        return 1;
    }

    std::cout << "Connected to " << port_name << " at " << baudrate << " baud.\n";

    run_terminal_session(my_serial);

    my_serial.close();
    std::cout << "Disconnected from " << port_name << ".\n";

    return 0;
}