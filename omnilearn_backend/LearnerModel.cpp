#include "LearnerModel.hpp"
#include <iostream>
#include <algorithm>

void LearnerModel::initialize(const FormalTheory& theory) {
    for (const auto& pair : theory.concepts) {
        mastery[pair.first] = 0.0;
        coverage[pair.first] = 0;
    }
    errorPatterns.clear();
}

void LearnerModel::update(const std::vector<std::string>& assessedConcepts, bool isCorrect, const std::string& detectedError) {
    for (const std::string& c : assessedConcepts) {
        // Coverage(c) = Coverage(c) + 1
        coverage[c] += 1;
        
        if (isCorrect) {
            // Mastery(c) = Mastery(c) + α
            mastery[c] = std::min(1.0, mastery[c] + alpha);
        } else {
            // Penalize slightly to simulate knowledge decay on failure
            mastery[c] = std::max(0.0, mastery[c] - (alpha / 2.0));
        }
    }

    if (!detectedError.empty()) {
        errorPatterns[detectedError]++;
    } else if (isCorrect) {
        // A clean correct answer is evidence a prior misconception no
        // longer persists. Let old error-pattern counts decay so a
        // student who has since learned the material isn't permanently
        // blocked from certification (isCertified() requires every
        // error pattern's frequency to stay <= 2) by mistakes made
        // earlier in the session.
        for (auto& err : errorPatterns) {
            if (err.second > 0) err.second--;
        }
    }
}

std::vector<std::string> LearnerModel::getWeakConcepts(const FormalTheory& theory) const {
    std::vector<std::string> weak;
    for (const auto& pair : mastery) {
        if (!theory.isAssessable(pair.first)) continue;
        if (pair.second < theta) {
            weak.push_back(pair.first);
        }
    }
    return weak;
}

std::vector<std::string> LearnerModel::getUncoveredConcepts(const FormalTheory& theory) const {
    std::vector<std::string> uncovered;
    for (const auto& pair : coverage) {
        if (!theory.isAssessable(pair.first)) continue;
        if (pair.second == 0) {
            uncovered.push_back(pair.first);
        }
    }
    return uncovered;
}

bool LearnerModel::isCertified(const FormalTheory& theory) const {
    for (const auto& pair : theory.concepts) {
        const std::string& c = pair.first;
        if (!pair.second.assessable) continue; // Phase 2: properties/rules
        if (mastery.at(c) < theta || coverage.at(c) < k) {
            return false;
        }
    }
    // Also ensuring Frequency(e) < epsilon (simplified as <= 2 occurrences)
    for (const auto& err : errorPatterns) {
        if (err.second > 2) return false; 
    }
    return true;
}

void LearnerModel::printReport(const FormalTheory& theory) const {
    std::cout << "\n================ LEARNER MODEL STATE ================\n";
    for (const auto& pair : theory.concepts) {
        const std::string& c = pair.first;
        std::cout << c << " -> Mastery: " << mastery.at(c) 
                  << " | Coverage: " << coverage.at(c)
                  << (pair.second.assessable ? "" : "  [not yet assessed]") << "\n";
    }
    if (!errorPatterns.empty()) {
        std::cout << "--- Detected Error Patterns ---\n";
        for (const auto& err : errorPatterns) {
            std::cout << err.first << " : " << err.second << " occurrences\n";
        }
    }
    std::cout << "=====================================================\n";
}