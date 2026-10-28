#ifndef AI_DIAGNOSER_HPP
#define AI_DIAGNOSER_HPP

#include <string>
#include <vector>

struct AIDiagnosis {
    bool success = false;
    std::string errorName;
    std::string explanation;
};

class AIDiagnoser {
private:
    static std::string loadApiKey();
    static std::string httpPost(const std::string& url, const std::string& jsonPayload);

public:
    // Returns true if an API key was found in .env or environment variables.
    static bool isConfigured();

    // Sends the question, student answer, and correct answer to the LLM to deduce the error.
    static AIDiagnosis diagnose(
        const std::string& questionText,
        const std::string& studentAnswer,
        const std::string& correctAnswer,
        const std::string& answerType,
        const std::vector<std::string>& targetConcepts
    );
};

#endif // AI_DIAGNOSER_HPP
