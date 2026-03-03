// utils.hpp
#pragma once
#include <string>
#include <vector>

namespace utils {
    void log(const std::string& message);
    std::string execute_command(const std::string& cmd);
    bool save_to_file(const std::string& path, const std::string& data);
}
