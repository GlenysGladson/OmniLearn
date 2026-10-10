#ifndef INSTANCE_VALIDATOR_HPP
#define INSTANCE_VALIDATOR_HPP

#include "GenericExprEngine.hpp"
#include <string>
#include <vector>

struct InstanceValidationResult {
    bool isValid = true;
    std::vector<std::string> reasons;
};

class InstanceValidator {
public:
    // Validates a fully generated question instance for semantic and practical usability.
    // Checks for empty text, unreplaced {placeholder} names, and misconception overlaps.
    // Note: GenericExprEngine already leaves clashing misconceptions out of
    // `q.misconceptions`, so the two overlap checks are a safety net.
    static InstanceValidationResult validateInstance(const GeneratedQuestion& q);
};

#endif // INSTANCE_VALIDATOR_HPP
