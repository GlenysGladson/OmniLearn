#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include "LearnerModel.hpp"
#include "TemplateEngine.hpp"
#include "Theory.hpp"

namespace {
    const double kProbTolerance = 0.01; // acceptable rounding slack on a decimal answer
    const int kMaxQuestions = 200;      // safety valve: assessment should never hang

    void printDivider() {
        std::cout << "--------------------------------------------------\n";
    }
}

// Parses "1,2,3" into a sorted vector of ints.
std::vector<int> parseUserInput(const std::string& input) {
    std::vector<int> result;
    std::stringstream ss(input);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stoi(item));
        } catch (...) {}
    }
    std::sort(result.begin(), result.end());
    return result;
}

// Parses a decimal probability answer; returns false if unparseable.
bool parseProbabilityInput(const std::string& input, double& outValue) {
    try {
        size_t consumed = 0;
        outValue = std::stod(input, &consumed);
        return consumed > 0;
    } catch (...) {
        return false;
    }
}

namespace {
    std::set<int> toSet(const std::vector<int>& v) {
        return std::set<int>(v.begin(), v.end());
    }
    std::set<int> setUnion(const std::set<int>& x, const std::set<int>& y) {
        std::set<int> r(x); r.insert(y.begin(), y.end()); return r;
    }
    std::set<int> setInter(const std::set<int>& x, const std::set<int>& y) {
        std::set<int> r;
        for (int e : x) if (y.count(e)) r.insert(e);
        return r;
    }
    std::set<int> setDiff(const std::set<int>& x, const std::set<int>& y) {
        std::set<int> r;
        for (int e : x) if (!y.count(e)) r.insert(e);
        return r;
    }
}

// Compares the student's (wrong) answer against a handful of common
// operation-confusion mistakes for the givens actually used in this
// question, and returns a short label if one matches.
std::string detectSetErrorPattern(const GeneratedQuestion& q, const std::vector<int>& studentAnswer) {
    if (studentAnswer == q.correctAnswer) return "";
    if (!q.setGivens.count("A") || !q.setGivens.count("B")) return "";

    std::set<int> A = toSet(q.setGivens.at("A"));
    std::set<int> B = toSet(q.setGivens.at("B"));
    std::set<int> student = toSet(studentAnswer);
    bool hasC = q.setGivens.count("C") > 0;
    std::set<int> C = hasC ? toSet(q.setGivens.at("C")) : std::set<int>();

    if (student == setUnion(A, B)) return "Intersection->Union";
    if (student == setDiff(B, A))  return "Difference direction swapped (A-B vs B-A)";
    if (hasC) {
        if (student == setInter(A, B))          return "(A∩B)-C -> forgot to subtract C";
        if (student == setDiff(setUnion(A,B),C)) return "Intersection->Union before Difference";
    } else {
        if (student == setInter(A, B)) return "Union->Intersection";
    }
    return ""; // no recognizable pattern
}

// Same idea for the probability template: compares the (wrong) numeric
// answer against a handful of common Inclusion-Exclusion mistakes.
std::string detectProbabilityErrorPattern(const GeneratedQuestion& q, double studentAnswer) {
    auto close = [&](double v) { return std::fabs(studentAnswer - v) < kProbTolerance; };

    // 1. Inclusion-Exclusion Diagnostics: Find P(A U B)
    if (q.probGivens.count("P(A)") && q.probGivens.count("P(B)") && q.probGivens.count("P(A \u2229 B)")) {
        double pA = std::stod(q.probGivens.at("P(A)"));
        double pB = std::stod(q.probGivens.at("P(B)"));
        double pAandB = std::stod(q.probGivens.at("P(A \u2229 B)"));

        if (close(pA + pB)) return "Forgot to subtract P(A∩B) (double-counted the overlap)";
        if (close(pA * pB)) return "Multiplied P(A)*P(B) instead of using Inclusion-Exclusion";
        if (close(std::max(pA, pB))) return "Used max(P(A),P(B)) instead of the union formula";
        if (close(pA + pB - 2 * pAandB)) return "Subtracted P(A∩B) twice";
    }

    // 2. Conditional Probability Diagnostics: Find P(A | B)
    if (q.probGivens.count("P(B)") && q.probGivens.count("P(A \u2229 B)")) {
        double pB = std::stod(q.probGivens.at("P(B)"));
        double pAandB = std::stod(q.probGivens.at("P(A \u2229 B)"));

        if (close(pAandB * pB)) return "Multiplied P(A∩B)*P(B) instead of dividing P(A∩B)/P(B)";
        if (close(pB / pAandB)) return "Inverted fraction (computed P(B)/P(A∩B) instead of P(A∩B)/P(B))";
        if (close(pB - pAandB)) return "Subtracted P(B)-P(A∩B) instead of dividing";
    }

    // 3. Complement Diagnostics: Find P(A')
    if (q.probGivens.count("P(A)") && !q.probGivens.count("P(B)")) {
        double pA = std::stod(q.probGivens.at("P(A)"));
        if (close(pA)) return "Returned P(A) instead of 1 - P(A)";
    }

    return ""; // no recognizable pattern
}

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

    std::cout << "=== OmniLearn Adaptive Assessment Started ===\n";
    std::cout << "Covers: Set Theory + Probability (Inclusion-Exclusion)\n";

    FormalTheory theory = FormalTheory::createFullTheory();
    LearnerModel learner;
    learner.initialize(theory);

    TemplateEngine engine;
    int questionNumber = 1;
    int correctCount = 0;
    int setQuestionCount = 0;
    int probQuestionCount = 0;

    bool timedOut = false;
    while (!learner.isCertified(theory)) {
        if (questionNumber > kMaxQuestions) {
            timedOut = true;
            break;
        }
        std::cout << "\n--- Question " << questionNumber++ << " ---\n";

        GeneratedQuestion q = engine.selectAndInitializeTemplate(learner, theory);
        bool isCorrect = false;
        std::string detectedError;

        if (q.format == AnswerFormat::SET_ANSWER) {
            setQuestionCount++;
            std::cout << "Given the following sets:\n";
            for (const auto& given : q.setGivens) {
                std::cout << "  " << given.first << " = { ";
                for (size_t i = 0; i < given.second.size(); ++i) {
                    std::cout << given.second[i] << (i + 1 < given.second.size() ? ", " : "");
                }
                std::cout << " }\n";
            }

            std::cout << "\nEvaluate the expression: " << q.questionText << "\n";
            std::cout << "Enter your answer as a comma-separated list (e.g., 1,2,3): ";

            std::string userInputStr;
            std::getline(std::cin, userInputStr);
            std::vector<int> studentAnswer = parseUserInput(userInputStr);

            isCorrect = (studentAnswer == q.correctAnswer);
            detectedError = detectSetErrorPattern(q, studentAnswer);

            if (isCorrect) {
                std::cout << "> Correct!\n";
            } else {
                std::cout << "> Incorrect. The correct answer was: { ";
                for (size_t i = 0; i < q.correctAnswer.size(); ++i) {
                    std::cout << q.correctAnswer[i] << (i + 1 < q.correctAnswer.size() ? ", " : "");
                }
                std::cout << " }\n";
                if (!detectedError.empty()) {
                    std::cout << "  [Detected error pattern: " << detectedError << "]\n";
                }
            }
        } else { // PROBABILITY_ANSWER
            probQuestionCount++;
            std::cout << "Given:\n";
            for (const auto& given : q.probGivens) {
                std::cout << "  " << given.first << " = " << given.second << "\n";
            }

            std::cout << "\nEvaluate: " << q.questionText << "\n";
            std::cout << "Enter your answer as a decimal (e.g., 0.350): ";

            std::string userInputStr;
            std::getline(std::cin, userInputStr);
            double studentAnswer = 0.0;
            bool parsed = parseProbabilityInput(userInputStr, studentAnswer);

            double expected = std::stod(q.expectedProbAnswer);
            isCorrect = parsed && std::fabs(studentAnswer - expected) < kProbTolerance;
            detectedError = (!isCorrect && parsed) ? detectProbabilityErrorPattern(q, studentAnswer) : "";

            if (isCorrect) {
                std::cout << "> Correct!\n";
            } else {
                std::cout << "> Incorrect. The correct answer was: " << q.expectedProbAnswer << "\n";
                if (!detectedError.empty()) {
                    std::cout << "  [Detected error pattern: " << detectedError << "]\n";
                }
            }
        }

        if (isCorrect) correctCount++;
        learner.update(q.targetConcepts, isCorrect, detectedError);

        std::cout << "\n[Metrics Updated for Targeted Concepts]\n";
        for (const auto& concept : q.targetConcepts) {
            std::cout << "  * " << concept
                      << ": Mastery = " << learner.getMastery(concept)
                      << ", Coverage = " << learner.getCoverage(concept) << "\n";
        }
    }

    int totalQuestions = questionNumber - 1;

    std::cout << "\n============================================\n";
    if (timedOut) {
        std::cout << "   SESSION ENDED (question limit reached)  \n";
    } else {
        std::cout << "      MASTERY CERTIFICATION ACHIEVED!       \n";
    }
    std::cout << "============================================\n";
    learner.printReport(theory);
    learner.printFullState();

    printDivider();
    std::cout << "SESSION STATISTICS\n";
    printDivider();
    std::cout << "Total Questions Attempted : " << totalQuestions << "\n";
    std::cout << "Correct Answers           : " << correctCount << "\n";
    std::cout << "Incorrect Answers         : " << (totalQuestions - correctCount) << "\n";
    std::cout << "Set Theory Questions      : " << setQuestionCount << "\n";
    std::cout << "Probability Questions     : " << probQuestionCount << "\n";
    if (totalQuestions > 0) {
        double accuracy = 100.0 * correctCount / totalQuestions;
        std::cout << "Overall Accuracy          : " << accuracy << "%\n";
    }
    printDivider();

    if (!timedOut) {
        std::cout << "\n";
        std::cout << "   *****************************************************\n";
        std::cout << "   *              CERTIFICATE OF MASTERY              *\n";
        std::cout << "   *****************************************************\n";
        std::cout << "   * Theory     : " << theory.theoryName << "\n";
        std::cout << "   * Result     : CERTIFIED\n";
        std::cout << "   * Questions  : " << totalQuestions << " (Set Theory: " << setQuestionCount
                   << ", Probability: " << probQuestionCount << ")\n";
        std::cout << "   * All concepts reached mastery >= 0.8 with coverage >= 3,\n";
        std::cout << "   * and no error pattern occurred more than twice.\n";
        std::cout << "   *****************************************************\n";
    } else {
        std::cout << "\nNo certificate issued -- the question limit (" << kMaxQuestions
                   << ") was reached before every concept met the mastery threshold.\n"
                   << "See the per-concept mastery/coverage above for what still needs work.\n";
    }

    return 0;
}
