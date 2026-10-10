# OmniLearn Backend Source Files

This document aggregates the source code files provided for the `OmniLearnBackend` project.

---

## `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.10)
project(OmniLearnBackend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR})

# Locate Z3
find_path(Z3_INCLUDE_DIR z3++.h HINTS /usr/include /usr/local/include)
find_library(Z3_LIBRARIES NAMES z3 HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)

if(NOT Z3_INCLUDE_DIR OR NOT Z3_LIBRARIES)
    message(FATAL_ERROR "Z3 headers or library not found! Please check your installation.")
endif()

# Locate yaml-cpp
find_package(yaml-cpp REQUIRED)

include_directories(${Z3_INCLUDE_DIR})

set(SOURCES
    ${CMAKE_SOURCE_DIR}/LearnerModel.cpp
    ${CMAKE_SOURCE_DIR}/TemplateEngine.cpp
    ${CMAKE_SOURCE_DIR}/Theory.cpp
    ${CMAKE_SOURCE_DIR}/YamlParser.cpp
    ${CMAKE_SOURCE_DIR}/GenericExprEngine.cpp
    ${CMAKE_SOURCE_DIR}/TemplateValidator.cpp
    ${CMAKE_SOURCE_DIR}/InstanceValidator.cpp
)

add_executable(omnilearn_backend ${SOURCES} ${CMAKE_SOURCE_DIR}/main.cpp)
target_link_libraries(omnilearn_backend PRIVATE ${Z3_LIBRARIES} yaml-cpp pthread)

add_executable(omnilearn_validate ${SOURCES} ${CMAKE_SOURCE_DIR}/validate_main.cpp)
target_link_libraries(omnilearn_validate PRIVATE ${Z3_LIBRARIES} yaml-cpp pthread)

add_executable(omnilearn_tests ${SOURCES} ${CMAKE_SOURCE_DIR}/tests/test_validator.cpp)
target_link_libraries(omnilearn_tests PRIVATE ${Z3_LIBRARIES} yaml-cpp pthread)

# main.cpp uses std::filesystem (directory scanning for theory YAML
# files). GCC < 9 ships this in a separate library that must be linked
# explicitly; GCC >= 9 and Clang with a recent libc++ do not need it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(omnilearn_backend PRIVATE stdc++fs)
    target_link_libraries(omnilearn_validate PRIVATE stdc++fs)
    target_link_libraries(omnilearn_tests PRIVATE stdc++fs)
endif()
```

---

## `InstanceValidator.cpp`

```cpp
#include "InstanceValidator.hpp"

using namespace std;

InstanceValidationResult InstanceValidator::validateInstance(const GeneratedQuestion& q) {
    InstanceValidationResult res;
    res.isValid = true;
    auto addReason = [&](const string& r) { res.isValid = false; res.reasons.push_back(r); };

    // 1. Text sanity checks
    if (q.questionText.empty()) {
        addReason("Question text is completely empty.");
    }
    if (q.questionText.find('{') != string::npos || q.questionText.find('}') != string::npos) {
        addReason("Question text contains unreplaced placeholder brackets '{}'.");
    }

    // 2. Misconception overlap with correct answer
    // If a common mistake happens to yield the correct answer for these specific numbers,
    // the student shouldn't be falsely accused of a misconception when they type the right answer!
    for (const auto& mc : q.misconceptions) {
        if (concreteValuesEqual(q.correctValue, mc.value)) {
            addReason("Misconception '" + mc.description + "' yields the exact same value (" + 
                      mc.value.toDisplayString() + ") as the correct answer.");
        }
    }

    // 3. Misconception collisions with each other
    // If two different mistakes result in the exact same wrong answer, we wouldn't know which feedback to show.
    for (size_t i = 0; i < q.misconceptions.size(); ++i) {
        for (size_t j = i + 1; j < q.misconceptions.size(); ++j) {
            if (concreteValuesEqual(q.misconceptions[i].value, q.misconceptions[j].value)) {
                addReason("Misconception '" + q.misconceptions[i].description + 
                          "' and Misconception '" + q.misconceptions[j].description + 
                          "' yield the exact same value (" + q.misconceptions[i].value.toDisplayString() + ").");
            }
        }
    }

    return res;
}
```

---

## `InstanceValidator.hpp`

```cpp
#ifndef INSTANCE_VALIDATOR_HPP
#define INSTANCE_VALIDATOR_HPP

#include "GenericExprEngine.hpp"
#include <string>
#include <vector>

struct InstanceValidationResult {
    bool isValid;
    std::vector<std::string> reasons;
};

class InstanceValidator {
public:
    // Validates a fully generated question instance for semantic and practical usability.
    // Checks for empty text, unreplaced placeholders, and misconception overlaps.
    static InstanceValidationResult validateInstance(const GeneratedQuestion& q);
};

#endif // INSTANCE_VALIDATOR_HPP
```

---

## `TemplateEngine.cpp`

```cpp
#include "TemplateEngine.hpp"
#include "InstanceValidator.hpp"
#include <algorithm>
#include <cstdlib>
#include <vector>

std::string TemplateEngine::makeSignature(const GeneratedQuestion& q) const {
    // Plain rendered text -- this is exactly what's passed to
    // GenericExprEngine as the exclusion set, so the two must agree.
    return q.questionText;
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

    std::vector<const DynamicTemplate*> validTemplates;
    std::string activeTarget;
    
    for (const auto& tmpl : theory.templates) {
        for (const auto& target : tmpl.targetConcepts) {
            if (needsConcept(target)) { 
                if (activeTarget.empty()) {
                    activeTarget = target;
                }
                if (target == activeTarget) {
                    validTemplates.push_back(&tmpl); 
                }
                break; 
            }
        }
    }

    const DynamicTemplate* chosen = nullptr;
    if (!validTemplates.empty()) {
        chosen = validTemplates[rand() % validTemplates.size()];
    } else if (!theory.templates.empty()) {
        chosen = &theory.templates[0];
    }
    
    if (!chosen) throw TemplateError("No templates available in the current Formal Theory.");

    // Diversity (avoiding a repeat of a question already asked this
    // session) is handled inside GenericExprEngine via blocking clauses --
    // passing usedSignatures lets it actively search for an assignment
    // this session hasn't seen yet, rather than relying on re-randomizing
    // and hoping for the best (Z3 is deterministic for lightly-constrained
    // problems, so hoping doesn't work -- verified during development).
    const int kMaxValidationRetries = 10;
    GeneratedQuestion q;
    
    for (int attempt = 0; attempt < kMaxValidationRetries; ++attempt) {
        q = GenericExprEngine::generateInstance(*chosen, usedSignatures);
        InstanceValidationResult ivr = InstanceValidator::validateInstance(q);
        if (ivr.isValid) break;
        
        // If not valid (e.g. misconception collision), block this specific 
        // generation so the engine finds a different numerical assignment
        usedSignatures.insert(makeSignature(q));
    }

    registerIfUnique(q); // record even if it turned out to still be a repeat
    return q;
}
```