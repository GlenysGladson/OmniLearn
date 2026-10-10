#include "InstanceValidator.hpp"
#include <regex>

using namespace std;

InstanceValidationResult InstanceValidator::validateInstance(const GeneratedQuestion& q) {
    InstanceValidationResult res;
    res.isValid = true;
    auto addReason = [&](const string& r) { res.isValid = false; res.reasons.push_back(r); };

    // 1. Text sanity checks
    if (q.questionText.empty()) {
        addReason("Question text is completely empty.");
    }
    // A placeholder looks like {name} (a letter or underscore first). Rendered sets
    // such as {5, 6}, {3} or {} are NOT placeholders and must not be flagged.
    static const regex placeholder("\\{[A-Za-z_][A-Za-z0-9_]*\\}");
    smatch m;
    if (regex_search(q.questionText, m, placeholder)) {
        addReason("Question text contains an unreplaced placeholder '" + m.str() + "'.");
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
