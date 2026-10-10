#include "Theory.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <set>

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



std::vector<std::string> FormalTheory::prerequisitesOf(const std::string& concept) const {
    std::vector<std::string> out;
    for (const auto& pair : dependencyGraph) {
        for (const auto& dep : pair.second) {
            if (dep == concept && std::find(out.begin(), out.end(), pair.first) == out.end()) {
                out.push_back(pair.first);
            }
        }
    }
    return out;
}

std::vector<std::string> FormalTheory::assessablePrerequisites(const std::string& concept) const {
    std::vector<std::string> out;
    std::set<std::string> visited;
    std::vector<std::string> stack = {concept};
    visited.insert(concept);
    while (!stack.empty()) {
        std::string cur = stack.back();
        stack.pop_back();
        for (const auto& p : prerequisitesOf(cur)) {
            if (visited.count(p)) continue;
            visited.insert(p);
            auto it = concepts.find(p);
            if (it == concepts.end()) continue;           // unknown name: ignore
            if (it->second.assessable) out.push_back(p);  // must be mastered
            else stack.push_back(p);                      // pass through
        }
    }
    return out;
}

std::vector<std::string> FormalTheory::findDependencyCycle() const {
    // DFS with colours: 0 = new, 1 = on current path, 2 = done.
    std::map<std::string, int> colour;
    std::vector<std::string> path;
    std::vector<std::string> cycle;

    std::function<bool(const std::string&)> dfs = [&](const std::string& u) -> bool {
        colour[u] = 1;
        path.push_back(u);
        auto it = dependencyGraph.find(u);
        if (it != dependencyGraph.end()) {
            for (const auto& v : it->second) {
                if (colour[v] == 1) {
                    auto start = std::find(path.begin(), path.end(), v);
                    cycle.assign(start, path.end());
                    cycle.push_back(v);
                    return true;
                }
                if (colour[v] == 0 && dfs(v)) return true;
            }
        }
        path.pop_back();
        colour[u] = 2;
        return false;
    };

    for (const auto& pair : dependencyGraph) {
        if (colour[pair.first] == 0 && dfs(pair.first)) return cycle;
    }
    return {};
}
