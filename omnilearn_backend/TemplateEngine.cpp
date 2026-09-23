#include "TemplateEngine.hpp"
#include <algorithm>

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

    const DynamicTemplate* chosen = nullptr;
    for (const auto& tmpl : theory.templates) {
        for (const auto& target : tmpl.targetConcepts) {
            if (needsConcept(target)) { chosen = &tmpl; break; }
        }
        if (chosen) break;
    }
    if (!chosen && !theory.templates.empty()) chosen = &theory.templates[0];
    if (!chosen) throw TemplateError("No templates available in the current Formal Theory.");

    // Diversity (avoiding a repeat of a question already asked this
    // session) is handled inside GenericExprEngine via blocking clauses --
    // passing usedSignatures lets it actively search for an assignment
    // this session hasn't seen yet, rather than relying on re-randomizing
    // and hoping for the best (Z3 is deterministic for lightly-constrained
    // problems, so hoping doesn't work -- verified during development).
    GeneratedQuestion q = GenericExprEngine::generateInstance(*chosen, usedSignatures);
    registerIfUnique(q); // record even if it turned out to still be a repeat
    return q;
}