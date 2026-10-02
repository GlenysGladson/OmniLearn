#include "Theory.hpp"
#include <algorithm>
#include <iostream>

void FormalTheory::merge(const FormalTheory& other) {
    for (const auto& pair : other.concepts) {
        const std::string& name = pair.first;
        if (concepts.count(name)) {
            std::cerr << "[WARN] Concept '" << name << "' from theory '"
                      << other.theoryName << "' collides with a concept of the "
                      << "same name already loaded from an earlier theory file. "
                      << "Keeping the first definition; the one from '"
                      << other.theoryName << "' is ignored.\n";
            continue;
        }
        concepts[name] = pair.second;
    }

    for (const auto& pair : other.dependencyGraph) {
        auto& deps = dependencyGraph[pair.first];
        for (const auto& d : pair.second) {
            if (std::find(deps.begin(), deps.end(), d) == deps.end()) {
                deps.push_back(d);
            }
        }
    }

    for (const auto& tmpl : other.templates) {
        templates.push_back(tmpl);
    }

    sourceTheories.push_back(other.theoryName);
}

void FormalTheory::warnOnUnreachableConcepts() const {
    for (const auto& pair : concepts) {
        if (!pair.second.assessable) continue;
        bool reachable = false;
        for (const auto& tmpl : templates) {
            for (const auto& target : tmpl.targetConcepts) {
                if (target == pair.first) { reachable = true; break; }
            }
            if (reachable) break;
        }
        if (!reachable) {
            std::cerr << "[WARN] Concept '" << pair.first << "' is assessable but no "
                      << "successfully validated template targets it. Certification can "
                      << "never complete for this concept until a template is added, or "
                      << "the one that failed validation for it is fixed.\n";
        }
    }
}