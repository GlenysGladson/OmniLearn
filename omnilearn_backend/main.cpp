#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "GenericExprEngine.hpp"
#include "InstanceValidator.hpp"
#include "LearnerModel.hpp"
#include "TemplateEngine.hpp"
#include "TemplateValidator.hpp"
#include "Theory.hpp"
#include "YamlParser.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

namespace {

// Number of generated instances used to check, at load time, that each
// misconception really gives a different value than the correct answer.
const int kLoadSamples = 3;

} // namespace

// Scans `theoriesDir` for *.yaml / *.yml files and builds one combined theory.
//
// For every file: parse, check concept parameters, then for every template:
// structural/solver checks (TemplateValidator, with template IDs unique
// across ALL files), then real instances through GenericExprEngine (which
// also re-checks every answer in a second Z3 context). A misconception that
// matches the correct answer (or an earlier misconception) in every sampled
// instance is removed with a warning. Anything that fails is skipped and
// reported, never left to crash a session.
//
// After all files are merged, the dependency graph is checked: a cycle is
// fatal, an edge naming an unknown concept is a warning.
FormalTheory loadAllTheories(const std::string& theoriesDir) {
    if (!fs::exists(theoriesDir) || !fs::is_directory(theoriesDir)) {
        throw std::runtime_error("Theories directory not found: " + theoriesDir);
    }

    std::vector<fs::path> yamlFiles;
    for (const auto& entry : fs::directory_iterator(theoriesDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext == ".yaml" || ext == ".yml") yamlFiles.push_back(entry.path());
    }
    if (yamlFiles.empty()) {
        throw std::runtime_error("No .yaml/.yml theory files found in: " + theoriesDir);
    }
    std::sort(yamlFiles.begin(), yamlFiles.end());

    FormalTheory combined("CombinedTheorySet");
    std::set<std::string> seenTemplateIds;

    for (const auto& path : yamlFiles) {
        std::cout << "Loading theory definition from: " << path.string() << "\n";

        FormalTheory theory;
        try {
            theory = YamlParser::parseTheoryFile(path.string());
        } catch (const std::exception& ex) {
            std::cerr << "[SKIP FILE] " << path.string() << ": " << ex.what() << "\n";
            continue;
        }

        ValidationResult cr = TemplateValidator::validateConcepts(theory);
        if (!cr.isValid) {
            for (const auto& e : cr.errors) std::cerr << "[SKIP FILE] " << path.string() << ": " << e << "\n";
            continue;
        }

        FormalTheory validated(theory.theoryName);
        validated.concepts = theory.concepts;
        validated.dependencyGraph = theory.dependencyGraph;

        int validCount = 0, invalidCount = 0;
        for (const auto& original : theory.templates) {
            DynamicTemplate tmpl = original;

            ValidationResult vr = TemplateValidator::validateTemplate(tmpl, theory, seenTemplateIds);
            if (!vr.isValid) {
                for (const auto& e : vr.errors) {
                    std::cerr << "[VALIDATION ERROR] " << path.string() << ", template '" << tmpl.id << "': " << e << "\n";
                }
                ++invalidCount;
                continue;
            }

            try {
                // name -> (times dropped, reason)
                std::map<std::string, std::pair<int, std::string>> dropCount;
                for (int i = 0; i < kLoadSamples; ++i) {
                    GeneratedQuestion q = GenericExprEngine::generateInstance(tmpl);
                    InstanceValidationResult ivr = InstanceValidator::validateInstance(q);
                    if (!ivr.isValid) {
                        throw std::runtime_error("generated question is unusable: " + ivr.reasons.front());
                    }
                    for (const auto& d : q.droppedMisconceptions) {
                        auto& slot = dropCount[d.name];
                        slot.first += 1;
                        slot.second = d.reason;
                    }
                }
                std::vector<MisconceptionDef> kept;
                for (const auto& mc : tmpl.misconceptions) {
                    auto it = dropCount.find(mc.name);
                    if (it != dropCount.end() && it->second.first >= kLoadSamples) {
                        std::cerr << "[WARN] " << path.string() << ", template '" << tmpl.id
                                  << "': misconception '" << mc.name << "' removed ("
                                  << it->second.second << " in every sampled instance).\n";
                    } else {
                        kept.push_back(mc);
                    }
                }
                tmpl.misconceptions = kept;
                validated.templates.push_back(tmpl);
                ++validCount;
            } catch (const std::exception& ex) {
                std::cerr << "[VALIDATION ERROR] " << path.string() << ", template '" << tmpl.id << "': " << ex.what() << "\n";
                ++invalidCount;
            }
        }

        std::cout << "  -> '" << theory.theoryName << "': " << theory.concepts.size()
                  << " concept(s), " << validCount << " valid template(s)";
        if (invalidCount > 0) std::cout << ", " << invalidCount << " rejected";
        std::cout << "\n";

        combined.merge(validated);
    }

    ValidationResult dr = TemplateValidator::validateDependencies(combined);
    for (const auto& w : dr.warnings) std::cerr << "[WARN] " << w << "\n";
    if (!dr.isValid) {
        std::string msg = "Invalid dependency graph:";
        for (const auto& e : dr.errors) msg += "\n  " + e;
        throw std::runtime_error(msg);
    }

    if (combined.templates.empty()) {
        throw std::runtime_error("No valid templates were loaded from any theory file in: " + theoriesDir);
    }

    combined.warnOnUnreachableConcepts();
    return combined;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

namespace {

struct InputClosed {};  // stdin reached end of file
struct QuitRequested {}; // student typed quit / exit

std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
    return r;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool readLine(std::string& out) {
    if (!std::getline(std::cin, out)) return false;
    if (!out.empty() && out.back() == '\r') out.pop_back(); // Windows line endings
    return true;
}

bool parseWholeNumber(const std::string& s, long long& out) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '+' || s[0] == '-') i = 1;
    if (i >= s.size() || s.size() - i > 18) return false;
    for (size_t k = i; k < s.size(); ++k) {
        if (!std::isdigit(static_cast<unsigned char>(s[k]))) return false;
    }
    try { out = std::stoll(s); } catch (...) { return false; }
    return true;
}

// Strict parse of a typed answer. Returns false and sets `err` on bad input,
// so a typo is never silently turned into a (possibly correct) answer.
bool parseAnswer(const std::string& raw, const std::string& answerType, ConcreteValue& out, std::string& err) {
    std::string s = trim(raw);
    out = ConcreteValue();
    out.sort = answerType;

    if (answerType == "Bool") {
        std::string l = toLower(s);
        if (l == "true" || l == "t" || l == "yes" || l == "y" || l == "1") { out.boolValue = true; return true; }
        if (l == "false" || l == "f" || l == "no" || l == "n" || l == "0") { out.boolValue = false; return true; }
        err = "Please answer true or false.";
        return false;
    }
    if (answerType == "Real") {
        if (s.empty()) { err = "Please enter a number."; return false; }
        try {
            size_t pos = 0;
            double v = std::stod(s, &pos);
            if (pos != s.size() || !std::isfinite(v)) { err = "Please enter a plain number such as 0.25."; return false; }
            out.realValue = v;
            return true;
        } catch (...) {
            err = "Please enter a plain number such as 0.25.";
            return false;
        }
    }
    if (answerType == "Set") {
        std::string l = toLower(s);
        if (l == "{}" || l == "empty" || l == "none" || l == "{ }") return true; // empty set
        std::string cleaned;
        for (char c : s) {
            if (c == '{' || c == '}' || c == '[' || c == ']' || c == '(' || c == ')') continue;
            cleaned += (c == ',') ? ' ' : c;
        }
        std::stringstream ss(cleaned);
        std::string tok;
        std::set<int> elems;
        bool any = false;
        while (ss >> tok) {
            long long v = 0;
            if (!parseWholeNumber(tok, v) || v < 1 || v > 64) {
                err = "'" + tok + "' is not a valid element. Use whole numbers like 1,2,3 (or {} for the empty set).";
                return false;
            }
            elems.insert(static_cast<int>(v));
            any = true;
        }
        if (!any) { err = "Please list the elements, e.g. 1,2,3 (or {} for the empty set)."; return false; }
        out.setValue.assign(elems.begin(), elems.end());
        return true;
    }
    // "Int" and any other declared type are treated as whole numbers.
    long long v = 0;
    if (!parseWholeNumber(s, v)) { err = "Please enter a whole number."; return false; }
    out.sort = "Int";
    out.intValue = v;
    return true;
}

// Asks until the student gives a valid answer. Throws InputClosed on end of
// input and QuitRequested on "quit"/"exit".
ConcreteValue promptForAnswer(const std::string& answerType) {
    for (;;) {
        if (answerType == "Set") std::cout << "Enter answer (elements, e.g. 1,2,3 or {} for empty): ";
        else if (answerType == "Bool") std::cout << "Enter answer (true/false): ";
        else if (answerType == "Real") std::cout << "Enter answer (a number): ";
        else std::cout << "Enter answer: ";
        std::cout.flush();

        std::string line;
        if (!readLine(line)) throw InputClosed();
        std::string l = toLower(trim(line));
        if (l == "quit" || l == "exit") throw QuitRequested();

        ConcreteValue v;
        std::string err;
        if (parseAnswer(line, answerType, v, err)) return v;
        std::cout << "  Invalid input: " << err << "\n";
    }
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

std::string csvEscape(const std::string& s) {
    bool needs = s.find_first_of(",\"\n\r") != std::string::npos;
    if (!needs) return s;
    std::string r = "\"";
    for (char c : s) {
        if (c == '"') r += "\"\"";
        else if (c == '\n' || c == '\r') r += ' ';
        else r += c;
    }
    return r + "\"";
}

std::string joinStrings(const std::vector<std::string>& v, const std::string& sep) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) r += (i ? sep : "") + v[i];
    return r;
}

std::string nowString() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return buf;
}

void appendLog(const std::string& path, int questionNo, const Selection& sel, bool correct,
               const std::string& errorName, const std::string& studentAnswer,
               const std::string& expected, double pBefore, double pAfter) {
    bool writeHeader = !fs::exists(path) || fs::file_size(path) == 0;
    std::ofstream f(path, std::ios::app);
    if (!f) {
        std::cerr << "[WARN] Could not write response log '" << path << "'.\n";
        return;
    }
    if (writeHeader) {
        f << "timestamp,question_no,template_id,focus_concept,target_concepts,is_recheck,difficulty,"
             "correct,error_name,student_answer,expected_answer,p_known_before,p_known_after\n";
    }
    f << nowString() << ',' << questionNo << ',' << csvEscape(sel.question.templateId) << ','
      << csvEscape(sel.focusConcept) << ',' << csvEscape(joinStrings(sel.question.targetConcepts, ";")) << ','
      << (sel.isRecheck ? 1 : 0) << ',' << sel.question.difficulty << ',' << (correct ? 1 : 0) << ','
      << csvEscape(errorName) << ',' << csvEscape(studentAnswer) << ',' << csvEscape(expected) << ','
      << std::fixed << std::setprecision(4) << pBefore << ',' << pAfter << '\n';
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

struct Options {
    std::string theoriesDir = "theories/";
    std::string statePath = "learner_state.yaml";
    std::string logPath = "responses_log.csv";
    bool fresh = false;
    bool verbose = false;
    int maxQuestions = 0; // 0 = automatic
    bool help = false;
};

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [theories_dir] [options]\n"
              << "  --state FILE         learner state file (default learner_state.yaml)\n"
              << "  --log FILE           response log, CSV (default responses_log.csv)\n"
              << "  --fresh              ignore and overwrite any saved state\n"
              << "  --max-questions N    stop after N questions in this run (default: 15 per concept, at least 40)\n"
              << "  --verbose            show why each question was chosen and P(known)\n"
              << "  --help               show this text\n"
              << "Type 'quit' at an answer prompt to save and exit.\n";
}

bool parseArgs(int argc, char* argv[], Options& o, std::string& err) {
    bool haveDir = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto needValue = [&](std::string& dst) {
            if (i + 1 >= argc) { err = "Option " + a + " needs a value."; return false; }
            dst = argv[++i];
            return true;
        };
        if (a == "--help" || a == "-h") o.help = true;
        else if (a == "--fresh") o.fresh = true;
        else if (a == "--verbose") o.verbose = true;
        else if (a == "--state") { if (!needValue(o.statePath)) return false; }
        else if (a == "--log") { if (!needValue(o.logPath)) return false; }
        else if (a == "--max-questions") {
            std::string v;
            if (!needValue(v)) return false;
            long long n = 0;
            if (!parseWholeNumber(v, n) || n < 1) { err = "--max-questions needs a positive whole number."; return false; }
            o.maxQuestions = static_cast<int>(n);
        } else if (!a.empty() && a[0] == '-') {
            err = "Unknown option: " + a;
            return false;
        } else if (!haveDir) {
            o.theoriesDir = a;
            haveDir = true;
        } else {
            err = "Unexpected argument: " + a;
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    srand(static_cast<unsigned int>(time(nullptr)));

    Options opt;
    std::string argErr;
    if (!parseArgs(argc, argv, opt, argErr)) {
        std::cerr << argErr << "\n";
        printUsage(argv[0]);
        return 1;
    }
    if (opt.help) {
        printUsage(argv[0]);
        return 0;
    }

    FormalTheory theory;
    try {
        theory = loadAllTheories(opt.theoriesDir);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return 1;
    }

    int assessableCount = 0;
    for (const auto& kv : theory.concepts) if (kv.second.assessable) ++assessableCount;

    std::cout << "\nLoaded " << theory.sourceTheories.size() << " theory file(s), "
              << theory.concepts.size() << " total concept(s) (" << assessableCount << " assessable), "
              << theory.templates.size() << " total valid template(s).\n";

    int maxQuestions = opt.maxQuestions > 0 ? opt.maxQuestions : std::max(40, 15 * assessableCount);

    LearnerModel learner;
    learner.initialize(theory);
    if (!opt.fresh && learner.loadState(opt.statePath, theory)) {
        std::cout << "Resumed saved state from '" << opt.statePath << "' ("
                  << learner.totalAnswered() << " question(s) answered so far).\n";
    } else if (opt.fresh) {
        std::cout << "Starting fresh (any existing state in '" << opt.statePath << "' will be overwritten).\n";
    }

    TemplateEngine engine;
    int exitCode = 0;
    int askedThisRun = 0;

    auto saveOrWarn = [&]() {
        if (!learner.saveState(opt.statePath)) {
            std::cerr << "[WARN] Could not save state to '" << opt.statePath << "'.\n";
        }
    };

    std::cout << "(Type 'quit' at an answer prompt to save and exit.)\n";

    try {
        while (!learner.isCertified(theory)) {
            if (askedThisRun >= maxQuestions) {
                std::cout << "\nQuestion limit reached (" << maxQuestions << " this run). Not certified.\n";
                std::cout << "Still missing:\n";
                for (const auto& b : learner.certificationBlockers(theory)) std::cout << "  - " << b << "\n";
                exitCode = 3;
                break;
            }

            Selection sel = engine.selectNext(learner, theory);
            const GeneratedQuestion& q = sel.question;

            int number = learner.totalAnswered() + 1;
            std::cout << "\n--- Question " << number << (sel.isRecheck ? " (final re-check)" : "") << " ---\n";
            if (opt.verbose) std::cout << "[" << sel.reason << "]\n";
            if (q.isRepeat) std::cout << "[note] No unused variant exists for this template; this question was asked before.\n";
            std::cout << q.questionText << "\n";

            ConcreteValue studentValue = promptForAnswer(q.answerType);
            bool isCorrect = concreteValuesEqual(studentValue, q.correctValue);

            std::string errorName, errorDescription;
            if (!isCorrect) {
                for (const auto& mc : q.misconceptions) {
                    if (concreteValuesEqual(studentValue, mc.value)) {
                        errorName = mc.name;
                        errorDescription = mc.description;
                        break;
                    }
                }
            }

            if (isCorrect) {
                std::cout << "> Correct!\n";
            } else {
                std::cout << "> Incorrect.\n";
                if (!errorDescription.empty()) {
                    std::cout << "  [Detected Misconception: " << errorDescription << "]\n";
                }
            }

            double pBefore = learner.pKnown(sel.focusConcept);

            Observation obs;
            obs.templateId = q.templateId;
            obs.questionText = q.questionText;
            obs.targets = q.targetConcepts;
            obs.answerType = q.answerType;
            obs.difficulty = q.difficulty;
            obs.correct = isCorrect;
            obs.errorName = errorName;
            obs.isRecheck = sel.isRecheck;
            learner.update(obs);
            ++askedThisRun;

            double pAfter = learner.pKnown(sel.focusConcept);
            if (opt.verbose) {
                std::cout << "  [P(known) of '" << sel.focusConcept << "': " << std::fixed << std::setprecision(3)
                          << pBefore << " -> " << pAfter << "]\n";
            }

            appendLog(opt.logPath, number, sel, isCorrect, errorName,
                      studentValue.toDisplayString(), q.correctValue.toDisplayString(), pBefore, pAfter);
            saveOrWarn();
        }
    } catch (const InputClosed&) {
        std::cout << "\nInput closed. Progress saved to '" << opt.statePath << "'.\n";
        saveOrWarn();
        return 2;
    } catch (const QuitRequested&) {
        std::cout << "\nStopped by request. Progress saved to '" << opt.statePath << "'.\n";
        saveOrWarn();
        learner.printReport(theory);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "\nFatal error during assessment: " << ex.what() << "\n";
        saveOrWarn();
        return 1;
    }

    if (exitCode == 0) std::cout << "\nMASTERY CERTIFICATION ACHIEVED!\n";
    learner.printReport(theory);
    return exitCode;
}
