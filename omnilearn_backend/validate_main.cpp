#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include "YamlParser.hpp"
#include "TemplateValidator.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <theories_dir>\n";
        return 1;
    }

    std::string theoriesDir = argv[1];
    if (!fs::exists(theoriesDir) || !fs::is_directory(theoriesDir)) {
        std::cerr << "Directory not found: " << theoriesDir << "\n";
        return 1;
    }

    std::vector<fs::path> yamlFiles;
    for (const auto& entry : fs::directory_iterator(theoriesDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext == ".yaml" || ext == ".yml") yamlFiles.push_back(entry.path());
    }
    std::sort(yamlFiles.begin(), yamlFiles.end());

    if (yamlFiles.empty()) {
        std::cerr << "No .yaml/.yml files found in: " << theoriesDir << "\n";
        return 1;
    }

    bool allValid = true;

    // Pass 1: parse every file.
    std::vector<FormalTheory> parsed;
    for (const auto& path : yamlFiles) {
        std::cout << "Parsing " << path.filename().string() << "...\n";
        try {
            parsed.push_back(YamlParser::parseTheoryFile(path.string()));
        } catch (const std::exception& ex) {
            std::cout << "  FAIL (Exception)\n";
            std::cerr << "    - " << ex.what() << "\n";
            allValid = false;
        }
    }

    if (parsed.empty()) {
        std::cerr << "\nNo theory file parsed successfully.\n";
        return 1;
    }

    // Pass 2: merge everything so cross-file target concepts resolve correctly.
    FormalTheory combined("CombinedTheorySet");
    for (const auto& t : parsed) combined.merge(t);

    // Pass 3: validate the combined theory (concepts/BKT, templates, dependencies).
    std::cout << "\nValidating combined theory (" << parsed.size() << " file(s), "
              << combined.concepts.size() << " concept(s), "
              << combined.templates.size() << " template(s))...\n";

    ValidationResult cr = TemplateValidator::validateConcepts(combined);
    if (!cr.isValid) {
        allValid = false;
        std::cout << "  FAIL (concept/BKT parameters)\n";
        for (const auto& e : cr.errors) std::cerr << "    - " << e << "\n";
    }

    ValidationResult tr = TemplateValidator::validateTheory(combined);
    if (!tr.isValid) {
        allValid = false;
        std::cout << "  FAIL (templates)\n";
        for (const auto& e : tr.errors) std::cerr << "    - " << e << "\n";
    }

    ValidationResult dr = TemplateValidator::validateDependencies(combined);
    for (const auto& w : dr.warnings) std::cerr << "  [WARN] " << w << "\n";
    if (!dr.isValid) {
        allValid = false;
        std::cout << "  FAIL (dependency graph)\n";
        for (const auto& e : dr.errors) std::cerr << "    - " << e << "\n";
    }

    if (allValid) {
        std::cout << "\nAll templates valid!\n";
        return 0;
    } else {
        std::cerr << "\nValidation failed.\n";
        return 1;
    }
}