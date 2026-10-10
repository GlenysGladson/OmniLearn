#include "TemplateEngine.hpp"
#include "InstanceValidator.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

namespace {

std::vector<const DynamicTemplate*> templatesFor(const FormalTheory& theory, const std::string& concept) {
    std::vector<const DynamicTemplate*> out;
    for (const auto& t : theory.templates) {
        if (std::find(t.targetConcepts.begin(), t.targetConcepts.end(), concept) != t.targetConcepts.end()) {
            out.push_back(&t);
        }
    }
    return out;
}

bool exposesError(const DynamicTemplate& t, const std::set<std::string>& errorNames) {
    for (const auto& mc : t.misconceptions) {
        if (errorNames.count(mc.name)) return true;
    }
    return false;
}

std::string fmt2(double x) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2) << x;
    return o.str();
}

} // namespace

TemplateEngine::TemplateEngine() : rng(std::random_device{}()) {}

bool TemplateEngine::tryConcept(const std::string& concept, bool isRecheck, const LearnerModel& learner,
                                const FormalTheory& theory, Selection& out, std::string& failure) {
    std::vector<const DynamicTemplate*> eligible = templatesFor(theory, concept);
    if (eligible.empty()) {
        failure = "no template targets '" + concept + "'";
        return false;
    }

    std::vector<std::string> errList = learner.activeErrors(concept);
    std::set<std::string> errors(errList.begin(), errList.end());
    double target = std::max(0.1, std::min(0.9, learner.pKnown(concept)));

    struct Scored {
        const DynamicTemplate* t;
        int errRank;     // 0 = exposes an unresolved error
        int uses;
        double gap;
        unsigned jitter;
    };
    std::vector<Scored> scored;
    for (const auto* t : eligible) {
        Scored s;
        s.t = t;
        s.errRank = (!isRecheck && exposesError(*t, errors)) ? 0 : 1;
        s.uses = learner.templateUseCount(t->id);
        s.gap = std::fabs(t->difficulty - target);
        s.jitter = rng();
        scored.push_back(s);
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.errRank != b.errRank) return a.errRank < b.errRank;
        if (a.uses != b.uses) return a.uses < b.uses;
        if (std::fabs(a.gap - b.gap) > 1e-9) return a.gap < b.gap;
        return a.jitter < b.jitter;
    });

    bool haveRepeat = false;
    Selection repeatFallback;
    std::string lastError;

    for (const auto& s : scored) {
        try {
            GeneratedQuestion q = GenericExprEngine::generateInstance(*s.t, learner.asked());
            InstanceValidationResult ivr = InstanceValidator::validateInstance(q);
            if (!ivr.isValid) {
                // Text problems are the same for every assignment, so move on to the next template.
                lastError = "template '" + s.t->id + "' produced an unusable question: " + ivr.reasons.front();
                continue;
            }
            Selection sel;
            sel.focusConcept = concept;
            sel.isRecheck = isRecheck;

            std::ostringstream why;
            if (isRecheck) {
                why << "final re-check of '" << concept << "'";
            } else {
                why << (learner.attempts(concept) == 0 ? "uncovered" : "weak")
                    << " concept '" << concept << "' (P(known)=" << fmt2(learner.pKnown(concept)) << ")";
                if (s.errRank == 0) why << "; targets an unresolved error pattern";
            }
            sel.reason = why.str();
            sel.question = q;

            if (!q.isRepeat) {
                out = sel;
                return true;
            }
            if (!haveRepeat) {
                repeatFallback = sel;
                haveRepeat = true;
            }
        } catch (const TemplateError& e) {
            lastError = e.what();
        }
    }

    if (haveRepeat) {
        out = repeatFallback;
        return true;
    }
    failure = lastError.empty() ? "no template could be instantiated for '" + concept + "'" : lastError;
    return false;
}

Selection TemplateEngine::selectNext(const LearnerModel& learner, const FormalTheory& theory) {
    struct Cand {
        std::string name;
        double p;
        bool err;
        int attempts;
    };
    std::vector<Cand> work, recheck;
    std::vector<std::string> blocked; // need work but have no template

    for (const auto& kv : theory.concepts) {
        if (!kv.second.assessable) continue;
        const std::string& name = kv.first;

        bool needsWork = learner.needsWork(name);
        bool needsRecheck = learner.needsRecheck(name);
        if (!needsWork && !needsRecheck) continue;

        if (templatesFor(theory, name).empty()) {
            blocked.push_back(name);
            continue;
        }
        Cand c{name, learner.pKnown(name), learner.hasActiveError(name), learner.attempts(name)};
        if (needsWork) {
            if (learner.prerequisitesMet(name, theory)) work.push_back(c);
        } else {
            recheck.push_back(c);
        }
    }

    std::sort(work.begin(), work.end(), [](const Cand& a, const Cand& b) {
        if (std::fabs(a.p - b.p) > 1e-9) return a.p < b.p;
        if (a.err != b.err) return a.err;
        if (a.attempts != b.attempts) return a.attempts < b.attempts;
        return a.name < b.name;
    });
    std::sort(recheck.begin(), recheck.end(), [](const Cand& a, const Cand& b) {
        if (std::fabs(a.p - b.p) > 1e-9) return a.p < b.p;
        return a.name < b.name;
    });

    std::string failures;
    Selection sel;

    for (const auto& c : work) {
        std::string why;
        if (tryConcept(c.name, false, learner, theory, sel, why)) return sel;
        failures += "  - " + c.name + ": " + why + "\n";
    }
    // Final re-checks run only when no concept needs work.
    if (work.empty()) {
        for (const auto& c : recheck) {
            std::string why;
            if (tryConcept(c.name, true, learner, theory, sel, why)) return sel;
            failures += "  - " + c.name + " (re-check): " + why + "\n";
        }
    }

    std::string msg = "No question can be produced.";
    if (!blocked.empty()) {
        msg += "\n  Concepts that need questions but have no template: ";
        for (size_t i = 0; i < blocked.size(); ++i) msg += (i ? ", " : "") + blocked[i];
        msg += "\n  Concepts that depend on them stay blocked until a template is added.";
    }
    if (!failures.empty()) msg += "\n  Template failures:\n" + failures;
    if (blocked.empty() && failures.empty()) {
        msg += " Nothing needs work or a re-check (the learner may already be certified).";
    }
    throw TemplateError(msg);
}
