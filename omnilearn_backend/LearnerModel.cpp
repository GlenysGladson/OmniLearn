#include "LearnerModel.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace {

double clampd(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }

// One Bayesian Knowledge Tracing step.
//  1. Bayes update of P(known) from the observed answer.
//  2. Learning step: an unknown concept may become known.
// `difficulty` (0 easy .. 1 hard) scales slip up and guess down for harder
// templates. A Bool answer has a 50% guess floor, whatever the YAML says.
// The scaling is a simple heuristic, not a fitted IRT model.
double bktStep(const BktParams& p, double pKnown, bool correct,
               const std::string& answerType, double difficulty) {
    double d = clampd(difficulty, 0.0, 1.0);
    double slip = clampd(p.pSlip * (0.5 + d), 0.01, 0.45);
    double guess = clampd(p.pGuess * (1.5 - d), 0.01, 0.50);
    if (answerType == "Bool") guess = std::max(guess, 0.50);

    double k = clampd(pKnown, 1e-6, 1.0 - 1e-6);
    double posterior;
    if (correct) {
        posterior = k * (1.0 - slip) / (k * (1.0 - slip) + (1.0 - k) * guess);
    } else {
        posterior = k * slip / (k * slip + (1.0 - k) * (1.0 - guess));
    }
    double next = posterior + (1.0 - posterior) * clampd(p.pLearn, 0.0, 1.0);
    return clampd(next, 1e-6, 1.0 - 1e-6);
}

} // namespace

double LearnerModel::recentAccuracy(const ConceptState& s) const {
    if (s.recent.empty()) return 0.0;
    int sum = 0;
    for (int v : s.recent) sum += v;
    return static_cast<double>(sum) / static_cast<double>(s.recent.size());
}

void LearnerModel::initialize(const FormalTheory& theory) {
    concepts.clear();
    errors.clear();
    templateUses.clear();
    askedQuestions.clear();
    questionsAnswered = 0;
    for (const auto& pair : theory.concepts) {
        ConceptState s;
        s.bkt = pair.second.bkt;
        s.pKnown = pair.second.bkt.pInit;
        concepts[pair.first] = s;
    }
}

void LearnerModel::update(const Observation& obs) {
    ++questionsAnswered;
    if (!obs.templateId.empty()) templateUses[obs.templateId]++;
    if (!obs.questionText.empty()) askedQuestions.insert(obs.questionText);

    for (const std::string& t : obs.targets) {
        auto it = concepts.find(t);
        if (it == concepts.end()) continue;
        ConceptState& s = it->second;

        s.attempts += 1;
        if (obs.correct) s.correct += 1;
        s.pKnown = bktStep(s.bkt, s.pKnown, obs.correct, obs.answerType, obs.difficulty);

        s.recent.push_back(obs.correct ? 1 : 0);
        while (static_cast<int>(s.recent.size()) > cfg.accuracyWindow) s.recent.pop_front();

        if (!obs.correct) s.recheckPassed = false;

        // Error patterns of this concept: a correct answer counts toward
        // resolving them; a wrong answer that is NOT the same error breaks
        // the streak. The matched error itself is recorded below.
        for (auto& kv : errors) {
            if (kv.first.first != t) continue;
            ErrorState& e = kv.second;
            if (obs.correct) {
                if (e.active && ++e.streakSinceLast >= cfg.errorResolveStreak) e.active = false;
            } else if (obs.errorName.empty() || kv.first.second != obs.errorName) {
                e.streakSinceLast = 0;
            }
        }
        if (!obs.correct && !obs.errorName.empty()) {
            ErrorState& e = errors[{t, obs.errorName}];
            e.occurrences += 1;
            e.streakSinceLast = 0;
            e.active = true;
        }

        if (obs.correct && obs.isRecheck && isMastered(t) && !hasActiveError(t)) {
            s.recheckPassed = true;
        }
    }
}

double LearnerModel::pKnown(const std::string& c) const {
    auto it = concepts.find(c);
    return it == concepts.end() ? 0.0 : it->second.pKnown;
}

int LearnerModel::attempts(const std::string& c) const {
    auto it = concepts.find(c);
    return it == concepts.end() ? 0 : it->second.attempts;
}

int LearnerModel::correctCount(const std::string& c) const {
    auto it = concepts.find(c);
    return it == concepts.end() ? 0 : it->second.correct;
}

bool LearnerModel::isMastered(const std::string& c) const {
    auto it = concepts.find(c);
    if (it == concepts.end()) return false;
    const ConceptState& s = it->second;
    return s.pKnown >= cfg.masteryThreshold &&
           s.attempts >= cfg.minAttempts &&
           recentAccuracy(s) >= cfg.minRecentAccuracy;
}

bool LearnerModel::hasActiveError(const std::string& c) const {
    for (const auto& kv : errors) {
        if (kv.first.first == c && kv.second.active) return true;
    }
    return false;
}

std::vector<std::string> LearnerModel::activeErrors(const std::string& c) const {
    std::vector<std::string> out;
    for (const auto& kv : errors) {
        if (kv.first.first == c && kv.second.active) out.push_back(kv.first.second);
    }
    return out;
}

bool LearnerModel::needsWork(const std::string& c) const {
    return !isMastered(c) || hasActiveError(c);
}

bool LearnerModel::needsRecheck(const std::string& c) const {
    auto it = concepts.find(c);
    if (it == concepts.end()) return false;
    return isMastered(c) && !hasActiveError(c) && !it->second.recheckPassed;
}

bool LearnerModel::prerequisitesMet(const std::string& c, const FormalTheory& theory) const {
    for (const auto& p : theory.assessablePrerequisites(c)) {
        if (!isMastered(p)) return false;
    }
    return true;
}

std::vector<std::string> LearnerModel::getWeakConcepts(const FormalTheory& theory) const {
    std::vector<std::string> out;
    for (const auto& pair : theory.concepts) {
        if (!pair.second.assessable) continue;
        if (attempts(pair.first) > 0 && needsWork(pair.first)) out.push_back(pair.first);
    }
    return out;
}

std::vector<std::string> LearnerModel::getUncoveredConcepts(const FormalTheory& theory) const {
    std::vector<std::string> out;
    for (const auto& pair : theory.concepts) {
        if (!pair.second.assessable) continue;
        if (attempts(pair.first) == 0) out.push_back(pair.first);
    }
    return out;
}

bool LearnerModel::isCertified(const FormalTheory& theory) const {
    for (const auto& pair : theory.concepts) {
        if (!pair.second.assessable) continue;
        const std::string& c = pair.first;
        auto it = concepts.find(c);
        if (it == concepts.end()) return false;
        if (!isMastered(c) || hasActiveError(c) || !it->second.recheckPassed) return false;
    }
    return true;
}

std::vector<std::string> LearnerModel::certificationBlockers(const FormalTheory& theory) const {
    std::vector<std::string> out;
    for (const auto& pair : theory.concepts) {
        if (!pair.second.assessable) continue;
        const std::string& c = pair.first;
        auto it = concepts.find(c);
        if (it == concepts.end()) { out.push_back(c + ": no learner state"); continue; }
        const ConceptState& s = it->second;
        std::ostringstream o;
        o << std::fixed << std::setprecision(2);
        if (s.attempts == 0) out.push_back(c + ": never asked");
        else {
            if (s.pKnown < cfg.masteryThreshold)
                { o.str(""); o << c << ": P(known) " << s.pKnown << " < " << cfg.masteryThreshold; out.push_back(o.str()); }
            if (s.attempts < cfg.minAttempts)
                out.push_back(c + ": only " + std::to_string(s.attempts) + " attempt(s), need " + std::to_string(cfg.minAttempts));
            if (recentAccuracy(s) < cfg.minRecentAccuracy)
                { o.str(""); o << c << ": recent accuracy " << recentAccuracy(s) << " < " << cfg.minRecentAccuracy; out.push_back(o.str()); }
        }
        for (const auto& e : activeErrors(c)) out.push_back(c + ": unresolved error pattern '" + e + "'");
        if (isMastered(c) && !hasActiveError(c) && !s.recheckPassed) out.push_back(c + ": final re-check not passed yet");
    }
    return out;
}

int LearnerModel::templateUseCount(const std::string& templateId) const {
    auto it = templateUses.find(templateId);
    return it == templateUses.end() ? 0 : it->second;
}

bool LearnerModel::saveState(const std::string& path) const {
    YAML::Emitter out;
    out.SetDoublePrecision(10);
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << 1;
    out << YAML::Key << "questions_answered" << YAML::Value << questionsAnswered;

    out << YAML::Key << "concepts" << YAML::Value << YAML::BeginMap;
    for (const auto& kv : concepts) {
        const ConceptState& s = kv.second;
        out << YAML::Key << kv.first << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "p_known" << YAML::Value << s.pKnown;
        out << YAML::Key << "attempts" << YAML::Value << s.attempts;
        out << YAML::Key << "correct" << YAML::Value << s.correct;
        out << YAML::Key << "recheck_passed" << YAML::Value << s.recheckPassed;
        out << YAML::Key << "recent" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (int v : s.recent) out << v;
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndMap;

    out << YAML::Key << "errors" << YAML::Value << YAML::BeginSeq;
    for (const auto& kv : errors) {
        out << YAML::BeginMap;
        out << YAML::Key << "concept" << YAML::Value << kv.first.first;
        out << YAML::Key << "name" << YAML::Value << kv.first.second;
        out << YAML::Key << "occurrences" << YAML::Value << kv.second.occurrences;
        out << YAML::Key << "streak" << YAML::Value << kv.second.streakSinceLast;
        out << YAML::Key << "active" << YAML::Value << kv.second.active;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "template_uses" << YAML::Value << YAML::BeginMap;
    for (const auto& kv : templateUses) out << YAML::Key << kv.first << YAML::Value << kv.second;
    out << YAML::EndMap;

    out << YAML::Key << "asked" << YAML::Value << YAML::BeginSeq;
    for (const auto& text : askedQuestions) out << YAML::DoubleQuoted << text;
    out << YAML::EndSeq;
    out << YAML::EndMap;

    if (!out.good()) return false;

    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << out.c_str() << "\n";
        f.flush();
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(tmp, path, ec);
        if (ec) return false;
    }
    return true;
}

bool LearnerModel::loadState(const std::string& path, const FormalTheory& theory) {
    if (!fs::exists(path)) return false;

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const std::exception& ex) {
        std::cerr << "[WARN] Could not read state file '" << path << "': " << ex.what() << "\n";
        return false;
    }
    if (!root.IsMap() || !root["concepts"]) {
        std::cerr << "[WARN] State file '" << path << "' has no 'concepts' section; ignored.\n";
        return false;
    }

    // Parse into temporaries first so a bad file leaves the model untouched.
    std::map<std::string, ConceptState> newConcepts = concepts;
    std::map<std::pair<std::string, std::string>, ErrorState> newErrors;
    std::map<std::string, int> newUses;
    std::set<std::string> newAsked;
    int newAnswered = 0;

    try {
        if (root["questions_answered"]) newAnswered = root["questions_answered"].as<int>();

        for (const auto& kv : root["concepts"]) {
            std::string name = kv.first.as<std::string>();
            auto it = newConcepts.find(name);
            if (it == newConcepts.end() || !theory.concepts.count(name)) {
                std::cerr << "[WARN] State has concept '" << name << "' that is not in the loaded theories; ignored.\n";
                continue;
            }
            const YAML::Node& n = kv.second;
            ConceptState& s = it->second;
            if (n["p_known"]) s.pKnown = n["p_known"].as<double>();
            if (n["attempts"]) s.attempts = n["attempts"].as<int>();
            if (n["correct"]) s.correct = n["correct"].as<int>();
            if (n["recheck_passed"]) s.recheckPassed = n["recheck_passed"].as<bool>();
            s.recent.clear();
            if (n["recent"]) for (const auto& v : n["recent"]) s.recent.push_back(v.as<int>() ? 1 : 0);
            while (static_cast<int>(s.recent.size()) > cfg.accuracyWindow) s.recent.pop_front();
            s.pKnown = clampd(s.pKnown, 1e-6, 1.0 - 1e-6);
        }

        if (root["errors"]) {
            for (const auto& n : root["errors"]) {
                std::string c = n["concept"].as<std::string>();
                std::string name = n["name"].as<std::string>();
                if (!theory.concepts.count(c)) continue;
                ErrorState e;
                if (n["occurrences"]) e.occurrences = n["occurrences"].as<int>();
                if (n["streak"]) e.streakSinceLast = n["streak"].as<int>();
                if (n["active"]) e.active = n["active"].as<bool>();
                newErrors[{c, name}] = e;
            }
        }
        if (root["template_uses"]) {
            for (const auto& kv : root["template_uses"]) newUses[kv.first.as<std::string>()] = kv.second.as<int>();
        }
        if (root["asked"]) {
            for (const auto& n : root["asked"]) newAsked.insert(n.as<std::string>());
        }
    } catch (const std::exception& ex) {
        std::cerr << "[WARN] State file '" << path << "' is malformed (" << ex.what() << "); ignored.\n";
        return false;
    }

    concepts = newConcepts;
    errors = newErrors;
    templateUses = newUses;
    askedQuestions = newAsked;
    questionsAnswered = newAnswered;
    return true;
}

void LearnerModel::printReport(const FormalTheory& theory) const {
    std::cout << "\n================ LEARNER MODEL STATE ================\n";
    std::cout << std::left << std::setw(26) << "Concept" << std::right
              << std::setw(10) << "P(known)" << std::setw(10) << "Attempts"
              << std::setw(9) << "Correct" << std::setw(9) << "Recent" << "  Re-check\n";
    for (const auto& pair : theory.concepts) {
        if (!pair.second.assessable) continue;
        const std::string& c = pair.first;
        auto it = concepts.find(c);
        if (it == concepts.end()) continue;
        const ConceptState& s = it->second;
        std::cout << std::left << std::setw(26) << c << std::right << std::fixed << std::setprecision(3)
                  << std::setw(10) << s.pKnown << std::setw(10) << s.attempts << std::setw(9) << s.correct
                  << std::setprecision(2) << std::setw(9) << recentAccuracy(s)
                  << "  " << (s.recheckPassed ? "passed" : "pending") << "\n";
    }

    bool anyError = false;
    for (const auto& kv : errors) {
        if (!theory.isAssessable(kv.first.first)) continue;
        if (!anyError) { std::cout << "\nError patterns (concept :: misconception)\n"; anyError = true; }
        std::cout << "  " << kv.first.first << " :: " << kv.first.second
                  << "  seen " << kv.second.occurrences << "x, "
                  << (kv.second.active ? "UNRESOLVED" : "resolved") << "\n";
    }
    std::cout << "\nNote: BKT parameters are untuned defaults unless a concept sets its own.\n";
    std::cout << "=====================================================\n";
}
