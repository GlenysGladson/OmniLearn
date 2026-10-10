#ifndef LEARNER_MODEL_HPP
#define LEARNER_MODEL_HPP

#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "Theory.hpp"

// Certification and mastery rules. Defaults were chosen with a small
// simulation (see the project notes); they are not fitted to real students.
struct LearnerConfig {
    double masteryThreshold = 0.95;   // P(known) needed for a concept
    int minAttempts = 3;              // minimum answers per concept
    int accuracyWindow = 10;          // look at the last N answers ...
    double minRecentAccuracy = 0.80;  // ... and require this share correct
    int errorResolveStreak = 2;       // correct answers in a row that resolve an error
};

// Everything the model needs to know about one answered question.
struct Observation {
    std::string templateId;
    std::string questionText;
    std::vector<std::string> targets;
    std::string answerType;     // "Int", "Real", "Bool", "Set"
    double difficulty = 0.5;
    bool correct = false;
    std::string errorName;      // misconception name, empty if none matched
    bool isRecheck = false;     // final re-check question
};

struct ConceptState {
    BktParams bkt;
    double pKnown = 0.10;
    int attempts = 0;
    int correct = 0;
    bool recheckPassed = false;
    std::deque<int> recent;     // last answers, 1 = correct, newest at the back
};

struct ErrorState {
    int occurrences = 0;
    int streakSinceLast = 0;    // correct answers in this concept since the last occurrence
    bool active = false;        // unresolved
};

class LearnerModel {
private:
    LearnerConfig cfg;
    std::map<std::string, ConceptState> concepts;
    // key = (concept, misconception name)
    std::map<std::pair<std::string, std::string>, ErrorState> errors;
    std::map<std::string, int> templateUses;
    std::set<std::string> askedQuestions;
    int questionsAnswered = 0;

    double recentAccuracy(const ConceptState& s) const;

public:
    LearnerModel() = default;

    const LearnerConfig& config() const { return cfg; }
    void setConfig(const LearnerConfig& c) { cfg = c; }

    // Fresh state for every concept in the theory (BKT parameters are taken
    // from the theory).
    void initialize(const FormalTheory& theory);

    // Applies one answer: BKT update for every target concept, error-pattern
    // bookkeeping, and history (asked texts, template use counts).
    void update(const Observation& obs);

    // --- queries ---
    double pKnown(const std::string& concept) const;
    int attempts(const std::string& concept) const;
    int correctCount(const std::string& concept) const;
    bool isMastered(const std::string& concept) const;      // P(known), attempts, recent accuracy
    bool hasActiveError(const std::string& concept) const;
    std::vector<std::string> activeErrors(const std::string& concept) const;
    bool needsWork(const std::string& concept) const;       // not mastered, or an unresolved error
    bool needsRecheck(const std::string& concept) const;    // mastered, clean, final check pending
    bool prerequisitesMet(const std::string& concept, const FormalTheory& theory) const;

    std::vector<std::string> getWeakConcepts(const FormalTheory& theory) const;      // asked, still needs work
    std::vector<std::string> getUncoveredConcepts(const FormalTheory& theory) const; // never asked

    bool isCertified(const FormalTheory& theory) const;
    std::vector<std::string> certificationBlockers(const FormalTheory& theory) const;

    const std::set<std::string>& asked() const { return askedQuestions; }
    int templateUseCount(const std::string& templateId) const;
    int totalAnswered() const { return questionsAnswered; }

    // Compatibility with the earlier interface.
    double getMastery(const std::string& concept) const { return pKnown(concept); }
    int getCoverage(const std::string& concept) const { return attempts(concept); }

    // State file (YAML). saveState writes to a temporary file and renames it.
    bool saveState(const std::string& path) const;
    // Overlays a saved state on a model already initialize()d from `theory`.
    // Returns false (and leaves the model untouched) if the file is missing or unreadable.
    bool loadState(const std::string& path, const FormalTheory& theory);

    void printReport(const FormalTheory& theory) const;
};

#endif // LEARNER_MODEL_HPP
