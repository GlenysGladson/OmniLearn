#ifndef TEMPLATE_ENGINE_HPP
#define TEMPLATE_ENGINE_HPP

#include "LearnerModel.hpp"
#include "Theory.hpp"
#include "GenericExprEngine.hpp"
#include <set>
#include <string>

class TemplateEngine {
private:
    std::set<std::string> usedSignatures;

    std::string makeSignature(const GeneratedQuestion& q) const;
    bool registerIfUnique(const GeneratedQuestion& q);

public:
    // Picks the first template (in load order) whose target concepts
    // include a currently weak or uncovered concept, and instantiates it
    // via GenericExprEngine. Throws TemplateError if no template exists.
    GeneratedQuestion selectAndInitializeTemplate(const LearnerModel& learner, const FormalTheory& theory);
};

#endif // TEMPLATE_ENGINE_HPP