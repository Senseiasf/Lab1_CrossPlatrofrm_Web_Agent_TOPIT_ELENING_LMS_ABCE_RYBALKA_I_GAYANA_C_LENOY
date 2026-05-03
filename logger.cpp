/**
 * @file logger.cpp
 * @brief System logging module.
 * Provides centralized and thread-safe recording of agent events.
 */
#include "logger.h"
#include <iostream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <thread>
#include <filesystem>

namespace fs = std::filesystem;
/** @brief Global mutex for synchronizing file writes from different threads. */
std::mutex log_mtx;


/**
 * @brief Ensures the existence of the directory for storing logs.
 * @return fs::path Path to the "logs" directory relative to the executable.
 * * Logic: Checks for the existence of the "logs" folder in the current working directory.
 * If the folder is missing, creates it recursively. Why recursively - I don't know.
 * But that's exactly how it works.
 */
fs::path get_log_directory() {
    fs::path log_dir = fs::current_path() / "logs";
    if (!fs::exists(log_dir)) {
        fs::create_directories(log_dir);
    }
    return log_dir;
}

/**
 * @brief Records a standard message to the system log.
 * @param module Module name or severity level (e.g., "SYSTEM", "ERROR", "UPLOADER").
 * @param message Message text to record.
 * * Implementation details:
 * 1. Thread-Safety: Uses std::lock_guard to prevent Race Conditions
 * between worker threads when accessing the same file.
 * 2. Log format: [Date Time] [Thread ID] [Module] Message.
 * 3. Autonomy: Opens and closes the file itself in std::ios::app mode (append to end),
 * which guarantees data safety in case of an unexpected program termination.
 */
void log_message(const std::string& module, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mtx);

    fs::path log_file_path = get_log_directory() / "agent.log";
    std::ofstream log_file(log_file_path, std::ios::app);

    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        log_file << "[" << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") << "] ";
        log_file << "[Thread: " << std::this_thread::get_id() << "] ";
        log_file << "[" << module << "] " << message << std::endl;

        log_file.close();
    } else {
        std::cerr << "[CRITICAL ERROR] Failed to open log file: " << log_file_path << std::endl;
    }
}