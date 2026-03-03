#pragma once
#include <string>
#include <nlohmann/json.h>
#include "httplib.h"

using json = nlohmann::json;

class WebAgent {
public:
    WebAgent(const std::string& config_path);
    void run(); // Основной цикл

private:
    std::string uid;
    std::string server_url;
    std::string session_id;
    int interval;
    std::string tasks_dir;
    std::string results_dir;

    bool register_on_server();
    void poll_tasks();
    void execute_task(const json& task);
    void send_result(const std::string& task_id, const std::string& output, const std::string& status);
};
