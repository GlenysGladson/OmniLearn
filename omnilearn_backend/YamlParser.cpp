#include "YamlParser.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {

    bool parseCategory(const std::string& s, ConceptCategory& out) {
        if (s == "DATATYPE")     { out = ConceptCategory::DATATYPE;     return true; }
        if (s == "OPERATION")    { out = ConceptCategory::OPERATION;    return true; }
        if (s == "RELATION")     { out = ConceptCategory::RELATION;     return true; }
        if (s == "PROPERTY")     { out = ConceptCategory::PROPERTY;     return true; }
        if (s == "RULE")         { out = ConceptCategory::RULE;         return true; }
        if (s == "PROBABILITY")  { out = ConceptCategory::PROBABILITY;  return true; }
        if (s == "GRAPH_THEORY") { out = ConceptCategory::GRAPH_THEORY; return true; }
        return false;
    }

    double readNumber(const YAML::Node& node, const std::string& file, const std::string& what) {
        try {
            return node.as<double>();
        } catch (const std::exception&) {
            throw std::runtime_error(file + ": " + what + " must be a number");
        }
    }

    BktParams parseBkt(const YAML::Node& node, const std::string& file, const std::string& conceptId) {
        BktParams p;
        if (!node.IsMap()) {
            throw std::runtime_error(file + ": concept '" + conceptId + "': 'bkt' must be a map "
                                                                        "(keys: p_init, p_learn, p_slip, p_guess)");
        }
        static const std::set<std::string> known = {"p_init", "p_learn", "p_slip", "p_guess"};
        for (const auto& kv : node) {
            std::string key = kv.first.as<std::string>();
            if (!known.count(key)) {
                std::cerr << "[WARN] " << file << ": concept '" << conceptId
                          << "': unknown bkt key '" << key << "' ignored.\n";
            }
        }
        std::string ctx = "concept '" + conceptId + "' bkt.";
        if (node["p_init"])  p.pInit  = readNumber(node["p_init"],  file, ctx + "p_init");
        if (node["p_learn"]) p.pLearn = readNumber(node["p_learn"], file, ctx + "p_learn");
        if (node["p_slip"])  p.pSlip  = readNumber(node["p_slip"],  file, ctx + "p_slip");
        if (node["p_guess"]) p.pGuess = readNumber(node["p_guess"], file, ctx + "p_guess");
        return p;
    }

// Expands "{zero:NAME}" into "(_ bv0 W)" where W is NAME's own declared
// BitVec width, so a constraint never hardcodes a width that belongs to
// the variable declaration instead. Throws if NAME isn't a declared
// BitVec variable, so a typo fails loudly at load time instead of
// producing a confusing Z3 parse error later.
    std::string substituteZeroLiterals(const std::string& expr, const std::string& filepath,
                                       const std::string& templateId,
                                       const std::map<std::string, unsigned>& bitWidths) {
        std::string result = expr;
        size_t pos = 0;
        while ((pos = result.find("{zero:", pos)) != std::string::npos) {
            size_t end = result.find('}', pos);
            if (end == std::string::npos) {
                throw std::runtime_error(filepath + ": template '" + templateId +
                                         "': unterminated '{zero:...}' placeholder");
            }
            std::string varName = result.substr(pos + 6, end - (pos + 6));
            auto it = bitWidths.find(varName);
            if (it == bitWidths.end()) {
                throw std::runtime_error(filepath + ": template '" + templateId + "': '{zero:" + varName +
                                         "}' refers to '" + varName + "', which is not a declared BitVec variable");
            }
            std::string replacement = "(_ bv0 " + std::to_string(it->second) + ")";
            result.replace(pos, end - pos + 1, replacement);
            pos += replacement.size();
        }
        return result;
    }

} // namespace

FormalTheory YamlParser::parseTheoryFile(const std::string& filepath) {
    YAML::Node config = YAML::LoadFile(filepath);

    if (!config["theory_name"]) {
        throw std::runtime_error(filepath + ": missing required field 'theory_name'");
    }
    FormalTheory theory(config["theory_name"].as<std::string>());

    if (config["concepts"]) {
        for (const auto& node : config["concepts"]) {
            if (!node["id"]) throw std::runtime_error(filepath + ": a concept is missing 'id'");
            std::string name = node["id"].as<std::string>();

            ConceptCategory cat = ConceptCategory::DATATYPE;
            if (node["category"]) {
                std::string catStr = node["category"].as<std::string>();
                if (!parseCategory(catStr, cat)) {
                    std::cerr << "[WARN] " << filepath << ": concept '" << name
                              << "' has unknown category '" << catStr << "'; using DATATYPE.\n";
                    cat = ConceptCategory::DATATYPE;
                }
            }
            bool assessable = node["assessable"] ? node["assessable"].as<bool>() : true;
            BktParams bkt;
            if (node["bkt"]) bkt = parseBkt(node["bkt"], filepath, name);
            theory.addConcept(name, cat, assessable, bkt);
        }
    }

    if (config["dependencies"]) {
        for (const auto& node : config["dependencies"]) {
            if (!node["prereq"] || !node["dependent"]) {
                throw std::runtime_error(filepath + ": a dependency entry needs both 'prereq' and 'dependent'");
            }
            theory.addDependency(node["prereq"].as<std::string>(), node["dependent"].as<std::string>());
        }
    }

    if (config["templates"]) {
        for (const auto& node : config["templates"]) {
            DynamicTemplate tmpl;

            if (!node["id"]) throw std::runtime_error(filepath + ": a template is missing 'id'");
            tmpl.id = node["id"].as<std::string>();

            if (!node["question_text"]) {
                throw std::runtime_error(filepath + ": template '" + tmpl.id + "' is missing 'question_text'");
            }
            tmpl.questionText = node["question_text"].as<std::string>();

            if (node["targets"]) {
                for (const auto& t : node["targets"]) tmpl.targetConcepts.push_back(t.as<std::string>());
            }
            if (tmpl.targetConcepts.empty()) {
                throw std::runtime_error(filepath + ": template '" + tmpl.id + "' has no 'targets'");
            }

            std::map<std::string, unsigned> bitWidths;
            if (node["variables"]) {
                for (const auto& vNode : node["variables"]) {
                    VariableDef v;
                    if (!vNode["name"] || !vNode["sort"]) {
                        throw std::runtime_error(filepath + ": template '" + tmpl.id +
                                                 "' has a variable missing 'name' or 'sort'");
                    }
                    v.name = vNode["name"].as<std::string>();
                    v.sort = vNode["sort"].as<std::string>();
                    if (vNode["width"]) v.width = vNode["width"].as<unsigned>();
                    tmpl.variables.push_back(v);
                    if (v.sort == "BitVec") bitWidths[v.name] = v.width;
                }
            }

            if (node["constraints"]) {
                for (const auto& cNode : node["constraints"]) {
                    tmpl.constraints.push_back(
                            substituteZeroLiterals(cNode.as<std::string>(), filepath, tmpl.id, bitWidths));
                }
            }

            if (!node["correct_answer_expr"]) {
                throw std::runtime_error(filepath + ": template '" + tmpl.id +
                                         "' is missing 'correct_answer_expr'");
            }
            tmpl.correctAnswerExpr = substituteZeroLiterals(
                    node["correct_answer_expr"].as<std::string>(), filepath, tmpl.id, bitWidths);

            if (!node["answer_type"]) {
                throw std::runtime_error(filepath + ": template '" + tmpl.id + "' is missing 'answer_type'");
            }
            tmpl.answerType = node["answer_type"].as<std::string>();

            if (node["difficulty"]) {
                tmpl.difficulty = readNumber(node["difficulty"], filepath,
                                             "template '" + tmpl.id + "' difficulty");
            }

            if (node["misconceptions"]) {
                for (const auto& errNode : node["misconceptions"]) {
                    MisconceptionDef mc;
                    if (!errNode["name"] || !errNode["expr"] || !errNode["description"]) {
                        throw std::runtime_error(filepath + ": template '" + tmpl.id +
                                                 "' has a misconception missing 'name'/'expr'/'description'");
                    }
                    mc.name = errNode["name"].as<std::string>();
                    mc.expr = substituteZeroLiterals(errNode["expr"].as<std::string>(), filepath, tmpl.id, bitWidths);
                    mc.description = errNode["description"].as<std::string>();
                    tmpl.misconceptions.push_back(mc);
                }
            }

            theory.templates.push_back(tmpl);
        }
    }

    return theory;
}