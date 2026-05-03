/**
 * @file consoletab.cpp
 * @brief Модуль управления консольным интерфейсом (TUI).
 * Обеспечивает разделение экрана: статичные статусы сверху, интерактивное меню снизу.
 */
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <chrono>

std::string get_time_str() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::string time_str = ctime(&time_t);
    time_str.pop_back(); // удаляем \n
    return time_str;
}

std::mutex console_mutex;

struct ConsoleTable {
    std::vector<std::string> lines;

    void set_line(int line_num, const std::string& text) {
        if (line_num >= lines.size()) {
            lines.resize(line_num + 1);
        }
        lines[line_num] = get_time_str() + " " + text;
    }

    void render() {
        // \033[s сохраняет текущую позицию курсора (там, где пользователь вводит текст)
        std::cout << "\033[s";
        std::cout << "\033[1;1H"; // Прыгаем в самый верх (строка 1)

        for (size_t i = 0; i < lines.size(); ++i) {
            std::cout << "\033[K"; // Очищаем строку от старого текста
            if (!lines[i].empty()) {
                std::cout << lines[i];
            }
            std::cout << "\n";
        }

        // \033[u возвращает курсор ровно туда, откуда мы его забрали
        std::cout << "\033[u";
        std::cout.flush();
    }
};

ConsoleTable table;

void update_console(int line, const std::string& text) {
#ifndef SEQ_OUT
    std::lock_guard<std::mutex> lock(console_mutex);
    table.set_line(line, text);
    table.render();
#endif
}

void init_console(void) {
    std::cout << "\033[2J\033[H"; // Полная очистка экрана при старте
    update_console(0, "[Таймер] Ожидание...");
    update_console(1, "[Рабочий 1] Ожидание...");
    update_console(2, "[Рабочий 2] Ожидание...");
    update_console(3, "[Рабочий 3] Ожидание...");
    update_console(4, "[Загрузчик 1] Ожидание...");
    update_console(5, "[Загрузчик 2] Ожидание...");
    update_console(6, "[Загрузчик 3] Ожидание...");
}

// =========================================================================
// НОВЫЙ БЛОК: ФУНКЦИИ УПРАВЛЕНИЯ ИНТЕРАКТИВНЫМ МЕНЮ (НИЖНЯЯ ЧАСТЬ ЭКРАНА)
// =========================================================================

/**
 * @brief Отрисовка заголовка меню (строки 9-11)
 */
void draw_cmd_header() { 
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "\033[s"; 
    std::cout << "\033[9;1H\033[0J"; 
    std::cout << "=================================================\n";
    std::cout << " ИНТЕРФЕЙС УПРАВЛЕНИЯ. Введите 'h' для справки.\n";
    std::cout << "=================================================\n";
    std::cout << "\033[u"; 
    std::cout.flush();
}

/**
 * @brief Подготовка строки ввода (строка 13)
 */
void prompt_input() {
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "\033[13;1H\033[K> "; // Переход на 13 строку, очистка строки
    std::cout.flush();
}

/**
 * @brief Вывод ответов системы (начиная с 14 строки)
 */
void print_cmd_response(const std::string& msg) {
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "\033[s";
    std::cout << "\033[14;1H\033[0J"; // Переход на 14 строку, очистка старых ответов
    std::cout << msg << "\n";
    std::cout << "\033[u";
    std::cout.flush();
}