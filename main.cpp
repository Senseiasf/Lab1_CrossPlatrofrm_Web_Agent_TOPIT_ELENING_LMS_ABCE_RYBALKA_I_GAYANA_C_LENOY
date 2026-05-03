#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <chrono>
#include <atomic>
#include <sstream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <memory>
#include <stdexcept>
#include <array>
#include <map>
#include "consoletable.h"
#include "HTTP_client.h"
#include <nlohmann/json.hpp>
#include "json_functions.h"
#include "logger.h"
#ifdef _WIN32
#include <windows.h>
#endif

void pause_ui(bool pause);

void save_keychain_safe();
void update_uid_display();

using json = nlohmann::json;
namespace fs = std::filesystem;

#define WORK_WITH_SERVICE

int current_polling_interval = 20;
#define TO_WORKER_THREAD 3000

std::string UID = "";
std::string access_code = "";
std::string last_active_uid = "";

std::mutex auth_mtx; 
std::map<std::string, std::string> keychain;

fs::path DATA_DIR;
fs::path SEND_DIR;

struct Task {
    std::string session_id;
    std::string task_code;
    std::vector<std::string> file_paths;
    
    std::string task_uid; 
    std::string task_token;
};

std::mutex mtx;
std::mutex mtx1;
std::condition_variable cv_timer;
std::condition_variable cv_workers;
std::condition_variable cv_workers1;

std::queue<Task> task_queue;
std::queue<Task> task_queue1;

std::atomic<bool> stop_flag{false};
std::atomic<bool> ping_flag{false};

/**
 * @brief Executes an external program. If the string contains C++ code, it compiles it.
 */
std::string run_task_logic(const std::string& cmd) {
    // 1. Create workspace folder (if it doesn't exist)
    fs::path base_path = fs::current_path();
    fs::path workspace_dir = base_path / "data" / "workspace";
    fs::create_directories(workspace_dir);

    std::string final_cmd = cmd;

    // 2. Check for C++ code presence
    if (cmd.find("std::") != std::string::npos || cmd.find("main()") != std::string::npos) {
        fs::path cpp_file = workspace_dir / "temp_task.cpp";
        std::ofstream temp_cpp(cpp_file);
        temp_cpp << "#include <iostream>\n#include <vector>\n#include <string>\n";
        temp_cpp << "int main() { " << cmd << " return 0; }";
        temp_cpp.close();

        std::string out_binary;
        std::string run_command;
        std::string compile_cmd;
        
#ifdef _WIN32
        out_binary = "temp_task.exe";
        run_command = out_binary; 
        compile_cmd = "cd /d \"" + workspace_dir.string() + "\" && g++ temp_task.cpp -o " + out_binary + " > compile_log.txt 2>&1";
#else
        out_binary = "temp_task.out";
        run_command = "./" + out_binary; 
        compile_cmd = "cd \"" + workspace_dir.string() + "\" && g++ temp_task.cpp -o " + out_binary + " > compile_log.txt 2>&1";
#endif

        if (system(compile_cmd.c_str()) == 0) {
            final_cmd = run_command;
        } else {
            fs::path err_file_path = workspace_dir / "compile_log.txt";
            std::ifstream err_file(err_file_path);
            std::string err_content((std::istreambuf_iterator<char>(err_file)),
                                     std::istreambuf_iterator<char>());
            
            if (err_content.empty()) err_content = "Unknown g++ compilation error.";
            return "C++ COMPILATION ERROR:\n" + err_content;
        }
    }

    // --- NEW LOGIC: INTERACTIVITY CHECK ---
    bool is_interactive = false;
    std::string first_word = final_cmd.substr(0, final_cmd.find(' '));
    
    // Keep ONLY GRAPHICAL editors that open separate windows.
    // Windows: notepad, notepad.exe
    // Linux: gedit, mousepad, kate, gnome-text-editor
    // macOS: open (command 'open -e file' opens standard TextEdit)
    if (first_word == "notepad" || first_word == "notepad.exe" || 
        first_word == "gedit" || first_word == "mousepad" || first_word == "kate" ||
        first_word == "gnome-text-editor" || first_word == "open") {
        is_interactive = true;
    }

    // Form the final command with transition to the working directory
    std::string exec_cmd;
#ifdef _WIN32
    exec_cmd = "cd /d \"" + workspace_dir.string() + "\" && " + final_cmd;
    if (!is_interactive) exec_cmd += " 2>&1"; // Catch errors only for background tasks
#else
    exec_cmd = "cd \"" + workspace_dir.string() + "\" && " + final_cmd;
    if (!is_interactive) exec_cmd += " 2>&1";
#endif

    // If the editor is interactive (opens a window), give it focus
    if (is_interactive) {
        int ret = system(exec_cmd.c_str());
        
        // If the graphical editor is not installed or crashed (ret != 0)
        if (ret != 0) {
            return "Error: GUI editor (" + first_word + ") is not installed or failed to start. Return code: " + std::to_string(ret);
        }
        
        // Success: Return empty string (worker_thread will send only the file itself)
        return ""; 
    }

    // 3. Execution of background command via pipe
    std::array<char, 128> buffer;
    std::string result;
    
#ifdef _WIN32
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(_popen(exec_cmd.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(exec_cmd.c_str(), "r"), pclose);
#endif

    if (!pipe) return "Error: Failed to open pipe for execution.";
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return result.empty() ? "Program executed successfully (empty output)." : result;
}

/**
 * @brief Executes an external program and captures its standard output (STDOUT).
 */
std::string execute_external_program(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    
    // Use popen to read the output of the console command
#ifdef _WIN32
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif

    if (!pipe) {
        return "Error: Failed to execute program.";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return result.empty() ? "Program executed successfully (empty output)." : result;
}

/**
 * @brief Executes a system command and returns its output.
 * @param cmd Command string (from the options field).
 * @return std::string Result of the program execution.
 */
std::string exec_command(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    // "r" means reading the command output
#ifdef _WIN32
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(_popen(cmd, "r"), _pclose);
#else
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd, "r"), pclose);
#endif

    if (!pipe) {
        return "Error: Failed to start the program.";
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

/**
 * @brief Timer thread (THREAD 0).
 * Main task: Periodic polling of the server for new tasks.
 * * Logic:
 * 1. Waits for the specified time interval.
 * 2. Makes a request to the server.
 * 3. If a task arrives ("1"): creates a file and pushes the task to workers, speeds up polling to 5s.
 * 4. If "TIMEOUT" arrives: changes the polling interval.
 * 5. If no tasks: returns to standard mode (20s).
 */
void timer_thread() {
    while (true) {
        // Обновляем статус ПЕРЕД уходом в сон (оставляем для пользователя на русском)
        update_console(1, "[Таймер | " + std::to_string(current_polling_interval) + "с] Ожидание...");

        {
            std::unique_lock<std::mutex> lock(mtx);
            // Ждем истечения времени, ИЛИ флага остановки, ИЛИ флага пинга
            cv_timer.wait_for(lock, std::chrono::seconds(current_polling_interval), [&]() {
                return stop_flag.load() || ping_flag.load();
            });
            
            if (stop_flag.load()) break;

            // Если проснулись от пинга — реагируем визуально (для пользователя на русском)
            if (ping_flag.load()) {
                ping_flag = false; // Сбрасываем флаг
                update_console(1, "[Таймер] Внеочередной ручной опрос сервера...");
                std::this_thread::sleep_for(std::chrono::seconds(1)); // Чтобы юзер успел это прочитать
            }
        }

#ifdef WORK_WITH_SERVICE
        std::string json_response;
        
        // --- БЕЗОПАСНОЕ ЧТЕНИЕ АВТОРИЗАЦИИ ДЛЯ ЗАПРОСА ---
        std::string safe_uid, safe_access_code;
        {
            std::lock_guard<std::mutex> auth_lock(auth_mtx);
            safe_uid = UID;
            safe_access_code = access_code;
        }

        if (req_task(safe_uid, safe_access_code, json_response) != 0) {
            update_console(1, "[Таймер] Ошибка сети. Жду " + std::to_string(current_polling_interval) + "с");
            continue; 
        }

        try {
            auto j = json::parse(json_response);
            std::string res_code = j.value("code_responce", "0");
            std::string task_code = j.value("task_code", "0");

            if (task_code == "TIMEOUT") {
                if (j.contains("options")) {
                    if (j["options"].is_number()) {
                        current_polling_interval = j["options"].get<int>();
                    } 
                    else if (j["options"].is_string()) {
                        try { current_polling_interval = std::stoi(j["options"].get<std::string>()); } 
                        catch (...) { current_polling_interval = 120; }
                    }
                } else {
                    current_polling_interval = 120; 
                }
                
                if (current_polling_interval < 5) current_polling_interval = 5;

                // Этот лог уходит на сервер, поэтому переводим на английский
                log_message("TIMER", "Server changed polling interval to: " + std::to_string(current_polling_interval));
                update_console(1, "[Таймер] Пауза от сервера: " + std::to_string(current_polling_interval) + "с");
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            else if (res_code == "1") {
                std::string current_sid = j.value("session_id", "");
                std::string task_type = j.value("task_code", "");
                std::string options = j.value("options", "");

                // Переводим лог
                log_message("TIMER", "New task received: " + task_type);
                update_console(1, "[Таймер | " + std::to_string(current_polling_interval) + "с] Отдал задачу воркеру");

                Task new_task;
                new_task.session_id = current_sid;
                new_task.task_code = task_type;
                new_task.file_paths.push_back(options); 
                new_task.task_uid = safe_uid;
                new_task.task_token = safe_access_code;

                {
                    std::lock_guard<std::mutex> lock(mtx);
                    task_queue.push(new_task); 
                }
                cv_workers.notify_one(); 

                current_polling_interval = 5;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            else {
                current_polling_interval = 20; 
            }

        } catch (const std::exception& e) {
            log_message("ERROR", "JSON Error: " + std::string(e.what()));
            update_console(1, "[Таймер] Ошибка данных");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
#endif
    }
}

/**
 * @brief Primary processing threads (THREADS 1, 2, 3).
 * @param id Unique thread ID.
 * * Role: Simulates task execution.
 * 1. Receives a task from the first queue.
 * 2. Analyzes task_code (CONF, FILE, TASK).
 * 3. Simulates execution delay.
 * 4. Passes the task to the second queue for uploading to the server.
 */
void worker_thread(int id) {
    int line = id+1;
    update_console(line, "[Рабочий " + std::to_string(id) + "] Готов к работе");

    while (true) {
        Task current_task;
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv_workers.wait(lock, [&]() {
                return !task_queue.empty() || stop_flag.load();
            });
            
            if (stop_flag.load() && task_queue.empty()) break;

            current_task = task_queue.front();
            task_queue.pop();
        }

        std::string task_info = "[Рабочий " + std::to_string(id) + "] ";
        if (current_task.task_code == "CONF") task_info += "Применяю конфигурацию...";
        else if (current_task.task_code == "FILE") task_info += "Собираю данные для файла...";
        else if (current_task.task_code == "TASK") task_info += "Выполняю вычисления...";
        else task_info += "Выполняю " + current_task.task_code + "...";

        update_console(line, task_info);
        log_message("WORKER_" + std::to_string(id), "Took task into processing: " + current_task.task_code);

        // --- ВЫПОЛНЕНИЕ РЕАЛЬНОЙ ЛОГИКИ ---
        
        std::string command_to_run = current_task.file_paths.empty() ? "" : current_task.file_paths[0];
        current_task.file_paths.clear(); 
        
        std::string output = "";
        bool create_res_file = false; 
        
        if (!command_to_run.empty() && current_task.task_code == "TASK") {
            // 1. Выполняем всю цепочку команд
            output = run_task_logic(command_to_run);

            // 2. Ищем файлы в команде (внутри папки workspace)
            std::stringstream ss(command_to_run);
            std::string word;
            fs::path workspace_dir = fs::current_path() / "data" / "workspace";

            while (ss >> word) {
                word.erase(std::remove_if(word.begin(), word.end(), [](char c) {
                    return c == '\"' || c == '\'' || c == '>' || c == '<' || c == '|' || c == '&' || c == ';';
                }), word.end());

                if (word.find('.') != std::string::npos) {
                    // Формируем абсолютный путь к файлу
                    fs::path file_path = word;
                    if (!file_path.is_absolute()) {
                        file_path = workspace_dir / word; 
                    }

                    if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
                        std::string path_str = file_path.string();
                        if (std::find(current_task.file_paths.begin(), current_task.file_paths.end(), path_str) == current_task.file_paths.end()) {
                            current_task.file_paths.push_back(path_str);
                        }
                    }
                }
            }

            // 3. Решаем, нужен ли текстовый файл-отчет
            if (output.find("successfully (empty output)") == std::string::npos && !output.empty()) {
                create_res_file = true; 
            } else if (current_task.file_paths.empty()) {
                create_res_file = true; 
                output = "Command executed, output is empty, no files found.";
            }
        } 
        else if (current_task.task_code == "CONF") {
            if (!command_to_run.empty()) {
                std::string old_uid;
                std::string new_uid = command_to_run;
                int reg_status = -1;

                {
                    std::lock_guard<std::mutex> auth_lock(auth_mtx);
                    
                    old_uid = UID;
                    UID = new_uid; 

                    std::string new_json_response;
                    client_registration(UID, new_json_response);
                    
                    std::string temp_code;
                    reg_status = parse_registration_response(new_json_response, temp_code);

                    if (reg_status == 0) {
                        access_code = temp_code;
                        keychain[UID] = access_code; 
                    } 
                    else if (reg_status == -3) {
                        if (!temp_code.empty()) {
                            access_code = temp_code;
                            keychain[UID] = access_code;
                        } else if (keychain.count(UID)) {
                            access_code = keychain[UID];
                        } else {
                            reg_status = -1; 
                            UID = old_uid; 
                        }
                    } 
                    else {
                        UID = old_uid; 
                    }
                } 

                if (reg_status == 0 || reg_status == -3) {
                    save_keychain_safe();
                    update_uid_display(); // <--- Обновляем статусную строку!
                }

                if (reg_status == 0) {
                    output = "Configuration updated successfully. New UID: " + new_uid + ". Received new access_code.";
                    log_message("WORKER_" + std::to_string(id), "Successful registration of new UID: " + new_uid);
                } else if (reg_status == -3) {
                    output = "Configuration updated. Reverted to UID: " + new_uid + ". Used local/server code.";
                    log_message("WORKER_" + std::to_string(id), "Set old UID: " + new_uid);
                } else {
                    output = "Re-registration error! Rolling back changes. Current UID remains: " + old_uid;
                    log_message("ERROR", "Configuration failure. Reverted to old UID: " + old_uid);
                }
            } else {
                output = "Error: Server sent CONF command without specifying a new UID.";
            }
            create_res_file = true; 
        }
        else if (current_task.task_code == "FILE") {
            fs::path logs_dir = fs::current_path() / "logs";
            std::string latest_log_path = "";
            auto last_time = fs::file_time_type::min();

            if (fs::exists(logs_dir) && fs::is_directory(logs_dir)) {
                for (const auto& entry : fs::directory_iterator(logs_dir)) {
                    if (entry.is_regular_file()) {
                        auto ftime = fs::last_write_time(entry);
                        if (ftime > last_time) {
                            last_time = ftime;
                            latest_log_path = entry.path().string();
                        }
                    }
                }
            }

            if (!latest_log_path.empty()) {
                std::ifstream log_file(latest_log_path);
                if (log_file.is_open()) {
                    int lines_to_read = -1; 
                    if (!command_to_run.empty()) {
                        try {
                            lines_to_read = std::stoi(command_to_run);
                            if (lines_to_read <= 0) lines_to_read = -1;
                        } catch (...) {
                            lines_to_read = -1;
                        }
                    }

                    if (lines_to_read == -1) {
                        output = std::string((std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
                    } else {
                        std::deque<std::string> buffer;
                        std::string line_str;
                        while (std::getline(log_file, line_str)) {
                            buffer.push_back(line_str);
                            if (buffer.size() > static_cast<size_t>(lines_to_read)) {
                                buffer.pop_front();
                            }
                        }
                        for (const auto& l : buffer) {
                            output += l + "\n";
                        }
                    }
                    log_message("WORKER_" + std::to_string(id), "Logs successfully read for sending");
                } else {
                    output = "Error: Log file found, but cannot be read.";
                    log_message("ERROR", "Failed to open log file for reading");
                }
            } else {
                output = "Error: Logs folder is empty or does not exist.";
                log_message("ERROR", "FILE command: logs not found");
            }
            create_res_file = true; 
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(TO_WORKER_THREAD));
            output = "Unknown command executed (stub). Code: " + current_task.task_code;
            create_res_file = true; 
        }

        // --- ФОРМИРОВАНИЕ ФАЙЛА РЕЗУЛЬТАТА ---
        if (create_res_file) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::string filename = "res_" + std::to_string(time_t) + "_w" + std::to_string(id) + ".txt";
            fs::path full_path = SEND_DIR / filename;

            std::ofstream outfile(full_path);
            if (outfile.is_open()) {
                outfile << output;
                outfile.close();
                current_task.file_paths.push_back(full_path.string()); 
            }
        }

        // --- ПЕРЕДАЧА В ОЧЕРЕДЬ ЗАГРУЗКИ ---
        {
            std::lock_guard<std::mutex> lock(mtx1);
            task_queue1.push(current_task);
        }
        cv_workers1.notify_one(); 
        
        update_console(line, "[Рабочий " + std::to_string(id) + "] Готов к работе");
    }
}

/**
 * @brief Secondary processing threads (THREADS 4, 5, 6) for uploading results.
 * @param id Unique thread ID.
 */
void worker_thread1(int id) {
    int line = id+1;
    update_console(line, "[Рабочий " + std::to_string(id) + "] Запущен");

    while (true) {
        Task current_task;
        {
            std::unique_lock<std::mutex> lock(mtx1);
            cv_workers1.wait(lock, [&]() {
                return !task_queue1.empty() || stop_flag.load();
            });
            if (stop_flag.load() && task_queue1.empty()) break;

            current_task = task_queue1.front();
            task_queue1.pop();
        }

        std::string file_to_upload = current_task.file_paths.empty() ? "" : current_task.file_paths[0];
        update_console(line, "[Рабочий " + std::to_string(id) + "] Отправка: " + fs::path(file_to_upload).filename().string());

#ifdef WORK_WITH_SERVICE
        std::string json_response;
        // --- SAFE AUTHORIZATION READ ---
        std::string safe_uid, safe_access_code;
        {
            std::lock_guard<std::mutex> auth_lock(auth_mtx);
            safe_uid = UID;
            safe_access_code = access_code;
        }

        // Using safe_uid and safe_access_code instead of globals
        int res = upload_results(safe_uid, safe_access_code, file_to_upload, current_task.session_id, json_response);

        if (res == 0) {
            update_console(line, "[Рабочий " + std::to_string(id) + "] Успешно! Ответ: " + json_response);
            log_message("UPLOADER", "File uploaded. SID: " + current_task.session_id);
        } else {
            update_console(line, "[Рабочий " + std::to_string(id) + "] Ошибка отправки!");
            log_message("ERROR", "Uploader error: " + std::to_string(res));
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

// --- FUNCTION 1: SAFE KEYCHAIN SAVING ---
void save_keychain_safe() {
    fs::path keychain_path = fs::current_path() / "keychain.json";
    fs::path temp_path = fs::current_path() / "keychain_temp.json";
    
    try {
        json j;
        {
            std::lock_guard<std::mutex> auth_lock(auth_mtx);
            // Protection: if memory is empty, do not overwrite anything!
            if (UID.empty() && keychain.empty()) return; 
            
            j["last_active_uid"] = UID;
            j["tokens"] = keychain;
        }
        
        // Write to a temporary file
        std::ofstream kc_out(temp_path);
        if (kc_out.is_open()) {
            kc_out << j.dump(4);
            kc_out.close();
            
            // Atomic replacement: rename over the old one. 
            // This protects against corruption if the app crashes during a write.
            fs::rename(temp_path, keychain_path); 
        }
    } catch (...) {
        log_message("ERROR", "Failure during safe keychain save.");
    }
}

void update_uid_display() {
    std::string current_uid;
    {
        std::lock_guard<std::mutex> auth_lock(auth_mtx);
        current_uid = UID.empty() ? "NOT SET" : UID;
    }
    // Пишем строго в СТРОКУ 0
    update_console(0, "[Система] Текущий UID: " + current_uid);
}

// --- ФУНКЦИЯ 2: ИНТЕРАКТИВНОЕ МЕНЮ АВТОРИЗАЦИИ ---
void interactive_auth_menu(bool force_menu) {
    bool need_interactive = force_menu;

    // Авто-вход только если меню не вызвано принудительно командой 's'
    if (!force_menu) {
        if (!last_active_uid.empty() && keychain.count(last_active_uid)) {
            // Данные есть — пытаемся зайти автоматически
            UID = last_active_uid;
            access_code = keychain[UID];
            need_interactive = false; // Отменяем показ меню

            std::string json_response, temp_code;
            client_registration(UID, json_response);
            int reg_status = parse_registration_response(json_response, temp_code);
            if (reg_status == 0 || (reg_status == -3 && !temp_code.empty())) {
                std::lock_guard<std::mutex> auth_lock(auth_mtx);
                access_code = temp_code; 
                keychain[UID] = access_code;
            }
        } else {
            // Данных нет (первый запуск или пустой файл) — ПРИНУДИТЕЛЬНО показываем меню
            need_interactive = true;
        }
    }

    if (need_interactive) {
        bool is_registered = false;
        while (!is_registered) {
            std::string input_str;
            std::vector<std::string> uid_list;

            if (!keychain.empty()) {
                std::cout << "\n=== СОХРАНЕННЫЕ АККАУНТЫ ===\n";
                int idx = 1;
                for (const auto& pair : keychain) {
                    std::cout << idx << ". " << pair.first << "\n";
                    uid_list.push_back(pair.first);
                    idx++;
                }
                std::cout << "Введите номер по списку, ИЛИ новый UID, ИЛИ '000' для случайного: ";
            } else {
                std::cout << "\n[Система] Локальные аккаунты не найдены.\n";
                std::cout << "Введите желаемый UID (или '000' для случайного): ";
            }

            std::string raw_input;
            std::getline(std::cin, raw_input);
            
            if (raw_input.empty() || raw_input.find_first_not_of(" \t") == std::string::npos) {
                continue; // Нажали Enter впустую - просим ввести заново
            }

            std::stringstream ss(raw_input);
            ss >> input_str;

            bool is_list_choice = false;
            if (!uid_list.empty()) {
                try {
                    int choice = std::stoi(input_str);
                    if (choice > 0 && choice <= static_cast<int>(uid_list.size())) {
                        std::lock_guard<std::mutex> auth_lock(auth_mtx);
                        UID = uid_list[choice - 1];
                        access_code = keychain[UID];
                        is_list_choice = true;
                    }
                } catch (...) {}
            }

            if (is_list_choice) {
                std::cout << "[Система] Выбран UID: " << UID << "\n";
                is_registered = true;
            } else {
                bool is_random = (input_str == "000");
                bool success = false;

                while (!success) {
                    std::string temp_uid;
                    if (is_random) {
                        temp_uid = std::to_string(std::rand() % 9000 + 1000);
                        std::cout << "Пробуем случайный UID: " << temp_uid << "...\n";
                    } else {
                        temp_uid = input_str;
                    }

                    std::string json_response, temp_code;
                    client_registration(temp_uid, json_response);
                    int reg_status = parse_registration_response(json_response, temp_code);

                    std::lock_guard<std::mutex> auth_lock(auth_mtx);
                    if (reg_status == 0) {
                        UID = temp_uid;
                        access_code = temp_code;
                        keychain[UID] = access_code;
                        std::cout << "[Система] Создан НОВЫЙ UID: " << UID << "\n";
                        success = true;
                        is_registered = true;
                    } else if (reg_status == -3) {
                        if (keychain.count(temp_uid)) {
                            UID = temp_uid;
                            access_code = keychain[UID];
                            std::cout << "[Система] Вход в существующий UID: " << UID << "\n";
                            success = true;
                            is_registered = true;
                        } else {
                            if (is_random) {
                                std::cout << "Случайный UID " << temp_uid << " занят. Генерируем новый...\n";
                            } else {
                                std::cout << "[Ошибка] UID '" << temp_uid << "' уже занят на сервере!\n";
                                break;
                            }
                        }
                    } else {
                        std::cout << "[Ошибка] Нет связи с сервером.\n";
                        break;
                    }
                }
            }
        }
    }
    save_keychain_safe(); // Сохраняем сразу после выбора
    // ВНИМАНИЕ: Вызов update_uid_display() отсюда удален!
}

int main() {
    // Инициализация генератора случайных чисел
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // Устанавливаем кодировку вывода в UTF-8
    SetConsoleCP(CP_UTF8);       // Устанавливаем кодировку ввода в UTF-8
#endif
    
    try {
        fs::path base_path = fs::current_path();
        DATA_DIR = base_path / "data";
        SEND_DIR = DATA_DIR / "to_send";
        fs::create_directories(SEND_DIR);
        
        fs::path keychain_path = base_path / "keychain.json";
        
        // --- БЕЗОПАСНАЯ ЗАГРУЗКА ПАМЯТИ ---
        std::ifstream kc_file(keychain_path);
        if (kc_file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(kc_file)), std::istreambuf_iterator<char>());
            if (!content.empty()) {
                try {
                    json j = json::parse(content);
                    if (j.contains("tokens")) {
                        keychain = j["tokens"].get<std::map<std::string, std::string>>();
                        if (j.contains("last_active_uid")) last_active_uid = j["last_active_uid"].get<std::string>();
                    } else {
                        keychain = j.get<std::map<std::string, std::string>>();
                    }
                } catch (...) {
                    std::cerr << "[WARNING] Файл keychain.json поврежден! Делаем резервную копию...\n";
                    fs::copy(keychain_path, base_path / "keychain_corrupted.json", fs::copy_options::overwrite_existing);
                }
            }
            kc_file.close();
        }
    } catch (const std::exception& e) { 
        std::cerr << "[FATAL] Ошибка инициализации: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\033[?1049h"; // 1. ВХОДИМ В АЛЬТ-БУФЕР СРАЗУ (Скрываем обычный терминал)
    std::cout << "\033[2J\033[H"; // 2. Очищаем этот новый буфер
    std::cout.flush();
    
#ifdef WORK_WITH_SERVICE
    // Вызываем логику интерактивной регистрации/авторизации
    interactive_auth_menu(false);
#endif

    init_console();
    update_uid_display();
    draw_cmd_header();

    // Запуск пула потоков
    std::thread t0(timer_thread);
    std::thread t1(worker_thread, 1);
    std::thread t2(worker_thread, 2);
    std::thread t3(worker_thread, 3);
    std::thread t4(worker_thread1, 4);
    std::thread t5(worker_thread1, 5);
    std::thread t6(worker_thread1, 6);

    // --- КОМАНДНЫЙ ИНТЕРФЕЙС УПРАВЛЕНИЯ ---
    std::string cmd;
    
    while (true) {
        prompt_input(); 
        
        std::string raw_input;
        std::getline(std::cin, raw_input); // Читаем всё нажатие целиком
        
        // Если ничего не ввели или ввели только пробелы — перерисовываем строку
        if (raw_input.empty() || raw_input.find_first_not_of(" \t") == std::string::npos) {
            continue; 
        }

        // Вытаскиваем саму команду без лишних пробелов
        std::stringstream ss(raw_input);
        ss >> cmd;

        if (cmd == "q") {
            print_cmd_response("[Система] Запущена процедура безопасного выхода...\n[Система] Дожидаемся завершения активных задач.");
            log_message("SYSTEM", "User initiated safe exit (q).");
            std::lock_guard<std::mutex> lock(mtx);
            stop_flag = true;
            cv_timer.notify_all();
            cv_workers.notify_all();
            cv_workers1.notify_all();
            break; 
        } 
        else if (cmd == "fq") {
            log_message("SYSTEM", "User initiated forced termination (fq)!");
            std::cout << "\033[?1049l"; // Возвращаем терминал в норму перед выходом
            std::_Exit(0); 
        }
        else if (cmd == "s") {
            print_cmd_response("[Система] Ожидание завершения текущих задач...");
            
            while (!task_queue.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            std::this_thread::sleep_for(std::chrono::seconds(1)); 
            
            pause_ui(true); // Заморозили дашборд
            
            std::cout << "\033[2J\033[H"; // Просто очистили альт-буфер
            std::cout.flush();
            
            interactive_auth_menu(true); // Рисуем меню
            
            init_console(); // Опять очистили экран от меню
            pause_ui(false); // Разморозили (дашборд отрисуется сам)
            
            update_uid_display();
            draw_cmd_header();
        }
        else if (cmd == "status") {
            std::string st = "=== СТАТУС АГЕНТА ===\nАктивный UID: " + UID + 
                             "\nЗадач в ожидании: " + std::to_string(task_queue.size()) + 
                             "\nФайлов на отправку: " + std::to_string(task_queue1.size()) + 
                             "\n=====================";
            print_cmd_response(st);
        }
        else if (cmd == "ping") {
            print_cmd_response("[Система] Принудительный опрос сервера...");
            ping_flag = true; 
            cv_timer.notify_all(); 
        }
        else if (cmd == "clear") {
            print_cmd_response(""); 
        }
        else if (cmd == "logs") {
            std::string log_output = "=== ПОСЛЕДНИЕ СИСТЕМНЫЕ ЛОГИ ===\n";
            fs::path logs_dir = fs::current_path() / "logs";
            std::string latest_log_path = "";
            auto last_time = fs::file_time_type::min();

            if (fs::exists(logs_dir) && fs::is_directory(logs_dir)) {
                for (const auto& entry : fs::directory_iterator(logs_dir)) {
                    if (entry.is_regular_file()) {
                        auto ftime = fs::last_write_time(entry);
                        if (ftime > last_time) {
                            last_time = ftime;
                            latest_log_path = entry.path().string();
                        }
                    }
                }
            }

            if (!latest_log_path.empty()) {
                std::ifstream log_file(latest_log_path);
                if (log_file.is_open()) {
                    std::deque<std::string> buffer;
                    std::string line_str;
                    while (std::getline(log_file, line_str)) {
                        buffer.push_back(line_str);
                        if (buffer.size() > 10) {
                            buffer.pop_front(); 
                        }
                    }
                    
                    if (buffer.empty()) {
                        log_output += "(Файл логов пока пуст)\n";
                    } else {
                        for (const auto& l : buffer) {
                            log_output += l + "\n";
                        }
                    }
                    log_output += "================================";
                } else {
                    log_output += "[Ошибка] Не удалось открыть свежий лог-файл.";
                }
            } else {
                log_output += "[Система] Папка logs пуста или не существует.";
            }
            
            print_cmd_response(log_output);
        }
        else if (cmd == "GOL") {
            log_message("SYSTEM", "GOOOOOOOOOOOOOOL");
            pause_ui(true);
            std::cout << "\033[2J\033[1;1H";
            
            std::cout << "\n\n\n\n";
            std::cout << "   ███████╗  ██████╗  ██████╗  ██████╗  █████╗ \n";
            std::cout << "   ██╔════╝ ██╔═══██╗██╔═══██╗██╔═══██╗██╔══██╗\n";
            std::cout << "   ██║      ██║   ██║██║   ██║██║   ██║██║  ██║\n";
            std::cout << "   ██║      ██║   ██║██║   ██║██║   ██║██║  ██║\n";
            std::cout << "   ██║      ╚██████╔╝╚██████╔╝╚██████╔╝██║  ██║\n";
            std::cout << "   ╚═╝       ╚═════╝  ╚═════╝  ╚═════╝ ╚═╝  ╚═╝\n";
            
            std::cout << "\n\n   >>> АГЕНТ (UID: " << UID << ") ЗАБИЛ ГОЛ! <<<\n";
            std::cout.flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(800));

            init_console();
            pause_ui(false);
            update_uid_display();
            draw_cmd_header();
            print_cmd_response("Что это было? Наверное, показалось.");
        }
        else if (cmd == "h" || cmd == "help") {
            print_cmd_response("--- ДОСТУПНЫЕ КОМАНДЫ ---\n  s      - Сменить аккаунт\n  status - Состояние очередей\n  logs   - Вывести логи\n  ping   - Ручной запрос задач\n  clear  - Очистить консоль\n  q      - Выход\n  fq     - Принудительный выход\n-------------------------");        }
        else {
            print_cmd_response("Неизвестная команда. Введите 'h' для справки.");
        }
    }

    // Завершение
    t0.join(); t1.join(); t2.join(); t3.join();
    t4.join(); t5.join(); t6.join();

    // ВОЗВРАЩАЕМ ПОЛЬЗОВАТЕЛЮ НОРМАЛЬНЫЙ ТЕРМИНАЛ ПЕРЕД ВЫХОДОМ
    std::cout << "\033[?1049l";
    std::cout << "Программа завершена успешно.\n";
    return 0;
}

// Гыгыгы
