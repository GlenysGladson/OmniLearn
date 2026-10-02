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

    bool allValid = true;
    for (const auto& path : yamlFiles) {
        std::cout << "Validating " << path.filename().string() << "...\n";
        try {
            FormalTheory theory = YamlParser::parseTheoryFile(path.string());
            ValidationResult res = TemplateValidator::validateTheory(theory);
            if (res.isValid) {
                std::cout << "  PASS\n";
            } else {
                std::cout << "  FAIL\n";
                for (const auto& err : res.errors) {
                    std::cerr << "    - " << err << "\n";
                }
                allValid = false;
            }
        } catch (const std::exception& ex) {
            std::cout << "  FAIL (Exception)\n";
            std::cerr << "    - " << ex.what() << "\n";
            allValid = false;
        }
    }

    if (allValid) {
        std::cout << "\nAll templates valid!\n";
        return 0;
    } else {
        std::cerr << "\nValidation failed for some templates.\n";
        return 1;
    }
}
