#ifndef THEORY_HPP
#define THEORY_HPP

#include <string>
#include <vector>
#include <map>

enum class ConceptCategory {
    DATATYPE,
    OPERATION,
    RELATION,
    PROPERTY,
    RULE,
    PROBABILITY,
    GRAPH_THEORY
};

struct Concept {
    std::string name;
    ConceptCategory category;
    bool assessable = true;
};

// One variable used inside a template. `sort` is the Z3/SMT-LIB2 sort this
// variable is declared with -- this, not any hardcoded operator list, is
// what determines what a template is allowed to say about it.
// Supported sorts: "Int", "Real", "Bool", "BitVec" (requires `width`).
struct VariableDef {
    std::string name;
    std::string sort;
    unsigned width = 6; // only meaningful when sort == "BitVec"; default of
                        // 6 kept for backward compatibility with the
                        // original fixed "universe size" convention.
};

// A misconception is itself just another SMT-LIB2 expression: "what would
// the answer be if a student made this specific mistake". It's evaluated
// with exactly the same machinery as correct_answer_expr, against the same
// concrete variable values, and compared to the student's actual answer.
// This is what makes misconception detection generic across every theory
// instead of hand-coded per theory in C++.
struct MisconceptionDef {
    std::string name;
    std::string expr;
    std::string description;
};

struct DynamicTemplate {
    std::string id;
    std::vector<std::string> targetConcepts;
    std::string questionText;
    std::vector<VariableDef> variables;

    // Bare boolean SMT-LIB2 expressions, e.g. "(distinct A B)" or
    // "(> a 0)". Each is auto-wrapped as "(assert ...)" when solved --
    // authors do not write "assert" themselves.
    std::vector<std::string> constraints;

    // Bare SMT-LIB2 expression evaluated against the solved instance,
    // e.g. "(bvor A B)" or "(+ a b)".
    std::string correctAnswerExpr;

    // How to interpret & compare correctAnswerExpr's result and the
    // student's typed answer: "Int", "Real", "Bool", or "Set" (a decoded
    // BitVec, entered as comma-separated integers).
    std::string answerType;

    std::vector<MisconceptionDef> misconceptions;
};

class FormalTheory {
public:
    std::string theoryName;
    std::map<std::string, Concept> concepts;
    std::map<std::string, std::vector<std::string>> dependencyGraph;
    std::vector<DynamicTemplate> templates;

    // Names of every theory file that has been folded into this object via
    // merge().
    std::vector<std::string> sourceTheories;

    FormalTheory() = default;
    FormalTheory(const std::string& name) : theoryName(name) {}

    void addConcept(const std::string& name, ConceptCategory category, bool assessable = true) {
        concepts[name] = {name, category, assessable};
    }

    void addDependency(const std::string& prereq, const std::string& dependent) {
        dependencyGraph[prereq].push_back(dependent);
    }

    bool isAssessable(const std::string& name) const {
        auto it = concepts.find(name);
        return it != concepts.end() && it->second.assessable;
    }

    // Folds another theory's concepts, dependency edges, and templates into
    // this one. Concept-name collisions are NOT overwritten -- the first
    // definition wins, and a warning is printed to stderr so the collision
    // is visible instead of silently resolved.
    void merge(const FormalTheory& other);

    // Prints a non-fatal warning (to stderr) for every assessable concept
    // that no (successfully validated) template targets. Such a concept
    // can never accumulate coverage, so certification could never
    // complete for it. This generalizes a real bug found in an earlier
    // version of set_theory.yaml (a "Difference" concept with no
    // template) into a standing startup check.
    void warnOnUnreachableConcepts() const;
};

#endif // THEORY_HPP