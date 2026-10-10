#include "TemplateValidator.hpp"
#include <z3++.h>
#include <sstream>
#include <regex>
#include <iostream>
#include <cmath>

using namespace std;

ValidationResult TemplateValidator::validateTemplate(const DynamicTemplate& tmpl, const FormalTheory& theory, std::set<std::string>& seenIds) {
    ValidationResult res;
    res.isValid = true;
    auto addError = [&](const string& e) { res.isValid = false; res.errors.push_back(e); };

    if (tmpl.id.empty()) addError("Template ID is empty");
    else {
        if (seenIds.count(tmpl.id)) addError("Duplicate template ID: " + tmpl.id);
        seenIds.insert(tmpl.id);
    }

    if (tmpl.targetConcepts.empty()) addError("No target concepts");
    for (const auto& c : tmpl.targetConcepts) {
        if (theory.concepts.find(c) == theory.concepts.end()) {
            addError("Target concept not found in theory: " + c);
        }
    }

    set<string> varNames;
    unsigned bitWidthHint = 6;
    for (const auto& v : tmpl.variables) {
        if (v.name.empty()) addError("Variable has empty name");
        else if (varNames.count(v.name)) addError("Duplicate variable name: " + v.name);
        varNames.insert(v.name);
        
        if (v.sort != "Int" && v.sort != "Real" && v.sort != "Bool" && v.sort != "BitVec") {
            addError("Unsupported sort for variable " + v.name + ": " + v.sort);
        }
        if (v.sort == "BitVec") {
            if (v.width == 0) addError("BitVec variable " + v.name + " has 0 width");
            else bitWidthHint = v.width;
        }
    }

    if (tmpl.questionText.empty()) addError("question_text is empty");

    if (!(tmpl.difficulty >= 0.0 && tmpl.difficulty <= 1.0)) {
        addError("difficulty must be between 0 and 1");
    }

    if (tmpl.constraints.empty()) addError("No constraints provided");

    if (tmpl.correctAnswerExpr.empty()) addError("correct_answer_expr is empty");
    
    if (tmpl.answerType != "Int" && tmpl.answerType != "Real" && tmpl.answerType != "Bool" && tmpl.answerType != "Set") {
        addError("Unsupported answer_type: " + tmpl.answerType);
    }

    // Check placeholders in question_text
    regex re("\\{([^}]+)\\}");
    sregex_iterator next(tmpl.questionText.begin(), tmpl.questionText.end(), re);
    sregex_iterator end;
    while (next != end) {
        smatch match = *next;
        string ph = match[1].str();
        if (!varNames.count(ph)) {
            addError("Placeholder {" + ph + "} in question_text does not match any declared variable");
        }
        next++;
    }

    set<string> mcNames;
    for (const auto& mc : tmpl.misconceptions) {
        if (!mc.name.empty() && !mcNames.insert(mc.name).second) {
            addError("Duplicate misconception name: " + mc.name);
        }
        if (mc.name.empty()) addError("Misconception has empty name");
        if (mc.expr.empty()) addError("Misconception " + mc.name + " has empty expr");
        if (mc.description.empty()) addError("Misconception " + mc.name + " has empty description");
    }

    // SMT checks
    if (res.isValid) {
        try {
            z3::config cfg;
            z3::context ctx(cfg);

            ostringstream declsStream;
            for (const auto& v : tmpl.variables) {
                string sortName = v.sort;
                if (v.sort == "BitVec") sortName = "(_ BitVec " + to_string(v.width) + ")";
                declsStream << "(declare-fun " << v.name << " () " << sortName << ")\n";
            }
            string decls = declsStream.str();

            ostringstream constrStream;
            constrStream << decls;
            for (const auto& c : tmpl.constraints) {
                constrStream << "(assert " << c << ")\n";
            }

            z3::expr_vector assertions(ctx);
            try {
                assertions = ctx.parse_string(constrStream.str().c_str());
            } catch (const z3::exception& e) {
                addError(string("Constraints parsing failed: ") + e.msg());
            }

            if (res.isValid) {
                z3::solver solver(ctx);
                solver.set("timeout", 2000u);
                for (unsigned i = 0; i < assertions.size(); ++i) solver.add(assertions[i]);

                z3::check_result cr = solver.check();
                if (cr != z3::sat) {
                    addError(string("Constraints are ") + (cr == z3::unknown ? "unknown (timeout?)" : "unsatisfiable"));
                }
            }

            // Check correct_answer_expr parseability
            auto checkExpr = [&](const string& expr, const string& errorPrefix) {
                try {
                    string ansSort = tmpl.answerType;
                    if (ansSort == "Set") ansSort = "(_ BitVec " + to_string(bitWidthHint) + ")";
                    string ansDecl = "(declare-fun __DUMMY__ () " + ansSort + ")\n";
                    ctx.parse_string((ansDecl + decls + "(assert (= __DUMMY__ " + expr + "))").c_str());
                } catch (const z3::exception& e) {
                    addError(errorPrefix + " parsing failed: " + e.msg());
                }
            };

            checkExpr(tmpl.correctAnswerExpr, "correct_answer_expr");

            for (const auto& mc : tmpl.misconceptions) {
                checkExpr(mc.expr, string("Misconception '") + mc.name + "' expr");
            }
        } catch (const z3::exception& ez) {
            addError(string("Z3 context error: ") + ez.msg());
        } catch (const exception& ex) {
            addError(string("Z3 context error: ") + ex.what());
        }
    }

    return res;
}

ValidationResult TemplateValidator::validateConcepts(const FormalTheory& theory) {
    ValidationResult res;
    res.isValid = true;
    auto bad = [&](const string& c, const string& msg) {
        res.isValid = false;
        res.errors.push_back("Concept '" + c + "': " + msg);
    };
    for (const auto& pair : theory.concepts) {
        const BktParams& b = pair.second.bkt;
        if (!(b.pInit > 0.0 && b.pInit < 1.0)) bad(pair.first, "bkt.p_init must be between 0 and 1 (exclusive)");
        if (!(b.pLearn >= 0.0 && b.pLearn < 1.0)) bad(pair.first, "bkt.p_learn must be in [0, 1)");
        if (!(b.pSlip >= 0.0 && b.pSlip < 0.5)) bad(pair.first, "bkt.p_slip must be in [0, 0.5)");
        if (!(b.pGuess >= 0.0 && b.pGuess <= 0.5)) bad(pair.first, "bkt.p_guess must be in [0, 0.5]");
    }
    return res;
}

ValidationResult TemplateValidator::validateDependencies(const FormalTheory& theory) {
    ValidationResult res;
    res.isValid = true;
    for (const auto& pair : theory.dependencyGraph) {
        for (const auto& dep : pair.second) {
            if (!theory.concepts.count(pair.first)) {
                res.warnings.push_back("Dependency " + pair.first + " -> " + dep +
                                       ": unknown prerequisite concept '" + pair.first + "' (edge ignored)");
            }
            if (!theory.concepts.count(dep)) {
                res.warnings.push_back("Dependency " + pair.first + " -> " + dep +
                                       ": unknown dependent concept '" + dep + "' (edge ignored)");
            }
        }
    }
    std::vector<std::string> cycle = theory.findDependencyCycle();
    if (!cycle.empty()) {
        res.isValid = false;
        string path;
        for (size_t i = 0; i < cycle.size(); ++i) path += (i ? " -> " : "") + cycle[i];
        res.errors.push_back("Dependency cycle: " + path);
    }
    return res;
}

ValidationResult TemplateValidator::validateTheory(const FormalTheory& theory) {
    ValidationResult res;
    res.isValid = true;
    {
        ValidationResult cr = validateConcepts(theory);
        if (!cr.isValid) {
            res.isValid = false;
            for (const auto& e : cr.errors) res.errors.push_back(e);
        }
    }
    set<string> seenIds;
    for (const auto& tmpl : theory.templates) {
        auto tr = validateTemplate(tmpl, theory, seenIds);
        if (!tr.isValid) {
            res.isValid = false;
            for (const auto& e : tr.errors) {
                res.errors.push_back("Template " + (tmpl.id.empty() ? "<unknown>" : tmpl.id) + ": " + e);
            }
        }
    }
    return res;
}


