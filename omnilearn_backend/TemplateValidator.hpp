#ifndef TEMPLATE_VALIDATOR_HPP
#define TEMPLATE_VALIDATOR_HPP

#include "Theory.hpp"
#include <string>
#include <vector>
#include <set>

struct ValidationResult {
    bool isValid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

class TemplateValidator {
public:
    static ValidationResult validateTemplate(const DynamicTemplate& tmpl, const FormalTheory& theory, std::set<std::string>& seenIds);
    static ValidationResult validateTheory(const FormalTheory& theory);

    // Checks the BKT parameters of every concept in `theory`.
    static ValidationResult validateConcepts(const FormalTheory& theory);

    // Checks the dependency graph of a (usually merged) theory.
    // Errors: a dependency cycle (it would block question selection for ever).
    // Warnings: an edge that names an unknown concept (it is ignored).
    static ValidationResult validateDependencies(const FormalTheory& theory);
};

#endif // TEMPLATE_VALIDATOR_HPP


