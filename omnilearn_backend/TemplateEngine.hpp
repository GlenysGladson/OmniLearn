#pragma once
#include "LearnerModel.hpp"
#include <z3++.h>
#include <vector>
#include <string>
#include <map>
#include <set>

// Tells main.cpp how to prompt for / evaluate the answer.
enum class AnswerFormat { SET_ANSWER, PROBABILITY_ANSWER };

struct GeneratedQuestion {
    std::string questionText;
    AnswerFormat format = AnswerFormat::SET_ANSWER;
    std::vector<int> correctAnswer;
    std::vector<std::string> targetConcepts; 
    
    // Maps to hold the generated concrete values
    std::map<std::string, std::vector<int>> setGivens;
    std::map<std::string, std::string> probGivens;
    std::string expectedProbAnswer;
};

class TemplateEngine {
private:
    std::vector<int> decodeBitVector(unsigned val, int universeSize);

    // Guards against generating the exact same question (same givens)
    // twice in a session. Repeating a *concept* is expected and required
    // for mastery -- repeating the identical question is not.
    std::set<std::string> usedSignatures;
    static const int kMaxRetries = 25;

    std::string makeSignature(const GeneratedQuestion& q) const;
    bool registerIfUnique(const GeneratedQuestion& q);

public:
    GeneratedQuestion selectAndInitializeTemplate(const LearnerModel& learner, const FormalTheory& theory);

    GeneratedQuestion generateUnion();
    GeneratedQuestion generateIntersectionDifference();
    GeneratedQuestion generateMembership();
    GeneratedQuestion generateSubset();
    GeneratedQuestion generateComplement();

    // Probability Generators
    GeneratedQuestion generateProbabilityInclusionExclusion();
    GeneratedQuestion generateProbabilityComplement();
    GeneratedQuestion generateConditionalProbability();
};