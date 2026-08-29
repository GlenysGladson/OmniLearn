#include "TemplateEngine.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace {
    const int kUniverseSize = 6;

    void applyRandomSeed(z3::config& cfg) {
        std::string seed = std::to_string(rand() % 10000);
        z3::set_param("sat.random_seed", seed.c_str());
        z3::set_param("smt.random_seed", seed.c_str());
    }

    // Injects a random constraint to force Z3 to find different models
    void injectRandomness(z3::context& c, z3::solver& s, z3::expr& A, int uSize) {
        int randomBitIndex = rand() % uSize;
        unsigned mask = 1u << randomBitIndex;
        z3::expr bit = c.bv_val(mask, uSize);

        // 50% chance to force a random element IN, 50% chance to force it OUT
        if (rand() % 2 == 0) {
            s.add((A & bit) != 0);
        } else {
            s.add((A & bit) == 0);
        }
    }

    // Fixed 3-decimal formatting, used for probability values.
    std::string formatDecimal(double val) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << val;
        return oss.str();
    }
}

std::vector<int> TemplateEngine::decodeBitVector(unsigned val, int universeSize) {
    std::vector<int> result;
    for (int i = 0; i < universeSize; ++i) {
        if ((val >> i) & 1) {
            result.push_back(i + 1);
        }
    }
    return result;
}

std::string TemplateEngine::makeSignature(const GeneratedQuestion& q) const {
    std::ostringstream oss;
    oss << q.questionText;
    for (const auto& kv : q.setGivens) {          // std::map -> sorted, deterministic
        oss << "|" << kv.first << "=";
        for (int v : kv.second) oss << v << ",";
    }
    for (const auto& kv : q.probGivens) {
        oss << "|" << kv.first << "=" << kv.second;
    }
    return oss.str();
}

bool TemplateEngine::registerIfUnique(const GeneratedQuestion& q) {
    std::string sig = makeSignature(q);
    if (usedSignatures.count(sig)) return false;
    usedSignatures.insert(sig);
    return true;
}

GeneratedQuestion TemplateEngine::selectAndInitializeTemplate(const LearnerModel& learner, const FormalTheory& theory) {
    std::vector<std::string> weak = learner.getWeakConcepts(theory);
    std::vector<std::string> uncov = learner.getUncoveredConcepts(theory);

    auto needsConcept = [&](const std::string& name) {
        return std::find(weak.begin(), weak.end(), name) != weak.end() ||
               std::find(uncov.begin(), uncov.end(), name) != uncov.end();
    };

    // Priority follows the dependency graph: assess the most
    // foundational still-weak/uncovered concept first.
    if (needsConcept("Element") || needsConcept("Membership")) {
        return generateMembership();
    }
    if (needsConcept("Subset")) {
        return generateSubset();
    }
    if (needsConcept("Complement")) {
        return generateComplement();
    }
    if (needsConcept("Difference") || needsConcept("Intersection")) {
        return generateIntersectionDifference();
    }
    
    // Probability extension templates prioritized by the dependency graph
    if (theory.concepts.count("ProbabilityComplement") && needsConcept("ProbabilityComplement")) {
        return generateProbabilityComplement();
    }
    if (theory.concepts.count("InclusionExclusion") && 
        (needsConcept("InclusionExclusion") || needsConcept("Probability"))) {
        return generateProbabilityInclusionExclusion();
    }
    if (theory.concepts.count("ConditionalProbability") && needsConcept("ConditionalProbability")) {
        return generateConditionalProbability();
    }

    // Covers Union and Set; also the fallback once everything else is strong.
    return generateUnion();
}

GeneratedQuestion TemplateEngine::generateIntersectionDifference() {
    GeneratedQuestion last;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        z3::config cfg;
        applyRandomSeed(cfg);
        z3::context c(cfg);

        int uSize = kUniverseSize;

        z3::expr A = c.bv_const("A", uSize);
        z3::expr B = c.bv_const("B", uSize);
        z3::expr C = c.bv_const("C", uSize);
        z3::solver solver(c);

        solver.add(A != 0 && B != 0 && C != 0);
        z3::expr interAB = A & B;
        solver.add(interAB != 0);
        z3::expr resultExpr = interAB & (~C);
        solver.add(resultExpr != 0);

        solver.add(A != B);
        solver.add(B != C);

        injectRandomness(c, solver, A, uSize);

        if (solver.check() == z3::sat) {
            z3::model m = solver.get_model();
            GeneratedQuestion q;
            q.questionText = "(A \u2229 B) - C";
            q.setGivens["A"] = decodeBitVector(m.eval(A).get_numeral_int(), uSize);
            q.setGivens["B"] = decodeBitVector(m.eval(B).get_numeral_int(), uSize);
            q.setGivens["C"] = decodeBitVector(m.eval(C).get_numeral_int(), uSize);
            q.correctAnswer = decodeBitVector(m.eval(resultExpr).get_numeral_int(), uSize);
            q.targetConcepts = {"Intersection", "Difference", "Set"};
            last = q;
            if (registerIfUnique(q)) return q;
            continue; // duplicate of an earlier question this session -- retry
        }
        throw std::runtime_error("SMT Solver failed on Intersection/Difference template.");
    }
    return last;
}

GeneratedQuestion TemplateEngine::generateUnion() {
    GeneratedQuestion last;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        z3::config cfg;
        applyRandomSeed(cfg);
        z3::context c(cfg);

        int uSize = kUniverseSize;

        z3::expr A = c.bv_const("A", uSize);
        z3::expr B = c.bv_const("B", uSize);
        z3::solver solver(c);

        solver.add(A != 0 && B != 0);
        solver.add((A & B) == 0);
        z3::expr resultExpr = A | B;

        injectRandomness(c, solver, A, uSize);

        if (solver.check() == z3::sat) {
            z3::model m = solver.get_model();
            GeneratedQuestion q;
            q.questionText = "A \u222A B";
            q.setGivens["A"] = decodeBitVector(m.eval(A).get_numeral_int(), uSize);
            q.setGivens["B"] = decodeBitVector(m.eval(B).get_numeral_int(), uSize);
            q.correctAnswer = decodeBitVector(m.eval(resultExpr).get_numeral_int(), uSize);
            q.targetConcepts = {"Union", "Set"};
            last = q;
            if (registerIfUnique(q)) return q;
            continue;
        }
        throw std::runtime_error("SMT Solver failed on Union template.");
    }
    return last;
}

GeneratedQuestion TemplateEngine::generateMembership() {
    GeneratedQuestion last;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        z3::config cfg;
        applyRandomSeed(cfg);
        z3::context c(cfg);

        int uSize = kUniverseSize;

        z3::expr A = c.bv_const("A", uSize);
        z3::solver solver(c);

        solver.add(A != 0);
        solver.add(A != c.bv_val((unsigned)((1u << uSize) - 1), uSize));

        injectRandomness(c, solver, A, uSize);

        if (solver.check() == z3::sat) {
            z3::model m = solver.get_model();
            GeneratedQuestion q;
            q.questionText = "List every element x such that x \u2208 A (universe = {1..6})";
            q.setGivens["A"] = decodeBitVector(m.eval(A).get_numeral_int(), uSize);
            q.correctAnswer = q.setGivens["A"];
            q.targetConcepts = {"Set", "Element", "Membership"};
            last = q;
            if (registerIfUnique(q)) return q;
            continue;
        }
        throw std::runtime_error("SMT Solver failed on Membership template.");
    }
    return last;
}

GeneratedQuestion TemplateEngine::generateSubset() {
    GeneratedQuestion last;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        z3::config cfg;
        applyRandomSeed(cfg);
        z3::context c(cfg);

        int uSize = kUniverseSize;

        z3::expr A = c.bv_const("A", uSize);
        z3::expr B = c.bv_const("B", uSize);
        z3::solver solver(c);

        solver.add(A != 0 && B != 0);
        solver.add((A & B) == A);
        solver.add(A != B);
        z3::expr resultExpr = B & (~A);
        solver.add(resultExpr != 0);

        injectRandomness(c, solver, A, uSize);

        if (solver.check() == z3::sat) {
            z3::model m = solver.get_model();
            GeneratedQuestion q;
            q.questionText = "Given A \u2286 B, compute B - A";
            q.setGivens["A"] = decodeBitVector(m.eval(A).get_numeral_int(), uSize);
            q.setGivens["B"] = decodeBitVector(m.eval(B).get_numeral_int(), uSize);
            q.correctAnswer = decodeBitVector(m.eval(resultExpr).get_numeral_int(), uSize);
            q.targetConcepts = {"Subset", "Difference", "Set"};
            last = q;
            if (registerIfUnique(q)) return q;
            continue;
        }
        throw std::runtime_error("SMT Solver failed on Subset template.");
    }
    return last;
}

GeneratedQuestion TemplateEngine::generateComplement() {
    GeneratedQuestion last;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        z3::config cfg;
        applyRandomSeed(cfg);
        z3::context c(cfg);

        int uSize = kUniverseSize;

        z3::expr A = c.bv_const("A", uSize);
        z3::solver solver(c);

        z3::expr full = c.bv_val((unsigned)((1u << uSize) - 1), uSize);
        solver.add(A != 0);
        solver.add(A != full);
        z3::expr resultExpr = full & (~A);

        injectRandomness(c, solver, A, uSize);

        if (solver.check() == z3::sat) {
            z3::model m = solver.get_model();
            GeneratedQuestion q;
            q.questionText = "Complement of A w.r.t. the universe {1..6}";
            q.setGivens["A"] = decodeBitVector(m.eval(A).get_numeral_int(), uSize);
            q.correctAnswer = decodeBitVector(m.eval(resultExpr).get_numeral_int(), uSize);
            q.targetConcepts = {"Complement", "Set"};
            last = q;
            if (registerIfUnique(q)) return q;
            continue;
        }
        throw std::runtime_error("SMT Solver failed on Complement template.");
    }
    return last;
}

// Generates valid, non-trivial probability parameters purely via Z3 integer constraints
// using a discrete sample space denominator (N = 20, resolution 0.05).
GeneratedQuestion TemplateEngine::generateProbabilityInclusionExclusion() {
    GeneratedQuestion last;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        z3::config cfg;
        applyRandomSeed(cfg);
        z3::context c(cfg);
        z3::solver s(c);

        const int N = 20; // Universe size (each step is 0.05)

        z3::expr countA = c.int_const("countA");
        z3::expr countB = c.int_const("countB");
        z3::expr countInter = c.int_const("countInter");
        z3::expr countUnion = c.int_const("countUnion");

        // Axiom: Inclusion-Exclusion formula
        s.add(countUnion == countA + countB - countInter);

        // Axiom: Probabilities bounded within [0, 1]
        s.add(countUnion <= N);
        s.add(countA >= 3 && countA <= N - 3);
        s.add(countB >= 3 && countB <= N - 3);

        // Non-triviality constraints (strictly overlapping, not subsets/disjoint)
        s.add(countInter >= 2);
        s.add(countInter < countA && countInter < countB);

        // Random guidance to explore different valid models
        int targetA = 4 + (rand() % (N - 7));
        s.add(countA == targetA);

        if (s.check() == z3::sat) {
            z3::model m = s.get_model();
            double pA = m.eval(countA).get_numeral_int() / static_cast<double>(N);
            double pB = m.eval(countB).get_numeral_int() / static_cast<double>(N);
            double pAandB = m.eval(countInter).get_numeral_int() / static_cast<double>(N);
            double pAorB = m.eval(countUnion).get_numeral_int() / static_cast<double>(N);

            GeneratedQuestion q;
            q.format = AnswerFormat::PROBABILITY_ANSWER;
            q.questionText = "Find P(A \u222A B)";
            q.probGivens["P(A)"] = formatDecimal(pA);
            q.probGivens["P(B)"] = formatDecimal(pB);
            q.probGivens["P(A \u2229 B)"] = formatDecimal(pAandB);
            q.expectedProbAnswer = formatDecimal(pAorB);
            q.targetConcepts = {"Probability", "Union", "InclusionExclusion"};

            last = q;
            if (registerIfUnique(q)) return q;
            continue;
        }
        throw std::runtime_error("SMT Solver failed on Probability template.");
    }
    return last;
}

// Probability Complement: P(A') = 1 - P(A)
GeneratedQuestion TemplateEngine::generateProbabilityComplement() {
    GeneratedQuestion last;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        z3::config cfg;
        applyRandomSeed(cfg);
        z3::context c(cfg);
        z3::solver s(c);

        const int N = 20;
        z3::expr countA = c.int_const("countA");
        z3::expr countComp = c.int_const("countComp");

        s.add(countA + countComp == N);
        s.add(countA >= 2 && countA <= N - 2);

        int targetA = 3 + (rand() % (N - 5));
        s.add(countA == targetA);

        if (s.check() == z3::sat) {
            z3::model m = s.get_model();
            double pA = m.eval(countA).get_numeral_int() / static_cast<double>(N);
            double pComp = m.eval(countComp).get_numeral_int() / static_cast<double>(N);

            GeneratedQuestion q;
            q.format = AnswerFormat::PROBABILITY_ANSWER;
            q.questionText = "Find P(A\u1D9C) given P(A)";
            q.probGivens["P(A)"] = formatDecimal(pA);
            q.expectedProbAnswer = formatDecimal(pComp);
            q.targetConcepts = {"Probability", "Complement", "ProbabilityComplement"};

            last = q;
            if (registerIfUnique(q)) return q;
            continue;
        }
        throw std::runtime_error("SMT Solver failed on Probability Complement template.");
    }
    return last;
}

// Conditional Probability: P(A | B) = P(A ∩ B) / P(B)
GeneratedQuestion TemplateEngine::generateConditionalProbability() {
    GeneratedQuestion last;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        z3::config cfg;
        applyRandomSeed(cfg);
        z3::context c(cfg);
        z3::solver s(c);

        const int N = 20;
        z3::expr countB = c.int_const("countB");
        z3::expr countInter = c.int_const("countInter");

        // P(A ∩ B) <= P(B)
        s.add(countInter >= 2);
        s.add(countInter < countB);
        s.add(countB <= N);

        // Constrain countB to divisors of 20 or 100 for clean terminating decimals
        s.add(countB == 4 || countB == 5 || countB == 10 || countB == 20);

        // Random exploration
        int options[] = {4, 5, 10, 20};
        int chosenB = options[rand() % 4];
        s.add(countB == chosenB);

        if (s.check() == z3::sat) {
            z3::model m = s.get_model();
            int bVal = m.eval(countB).get_numeral_int();
            int interVal = m.eval(countInter).get_numeral_int();

            double pB = bVal / static_cast<double>(N);
            double pInter = interVal / static_cast<double>(N);
            double pCond = static_cast<double>(interVal) / static_cast<double>(bVal);

            GeneratedQuestion q;
            q.format = AnswerFormat::PROBABILITY_ANSWER;
            q.questionText = "Find P(A | B)";
            q.probGivens["P(B)"] = formatDecimal(pB);
            q.probGivens["P(A \u2229 B)"] = formatDecimal(pInter);
            q.expectedProbAnswer = formatDecimal(pCond);
            q.targetConcepts = {"Probability", "Intersection", "ConditionalProbability"};

            last = q;
            if (registerIfUnique(q)) return q;
            continue;
        }
        throw std::runtime_error("SMT Solver failed on Conditional Probability template.");
    }
    return last;
}