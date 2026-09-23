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
// comparison works identically across every theory, not just the one it
// happened to be hand-coded for.
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
    ConcreteValue correctValue;

    struct MisconceptionOutcome {
        std::string description;
        ConcreteValue value;
    };
    std::vector<MisconceptionOutcome> misconceptions;
};

// Thrown for anything that keeps a template from being solved: an
// unsupported construct (a quantifier), a Z3 parse error, an unsatisfiable
// or "unknown" (e.g. timed-out) constraint set, or a bad declaration.
// Callers should catch this per-template at load time and skip the
// offending template rather than let it crash the whole program.
struct TemplateError : public std::runtime_error {
    explicit TemplateError(const std::string& msg) : std::runtime_error(msg) {}
};

class GenericExprEngine {
public:
    // Scans raw_expr for constructs outside the guaranteed-decidable,
    // quantifier-free fragment this engine supports. Currently rejects
    // "forall" / "exists". Throws TemplateError if found. (Note: this does
    // NOT detect nonlinear integer arithmetic, e.g. "(* a b)" for two
    // variables -- Z3 handles that with incomplete heuristics rather than
    // a true decision procedure, and can legitimately return "unknown" on
    // it. That case surfaces naturally as a TemplateError from
    // generateInstance below, via the solver's own "unknown" result,
    // rather than being pre-detected here.)
    static void rejectUnsupportedConstructs(const std::string& raw_expr);

    // Builds, solves, and fully evaluates one instance of `tmpl`: declares
    // its variables, asserts its constraints, solves (with a timeout),
    // evaluates correct_answer_expr and every misconception expr against
    // the resulting model, and renders question_text (auto-appending a
    // "Given: ..." line for any variable the template text never
    // mentions, so a value is never asked about without ever being shown).
    // Throws TemplateError on any failure.
    //
    // `excludeQuestionTexts` is the set of already-used rendered question
    // texts for this template (from the caller's own dedup history). Z3
    // is deterministic for a fixed, lightly-constrained problem -- it does
    // NOT explore different satisfying assignments on repeated calls just
    // because a random seed changed (verified empirically: a template
    // with only a "distinct A B" constraint returned the exact same model
    // every time regardless of seeding). So if the first solve lands on
    // an excluded assignment, this adds a blocking clause ("at least one
    // variable must differ") and re-solves in the SAME solver session,
    // repeating until a fresh assignment is found, the constraints become
    // unsatisfiable (no more distinct assignments exist), or a retry cap
    // is hit -- whichever comes first.
    static GeneratedQuestion generateInstance(const DynamicTemplate& tmpl,
                                               const std::set<std::string>& excludeQuestionTexts = {});
};

#endif // GENERIC_EXPR_ENGINE_HPP