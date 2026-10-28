#include "AIDiagnoser.hpp"
#include <yaml-cpp/yaml.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <filesystem>

#ifdef HAVE_LIBCURL
#include <curl/curl.h>

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string AIDiagnoser::httpPost(const std::string& url, const std::string& jsonPayload) {
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return "";
    return response;
}
#else
// CLI curl fallback (cross-platform for Windows 10/11 & Linux/WSL)
std::string AIDiagnoser::httpPost(const std::string& url, const std::string& jsonPayload) {
    std::string tmpPath = "omni_ai_req_" + std::to_string(std::rand()) + ".json";
    {
        std::ofstream out(tmpPath, std::ios::binary);
        if (!out) return "";
        out << jsonPayload;
    }

    std::string cmd = "curl -s -X POST \"" + url + "\" -H \"Content-Type: application/json\" --data-binary \"@" + tmpPath + "\"";
    
    std::string response;
    FILE* pipe = nullptr;
#ifdef _WIN32
    pipe = _popen(cmd.c_str(), "r");
#else
    pipe = popen(cmd.c_str(), "r");
#endif

    if (pipe) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            response += buffer;
        }
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
    }

    std::error_code ec;
    std::filesystem::remove(tmpPath, ec);
    return response;
}
#endif

namespace {

std::string escapeJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\b') o << "\\b";
        else if (c == '\f') o << "\\f";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else if (c == '\t') o << "\\t";
        else o << c;
    }
    return o.str();
}

} // namespace

std::string AIDiagnoser::loadApiKey() {
    // 1. Check system environment variables first
    const char* envVal = std::getenv("GEMINI_API_KEY");
    if (envVal && *envVal) return std::string(envVal);
    envVal = std::getenv("AI_API_KEY");
    if (envVal && *envVal) return std::string(envVal);

    // 2. Check .env files in standard locations
    std::vector<std::string> candidates = {
        ".env",
        "omnilearn_backend/.env",
        "../.env",
        "../omnilearn_backend/.env"
    };

    for (const auto& path : candidates) {
        std::ifstream file(path);
        if (!file.is_open()) continue;
        std::string line;
        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || line[start] == '#') continue;
            line = line.substr(start);

            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = line.substr(0, eqPos);
            size_t keyEnd = key.find_last_not_of(" \t\r\n");
            if (keyEnd != std::string::npos) key = key.substr(0, keyEnd + 1);

            if (key == "GEMINI_API_KEY" || key == "AI_API_KEY" || key == "API_KEY") {
                std::string val = line.substr(eqPos + 1);
                size_t valStart = val.find_first_not_of(" \t\r\n");
                size_t valEnd = val.find_last_not_of(" \t\r\n");
                if (valStart != std::string::npos && valEnd != std::string::npos) {
                    val = val.substr(valStart, valEnd - valStart + 1);
                } else {
                    val.clear();
                }
                if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                        (val.front() == '\'' && val.back() == '\''))) {
                    val = val.substr(1, val.size() - 2);
                }
                if (!val.empty()) return val;
            }
        }
    }
    return "";
}

bool AIDiagnoser::isConfigured() {
    return !loadApiKey().empty();
}

AIDiagnosis AIDiagnoser::diagnose(
    const std::string& questionText,
    const std::string& studentAnswer,
    const std::string& correctAnswer,
    const std::string& answerType,
    const std::vector<std::string>& targetConcepts)
{
    AIDiagnosis result;
    std::string apiKey = loadApiKey();
    if (apiKey.empty()) return result;

    std::string conceptsStr;
    for (size_t i = 0; i < targetConcepts.size(); ++i) {
        if (i) conceptsStr += ", ";
        conceptsStr += targetConcepts[i];
    }

    std::string promptText =
        "Question: " + questionText + "\\n" +
        "Target Concept(s): " + conceptsStr + "\\n" +
        "Answer Type: " + answerType + "\\n" +
        "Correct Answer: " + correctAnswer + "\\n" +
        "Student's Incorrect Answer: " + studentAnswer;

    std::ostringstream payload;
    payload << "{\n"
            << "  \"system_instruction\": {\n"
            << "    \"parts\": [{\n"
            << "      \"text\": \"You are an expert math tutor diagnostic engine. A student was given a problem and answered INCORRECTLY. Analyze the student's specific number/answer, reverse-engineer what arithmetic or conceptual error they made to get that exact result, and output your diagnosis in JSON.\"\n"
            << "    }]\n"
            << "  },\n"
            << "  \"contents\": [{\n"
            << "    \"role\": \"user\",\n"
            << "    \"parts\": [{\n"
            << "      \"text\": \"" << escapeJson(promptText) << "\"\n"
            << "    }]\n"
            << "  }],\n"
            << "  \"generationConfig\": {\n"
            << "    \"response_mime_type\": \"application/json\",\n"
            << "    \"response_schema\": {\n"
            << "      \"type\": \"OBJECT\",\n"
            << "      \"properties\": {\n"
            << "        \"error_name\": {\"type\": \"STRING\", \"description\": \"A CamelCase identifier for the mistake, e.g. AddedInsteadOfMultiplying\"},\n"
            << "        \"explanation\": {\"type\": \"STRING\", \"description\": \"Concise 1-sentence explanation of what calculation/error the student likely performed without revealing the correct answer\"}\n"
            << "      },\n"
            << "      \"required\": [\"error_name\", \"explanation\"]\n"
            << "    }\n"
            << "  }\n"
            << "}";

    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent?key=" + apiKey;
    std::string response = httpPost(url, payload.str());
    if (response.empty()) return result;

    try {
        // Parse the Gemini API response. JSON is valid YAML, so yaml-cpp parses it cleanly.
        YAML::Node root = YAML::Load(response);
        if (root["error"]) {
            std::string msg = root["error"]["message"] ? root["error"]["message"].as<std::string>() : "Unknown API error";
            std::cerr << "  [AI Diagnoser API Error] " << msg << "\n";
            return result;
        }
        if (!root["candidates"] || root["candidates"].size() == 0) return result;

        YAML::Node candidate = root["candidates"][0];
        if (!candidate["content"] || !candidate["content"]["parts"] || candidate["content"]["parts"].size() == 0) return result;

        std::string rawJson = candidate["content"]["parts"][0]["text"].as<std::string>();
        YAML::Node diagNode = YAML::Load(rawJson);

        if (diagNode["error_name"]) result.errorName = diagNode["error_name"].as<std::string>();
        if (diagNode["explanation"]) result.explanation = diagNode["explanation"].as<std::string>();

        result.success = !result.explanation.empty();
    } catch (const std::exception&) {
        // Silently fall back if response parsing fails
        result.success = false;
    }

    return result;
}
