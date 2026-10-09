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
