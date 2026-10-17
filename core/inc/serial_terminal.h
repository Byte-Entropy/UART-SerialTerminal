/**
 * @file serial_terminal.h
 * @brief Header file for the SOAR CLI Serial Terminal interface.
 */

#ifndef SERIAL_TERMINAL_H
#define SERIAL_TERMINAL_H

#include <string>
#include <atomic>
#include <set>
#include <serial/serial.h>

extern bool DEBUG_MODE;


/**
 * @brief Configuration struct for storing default COM port and baud rate settings.
 */
struct Config {
    std::string default_port = "";
    int default_baud = 115200;
};


/**
 * @brief loads Config from a local file
 */
Config load_config(); 

/**
 * @brief Saves Config to a local file
 */
void save_config(const Config& config);

/**
 * @brief Flushes remaining characters from std::cin stream buffer.
 */
void clear_input_buffer();

/**
 * @brief Applies ANSI color codes to raw telemetry strings based on SOAR status codes and HAL errors.
 * 
 * @param line Raw input string received over UART.
 * @return std::string Formatted string with embedded ANSI escape color sequences.
 */
std::string colorize_telemetry(const std::string& line);

/**
 * @brief Background thread function that continuously reads incoming UART data.
 */
void read_worker(serial::Serial* my_serial, std::atomic<bool>& running, std::atomic<bool>& is_paused);

/**
 * @brief Parses raw output from SOAR 'h' command response to extract command names.
 */
void scrape_soar_commands(const std::string& rx_buffer, std::set<std::string>& command_list);

/**
 * @brief Reads a line from console input supporting Tab auto-complete and local echo.
 * 
 * @param command_list Set of valid commands for completion matching.
 * @param local_echo Flag indicating whether typed input should be echoed to console.
 * @return std::string Completed input line.
 */
std::string readline_with_completion(const std::set<std::string>& command_list, bool local_echo);

/**
 * @brief Lists available COM ports on host system (includes MOCK option).
 */
std::string selectPort();

/**
 * @brief Displays standard baud rates and prompts user for selection.
 */
int selectBaudRate();

/**
 * @brief Prints internal ':' control command help menu.
 */
void print_help_menu();

/**
 * @brief Runs mock session for virtual hardware testing without a board.
 */
void run_mock_terminal_session();

/**
 * @brief Manages interactive terminal session for physical COM ports.
 */
void run_terminal_session(serial::Serial& my_serial, const std::string& port_name);

#endif // SERIAL_TERMINAL_H