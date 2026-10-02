#ifndef TEMPLATE_VALIDATOR_HPP
#define TEMPLATE_VALIDATOR_HPP

#include "Theory.hpp"
#include <string>
#include <vector>
#include <set>

struct ValidationResult {
    bool isValid;
    std::vector<std::string> errors;
};

class TemplateValidator {
public:
    static ValidationResult validateTemplate(const DynamicTemplate& tmpl, const FormalTheory& theory, std::set<std::string>& seenIds);
    static ValidationResult validateTheory(const FormalTheory& theory);
};

#endif // TEMPLATE_VALIDATOR_HPP
