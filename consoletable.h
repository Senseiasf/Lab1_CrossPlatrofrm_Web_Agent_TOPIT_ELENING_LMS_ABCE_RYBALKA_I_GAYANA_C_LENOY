//#define SEQ_OUT
#include <string>

void init_console(void);
void update_console(int line, const std::string& text);
void draw_cmd_header();
void prompt_input();
void print_cmd_response(const std::string& msg); 
void pause_ui(bool pause);