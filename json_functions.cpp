/**
 * @file json_functions.cpp
 * @brief Module for processing JSON responses from the server.
 * Performs data deserialization and extraction of control parameters (tokens, session IDs).
 */
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "json_functions.h" // Ensure this header matches the signatures
#include "logger.h"

using json = nlohmann::json;

/**
 * @brief Parsing the response during agent registration.
 * @param jsonResponse Raw JSON text from the server.
 * @param out_access_code [out] Reference to store the received access code.
 * @return int Registration status:
 * 0 - Successful new registration.
 * -3 - Agent is already registered (using existing code).
 * -1 - Parsing error or missing mandatory fields.
 * * Logic: Extracts the "access_code", which is required for all subsequent
 * authorized requests to the API.
 */
int parse_registration_response(const std::string& jsonResponse, std::string& out_access_code)
{
    try {
        json data = json::parse(jsonResponse);

        // Check if the key exists to prevent crashes
        if (!data.contains("code_responce")) return -1;

        std::string code = data["code_responce"].get<std::string>();

        if (code == "0" || code == "-3") {
            if (data.contains("access_code")) {
                // Correctly assign to the reference parameter
                out_access_code = data["access_code"].get<std::string>();
            }

            #ifdef SEQ_OUT
            if (data.contains("msg")) std::cout << data["msg"] << std::endl;
            #endif

            return (code == "0") ? 0 : -3;
        }
    }
    catch (const json::exception& e) {
        log_message("JSON_ERROR", "JSON Error during registration: " + std::string(e.what()));
        return -1;
    }
    return -1;
}

/**
 * @brief Parsing the response when polling the server for tasks.
 * @param jsonResponse Raw JSON text from the server.
 * @param out_session_id [out] Reference to store the current task session ID.
 * @return int Task status:
 * 1 - New task received.
 * 0 - No tasks on the server (standby mode).
 * -1 - JSON format or network protocol error.
 * * Logic: Checks "code_responce". If the value is "1", it captures the "session_id",
 * which links the file processing and subsequent result uploading.
 */
int parse_req_task(const std::string& jsonResponse, std::string& out_session_id)
{
    try {
        json data = json::parse(jsonResponse);

        if (!data.contains("code_responce")) return -1;

        std::string code = data["code_responce"].get<std::string>();

        if (code == "1") {
            if (data.contains("session_id")) {
                // Save session_id to the passed reference
                out_session_id = data["session_id"].get<std::string>();
            }
            return 1;
        }
        if (code == "0") return 0;
    }
    catch (...) {
        return -1;
    }
    return -1;
}