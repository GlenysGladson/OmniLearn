#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include "LearnerModel.hpp"
#include "TemplateEngine.hpp"
#include "Theory.hpp"
#include "YamlParser.hpp"
#include "GenericExprEngine.hpp"

namespace fs = std::filesystem;

// Scans `theoriesDir` for every *.yaml / *.yml file, parses each one, then
// validates every template in it by actually attempting one real
// instantiation through GenericExprEngine. A template that fails
// validation (unsupported construct, Z3 parse error, unsatisfiable/unknown
// constraints, bad declaration) is dropped and reported -- never left to
// crash the program later, mid-assessment. Successfully validated
// theories are folded into one combined FormalTheory via
// FormalTheory::merge(), so LearnerModel/TemplateEngine run over every
// loaded subject at once.
//
// Files are processed in sorted filename order for reproducible load
// order (relevant for which definition wins on a concept-name collision).
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

    for (const auto& path : yamlFiles) {
        std::cout << "Loading theory definition from: " << path.string() << "\n";

        FormalTheory theory;
        try {
            theory = YamlParser::parseTheoryFile(path.string());
        } catch (const std::exception& ex) {
            std::cerr << "[SKIP FILE] " << path.string() << ": " << ex.what() << "\n";
            continue;
        }

        // Validate every template by attempting one real instantiation.
        FormalTheory validated(theory.theoryName);
        validated.concepts = theory.concepts;
        validated.dependencyGraph = theory.dependencyGraph;

        int validCount = 0, invalidCount = 0;
        for (const auto& tmpl : theory.templates) {
            try {
                GenericExprEngine::generateInstance(tmpl);
                validated.templates.push_back(tmpl);
                ++validCount;
            } catch (const std::exception& ex) {
                std::cerr << "[VALIDATION ERROR] " << path.string() << ", template '"
                          << tmpl.id << "': " << ex.what() << "\n";
                ++invalidCount;
            }
        }

        std::cout << "  -> '" << theory.theoryName << "': " << theory.concepts.size()
                  << " concept(s), " << validCount << " valid template(s)";
        if (invalidCount > 0) std::cout << ", " << invalidCount << " rejected";
        std::cout << "\n";

        combined.merge(validated);
    }

    if (combined.templates.empty()) {
        throw std::runtime_error("No valid templates were loaded from any theory file in: " + theoriesDir);
    }

    combined.warnOnUnreachableConcepts();
    return combined;
}

namespace {

std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
    return r;
}

// Prompts for, reads, and parses the student's answer according to
// answerType -- this is the one place answer *shape* is interpreted, and
// it's driven entirely by what the template declared, not by which
// theory the template happened to come from.
ConcreteValue promptForAnswer(const std::string& answerType) {
    ConcreteValue v;
    v.sort = answerType;
    std::string input;

    if (answerType == "Set") {
        std::cout << "Enter answer (comma-separated, e.g. 1,2,3): ";
        std::getline(std::cin, input);
        std::stringstream ss(input);
        std::string item;
        while (std::getline(ss, item, ',')) {
            try { v.setValue.push_back(std::stoi(item)); } catch (...) {}
        }
        std::sort(v.setValue.begin(), v.setValue.end());
    } else if (answerType == "Bool") {
        std::cout << "Enter answer (true/false): ";
        std::getline(std::cin, input);
        std::string lower = toLower(input);
        v.boolValue = (lower == "true" || lower == "1");
    } else if (answerType == "Real") {
        std::cout << "Enter answer: ";
        std::getline(std::cin, input);
        try { v.realValue = std::stod(input); } catch (...) { v.realValue = 0.0; }
    } else { // "Int" (also the fallback for any unrecognized type, scored
             // as a plain integer so a malformed answer_type still fails
             // safely rather than crashing the input loop)
        std::cout << "Enter answer: ";
        std::getline(std::cin, input);
        try { v.intValue = std::stoll(input); } catch (...) { v.intValue = 0; }
    }
    return v;
}

} // namespace

int main(int argc, char* argv[]) {
    srand(static_cast<unsigned int>(time(nullptr)));

    // argv[1], if given, names the directory to scan for theory YAML
    // files. Default: "theories/". Editing or adding a .yaml file there
    // and restarting the program is all that's needed to pick it up --
    // no source change, and no cmake/make re-run.
    std::string theoriesDir = (argc > 1) ? argv[1] : "theories/";

    FormalTheory theory;
    try {
        theory = loadAllTheories(theoriesDir);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "\nLoaded " << theory.sourceTheories.size() << " theory file(s), "
              << theory.concepts.size() << " total concept(s), "
              << theory.templates.size() << " total valid template(s).\n";

    LearnerModel learner;
    learner.initialize(theory);
    TemplateEngine engine;
    int questionNumber = 1;

    try {
        while (!learner.isCertified(theory)) {
            std::cout << "\n--- Question " << questionNumber++ << " ---\n";
            GeneratedQuestion q = engine.selectAndInitializeTemplate(learner, theory);
            std::cout << q.questionText << "\n";

            ConcreteValue studentValue = promptForAnswer(q.answerType);
            bool isCorrect = concreteValuesEqual(studentValue, q.correctValue);

            std::string detectedError;
            if (!isCorrect) {
                for (const auto& mc : q.misconceptions) {
                    if (concreteValuesEqual(studentValue, mc.value)) {
                        detectedError = mc.description;
                        break;
                    }
                }
            }

            if (isCorrect) {
                std::cout << "> Correct!\n";
            } else {
                std::cout << "> Incorrect.\n";
                if (!detectedError.empty()) {
                    std::cout << "  [Detected Misconception: " << detectedError << "]\n";
                }
            }

            learner.update(q.targetConcepts, isCorrect, detectedError);
        }
    } catch (const std::exception& ex) {
        // Covers "no templates available" and any unexpected solver
        // failure mid-assessment -- reported cleanly instead of an
        // uncaught exception aborting the process.
        std::cerr << "\nFatal error during assessment: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "\nMASTERY CERTIFICATION ACHIEVED!\n";
    learner.printReport(theory);
    return 0;
}