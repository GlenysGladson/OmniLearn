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

// Bayesian Knowledge Tracing parameters for one concept.
//   pInit  : P(student already knows the concept before any question)
//   pLearn : P(student learns the concept after one more question)
//   pSlip  : P(wrong answer | student knows the concept)
//   pGuess : P(right answer | student does NOT know the concept)
// These defaults are NOT fitted to data. They are reasonable starting
// values. A concept may override them in YAML under `bkt:`.
struct BktParams {
    double pInit  = 0.10;
    double pLearn = 0.15;
    double pSlip  = 0.10;
    double pGuess = 0.10;
};

struct Concept {
    std::string name;
    ConceptCategory category;
    bool assessable = true;
    BktParams bkt;
};

// One variable used inside a template. `sort` is the Z3/SMT-LIB2 sort this
// variable is declared with -- this, not any hardcoded operator list, is
// what determines what a template is allowed to say about it.
// Supported sorts: "Int", "Real", "Bool", "BitVec" (requires `width`).
struct VariableDef {
    std::string name;
    std::string sort;
    unsigned width = 6; // only meaningful when sort == "BitVec"
};

// A misconception is itself just another SMT-LIB2 expression: "what would
// the answer be if a student made this specific mistake". `name` is the
// stable identifier used to count error patterns (per concept), so two
// templates that expose the same mistake should use the same `name`.
// `description` is only the text shown to the student.
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

    // Bare boolean SMT-LIB2 expressions. Each is wrapped as "(assert ...)".
    std::vector<std::string> constraints;

    // Bare SMT-LIB2 expression evaluated against the solved instance.
    std::string correctAnswerExpr;

    // "Int", "Real", "Bool", or "Set" (a decoded BitVec).
    std::string answerType;

    std::vector<MisconceptionDef> misconceptions;

    // Optional. 0.0 = easy, 1.0 = hard. Default 0.5 means "average".
    // Used to adjust BKT guess/slip and to choose between templates.
    double difficulty = 0.5;
};

class FormalTheory {
public:
    std::string theoryName;
    std::map<std::string, Concept> concepts;

    // prerequisite -> list of dependents
    std::map<std::string, std::vector<std::string>> dependencyGraph;
    std::vector<DynamicTemplate> templates;

    std::vector<std::string> sourceTheories;

    FormalTheory() = default;
    FormalTheory(const std::string& name) : theoryName(name) {}

    void addConcept(const std::string& name, ConceptCategory category, bool assessable = true,
                    const BktParams& bkt = BktParams()) {
        Concept c;
        c.name = name;
        c.category = category;
        c.assessable = assessable;
        c.bkt = bkt;
        concepts[name] = c;
    }

    void addDependency(const std::string& prereq, const std::string& dependent) {
        dependencyGraph[prereq].push_back(dependent);
    }

    bool isAssessable(const std::string& name) const {
        auto it = concepts.find(name);
        return it != concepts.end() && it->second.assessable;
    }

    // Direct prerequisites of `concept` (edges prereq -> concept).
    std::vector<std::string> prerequisitesOf(const std::string& concept) const;

    // Assessable prerequisites that must be mastered before `concept` is
    // asked. A non-assessable prerequisite is never asked about, so the
    // search passes through it to ITS prerequisites. Unknown concept names
    // are ignored.
    std::vector<std::string> assessablePrerequisites(const std::string& concept) const;

    // Returns one dependency cycle as a path (first == last), or an empty
    // vector if the graph is acyclic.
    std::vector<std::string> findDependencyCycle() const;

    // Folds another theory's concepts, dependency edges, and templates into
    // this one. Concept-name collisions keep the first definition and warn.
    void merge(const FormalTheory& other);

    // Warns for every assessable concept that no template targets.
    void warnOnUnreachableConcepts() const;
};

#endif // THEORY_HPP
