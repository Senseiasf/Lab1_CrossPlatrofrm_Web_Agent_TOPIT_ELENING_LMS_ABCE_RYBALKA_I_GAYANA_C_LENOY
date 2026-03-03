#include <iostream>
#include <fstream>
#include <filesystem>
#include "agent.h"
#include "utils.h"

namespace fs = std::filesystem;

int main() {
    const std::string config_path = "config.json";

    // 1. Проверка наличия конфигурации
    if (!fs::exists(config_path)) {
        std::cerr << "Error: config.json not found!" << std::endl;
        return 1;
    }

    try {
        // 2. Автоматическое создание папок, если их нет
        // Это гарантирует, что требования к хранению задач будут выполнены
        fs::create_directories("tasks");
        fs::create_directories("results");

        utils::log("Starting Web Agent...");

        // 3. Инициализация и запуск агента
        WebAgent agent(config_path);
        agent.run(); // Внутри этого метода основной цикл

    } catch (const std::exception& e) {
        utils::log("Critical Error: " + std::string(e.what()));
        return 1;
    }

    return 0;
}
