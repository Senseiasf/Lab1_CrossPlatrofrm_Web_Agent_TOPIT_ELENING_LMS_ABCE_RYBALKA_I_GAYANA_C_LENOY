#include "utils.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <array>
#include <memory>
#include <stdexcept>

namespace utils {

    // Функция для логирования действий в консоль и файл
    void log(const std::string& message) {
        // Получаем текущее время
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        char timestamp[20];
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));

        std::string formatted_msg = "[" + std::string(timestamp) + "] " + message;

        // Печать в консоль
        std::cout << formatted_msg << std::endl;

        // Запись в файл (дозапись в конец)
        std::ofstream log_file("agent.log", std::ios_base::app);
        if (log_file.is_open()) {
            log_file << formatted_msg << std::endl;
        }
    }

    // Запуск системной команды и получение её вывода (stdout)
    std::string execute_command(const std::string& cmd) {
        std::array<char, 128> buffer;
        std::string result;

        // Используем popen для чтения вывода консоли (кроссплатформенно для Linux/Mac)
        // Для полноценного Windows (MSVC) используется _popen
#ifdef _WIN32
        auto pipe = _popen(cmd.c_str(), "r");
#else
        auto pipe = popen(cmd.c_str(), "r");
#endif

        if (!pipe) {
            return "Error: Failed to run command";
        }

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }

#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return result;
    }

    // Сохранение данных в файл (результаты работы)
    bool save_to_file(const std::string& path, const std::string& data) {
        try {
            std::ofstream file(path);
            if (!file.is_open()) return false;
            file << data;
            return true;
        } catch (...) {
            return false;
        }
    }
}
