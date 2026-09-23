#ifndef LEARNER_MODEL_HPP
#define LEARNER_MODEL_HPP

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include "Theory.hpp"

class LearnerModel {
private:
    std::map<std::string, double> mastery;
    std::map<std::string, int> coverage;
    std::map<std::string, int> errorPatterns;

    double alpha = 0.2;
    double theta = 0.8;
    int k = 3;

public:
    LearnerModel() = default;

    void initialize(const FormalTheory& theory);
    void update(const std::vector<std::string>& assessedConcepts, bool isCorrect, const std::string& detectedError = "");

    std::vector<std::string> getWeakConcepts(const FormalTheory& theory) const;
    std::vector<std::string> getUncoveredConcepts(const FormalTheory& theory) const;

    bool isCertified(const FormalTheory& theory) const;
    void printReport(const FormalTheory& theory) const;

    double getMastery(const std::string& concept) const {
        auto it = mastery.find(concept);
        return (it != mastery.end()) ? it->second : 0.0;
    }

    int getCoverage(const std::string& concept) const {
        auto it = coverage.find(concept);
        return (it != coverage.end()) ? it->second : 0;
    }
};

#endif // LEARNER_MODEL_HPP