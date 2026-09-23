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
        coverage[c] += 1;
        if (isCorrect) {
            mastery[c] = std::min(1.0, mastery[c] + alpha);
        } else {
            mastery[c] = std::max(0.0, mastery[c] - (alpha / 2.0));
        }
    }

    if (!detectedError.empty()) {
        errorPatterns[detectedError]++;
    } else if (isCorrect) {
        for (auto& err : errorPatterns) {
            if (err.second > 0) err.second--;
        }
    }
}

std::vector<std::string> LearnerModel::getWeakConcepts(const FormalTheory& theory) const {
    std::vector<std::string> weak;
    for (const auto& pair : mastery) {
        if (!theory.isAssessable(pair.first)) continue;
        if (pair.second < theta) weak.push_back(pair.first);
    }
    return weak;
}

std::vector<std::string> LearnerModel::getUncoveredConcepts(const FormalTheory& theory) const {
    std::vector<std::string> uncovered;
    for (const auto& pair : coverage) {
        if (!theory.isAssessable(pair.first)) continue;
        if (pair.second == 0) uncovered.push_back(pair.first);
    }
    return uncovered;
}

bool LearnerModel::isCertified(const FormalTheory& theory) const {
    for (const auto& pair : theory.concepts) {
        const std::string& c = pair.first;
        if (!pair.second.assessable) continue;
        if (mastery.at(c) < theta || coverage.at(c) < k) return false;
    }
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
                  << " | Coverage: " << coverage.at(c) << "\n";
    }
    std::cout << "=====================================================\n";
}