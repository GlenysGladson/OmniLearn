#ifndef GENERIC_EXPR_ENGINE_HPP
#define GENERIC_EXPR_ENGINE_HPP

#include <z3++.h>
#include <string>
#include <vector>
#include <set>
#include <stdexcept>
#include "Theory.hpp"

// A sort-tagged concrete value: a generated variable's value, a template's
// correct answer, a misconception's "wrong if you made this mistake"
// value, or the student's parsed answer -- all represented the same way so
// comparison works identically across every theory.
struct ConcreteValue {
    std::string sort; // "Int", "Real", "Bool", "Set"

    long long intValue = 0;
    double realValue = 0.0;
    bool boolValue = false;
    std::vector<int> setValue; // sorted, 1-based bit positions (for "Set")

    std::string toDisplayString() const;
};

bool concreteValuesEqual(const ConcreteValue& a, const ConcreteValue& b);

// One fully generated, ready-to-present question instance.
struct GeneratedQuestion {
    std::string templateId;
    std::string questionText;
    std::string answerType; // "Int", "Real", "Bool", "Set"
    std::vector<std::string> targetConcepts;
    double difficulty = 0.5;
    ConcreteValue correctValue;

    struct MisconceptionOutcome {
        std::string name;        // stable id used for error-pattern counting
        std::string description; // text shown to the student
        ConcreteValue value;
    };
    // Only misconceptions that give a value different from the correct
    // answer and from every earlier misconception, so a match is meaningful.
    std::vector<MisconceptionOutcome> misconceptions;

    // Misconceptions left out of THIS instance, with the reason.
    struct DroppedMisconception {
        std::string name;
        std::string reason;
    };
    std::vector<DroppedMisconception> droppedMisconceptions;

    // True when the question text had already been asked and no unused
    // assignment could be found (so the caller may want to warn).
    bool isRepeat = false;
};

// Thrown for anything that keeps a template from being solved: an
// unsupported construct, a Z3 error, an unsatisfiable or "unknown"
// constraint set, a bad declaration, or a failed answer re-check.
struct TemplateError : public std::runtime_error {
    explicit TemplateError(const std::string& msg) : std::runtime_error(msg) {}
};

class GenericExprEngine {
public:
    // Rejects "forall" / "exists". Throws TemplateError if found.
    // (Nonlinear integer arithmetic is not pre-detected; Z3 may answer
    // "unknown", which surfaces as a TemplateError from generateInstance.)
    static void rejectUnsupportedConstructs(const std::string& raw_expr);

    // Builds, solves and fully evaluates one instance of `tmpl`:
    //  1. solves the constraints, picking a random-looking assignment
    //     (random value per variable inside its feasible range, falling
    //     back to Z3's own model when a pick is infeasible);
    //  2. renders the question text;
    //  3. evaluates correct_answer_expr and each misconception;
    //  4. re-computes every answer in a SEPARATE Z3 context from the printed
    //     variable values and throws if the two results differ;
    //  5. retries (up to 25 times, with blocking clauses) when the rendered
    //     text was already asked (`excludeQuestionTexts`) or when a
    //     misconception collides with the correct answer.
    // Every z3::exception is converted to TemplateError: z3::exception does
    // not derive from std::exception, so callers could not catch it.
    static GeneratedQuestion generateInstance(const DynamicTemplate& tmpl,
                                               const std::set<std::string>& excludeQuestionTexts = {});
};

#endif // GENERIC_EXPR_ENGINE_HPP
