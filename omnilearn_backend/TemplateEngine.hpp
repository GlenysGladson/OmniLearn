#ifndef TEMPLATE_ENGINE_HPP
#define TEMPLATE_ENGINE_HPP

#include "LearnerModel.hpp"
#include "Theory.hpp"
#include "GenericExprEngine.hpp"
#include <random>
#include <string>

// What the engine hands back: the question plus why it was chosen.
struct Selection {
    GeneratedQuestion question;
    std::string focusConcept;   // the concept that drove the choice
    bool isRecheck = false;     // final re-check question
    std::string reason;         // short human-readable explanation
};

class TemplateEngine {
private:
    std::mt19937 rng;

    bool tryConcept(const std::string& concept, bool isRecheck, const LearnerModel& learner,
                    const FormalTheory& theory, Selection& out, std::string& failure);

public:
    TemplateEngine();

    // Chooses the next question.
    //  1. Concepts that need work (weak, uncovered, or with an unresolved
    //     error pattern) whose assessable prerequisites are mastered,
    //     lowest P(known) first; ties go to concepts with an active error,
    //     then to fewer attempts.
    //  2. Inside that concept, templates that expose an unresolved error
    //     come first, then the least-used templates, then the difficulty
    //     closest to the learner's current P(known).
    //  3. When nothing needs work, one final re-check question for each
    //     concept that has not passed it.
    // Throws TemplateError when no question can be produced (for example, a
    // concept that needs work has no template, which blocks its dependents).
    Selection selectNext(const LearnerModel& learner, const FormalTheory& theory);
};

#endif // TEMPLATE_ENGINE_HPP
