#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>

// D, O, Rel, P, R (+ Probability extension)
enum class ConceptCategory { 
    DATATYPE, 
    OPERATION, 
    RELATION, 
    PROPERTY, 
    RULE,
    PROBABILITY // quantities/laws belonging to the Probability extension
};

struct Concept {
    std::string name;
    ConceptCategory category;
    // Whether TemplateEngine can currently generate a question that
    // targets this concept. Properties (Commutativity, DeMorgan) and
    // Inference Rules are proof/justification-style concepts that need
    // a true/false or step-based answer format, which the current
    // question UI (comma-separated set answers) doesn't support yet.
    // They're kept in the concept space for completeness, but excluded
    // from mastery/certification so the assessment loop can terminate.
    bool assessable = true;
};

class FormalTheory {
public:
    std::string theoryName;
    std::map<std::string, Concept> concepts;
    std::map<std::string, std::vector<std::string>> dependencyGraph; // Dep ⊆ C × C

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

    // Initialize Set Theory 
    static FormalTheory createSetTheory() {
        FormalTheory st("Set Theory");
        
        // Data Types (D)
        st.addConcept("Set", ConceptCategory::DATATYPE);
        st.addConcept("Element", ConceptCategory::DATATYPE);
        // Boolean is used implicitly as the return type of relations
        // (membership, subset, equality). No template targets it
        // directly yet, so it's marked non-assessable for now.
        st.addConcept("Boolean", ConceptCategory::DATATYPE, false);
        
        // Operations (O)
        st.addConcept("Union", ConceptCategory::OPERATION);
        st.addConcept("Intersection", ConceptCategory::OPERATION);
        st.addConcept("Difference", ConceptCategory::OPERATION);
        st.addConcept("Complement", ConceptCategory::OPERATION);
        
        // Relations (Rel)
        st.addConcept("Membership", ConceptCategory::RELATION);
        st.addConcept("Subset", ConceptCategory::RELATION);
        // Equality needs a true/false answer format -- not supported
        // by the current list-based answer UI. Non-assessable for now.
        st.addConcept("Equality", ConceptCategory::RELATION, false);

        // Properties (P) -- proof-style, no template yet
        st.addConcept("Commutativity", ConceptCategory::PROPERTY, false);
        st.addConcept("DeMorgan", ConceptCategory::PROPERTY, false);

        // Inference Rules (R) -- proof-style, no template yet
        st.addConcept("UnionIntroduction", ConceptCategory::RULE, false);
        st.addConcept("SubsetTransitivity", ConceptCategory::RULE, false);

        // Dependency Graph (Dep), following the reference notes:
        // Set -> Element -> Membership -> Subset -> Union -> Intersection
        // -> Difference -> Complement -> DeMorgan
        st.addDependency("Set", "Element");
        st.addDependency("Element", "Membership");
        st.addDependency("Membership", "Subset");
        st.addDependency("Membership", "Union");
        st.addDependency("Union", "Intersection");
        st.addDependency("Intersection", "Difference");
        st.addDependency("Difference", "Complement");
        st.addDependency("Complement", "DeMorgan");
        
        return st;
    }

    // Set Theory + Probability. Builds on createSetTheory() and adds the
    // Probability concepts (P(A), P(A^c), P(A ∪ B), P(A | B)), which
    // depend on Union/Intersection/Complement already being understood.
    static FormalTheory createFullTheory() {
        FormalTheory st = createSetTheory();
        st.theoryName = "Set Theory & Probability";

        // Probability (PROBABILITY)
        st.addConcept("Probability", ConceptCategory::PROBABILITY);
        st.addConcept("ProbabilityComplement", ConceptCategory::PROBABILITY);
        st.addConcept("InclusionExclusion", ConceptCategory::PROBABILITY);
        st.addConcept("ConditionalProbability", ConceptCategory::PROBABILITY);

        // Dependency graph chaining probability concepts:
        // Complement -> Probability -> ProbabilityComplement -> InclusionExclusion -> ConditionalProbability
        st.addDependency("Complement", "Probability");
        st.addDependency("Probability", "ProbabilityComplement");
        st.addDependency("ProbabilityComplement", "InclusionExclusion");
        st.addDependency("InclusionExclusion", "ConditionalProbability");

        return st;
    }
};