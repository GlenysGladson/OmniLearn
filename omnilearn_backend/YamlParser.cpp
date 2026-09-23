#include "YamlParser.hpp"
#include <yaml-cpp/yaml.h>
#include <stdexcept>

namespace {

ConceptCategory parseCategory(const std::string& s) {
    if (s == "OPERATION") return ConceptCategory::OPERATION;
    if (s == "RELATION") return ConceptCategory::RELATION;
    if (s == "PROPERTY") return ConceptCategory::PROPERTY;
    if (s == "RULE") return ConceptCategory::RULE;
    if (s == "PROBABILITY") return ConceptCategory::PROBABILITY;
    if (s == "GRAPH_THEORY") return ConceptCategory::GRAPH_THEORY;
    return ConceptCategory::DATATYPE;
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
            std::string catStr = node["category"] ? node["category"].as<std::string>() : "DATATYPE";
            bool assessable = node["assessable"] ? node["assessable"].as<bool>() : true;
            theory.addConcept(name, parseCategory(catStr), assessable);
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
                }
            }

            if (node["constraints"]) {
                for (const auto& cNode : node["constraints"]) {
                    tmpl.constraints.push_back(cNode.as<std::string>());
                }
            }

            if (!node["correct_answer_expr"]) {
                throw std::runtime_error(filepath + ": template '" + tmpl.id +
                                          "' is missing 'correct_answer_expr'");
            }
            tmpl.correctAnswerExpr = node["correct_answer_expr"].as<std::string>();

            if (!node["answer_type"]) {
                throw std::runtime_error(filepath + ": template '" + tmpl.id + "' is missing 'answer_type'");
            }
            tmpl.answerType = node["answer_type"].as<std::string>();

            if (node["misconceptions"]) {
                for (const auto& errNode : node["misconceptions"]) {
                    MisconceptionDef mc;
                    if (!errNode["name"] || !errNode["expr"] || !errNode["description"]) {
                        throw std::runtime_error(filepath + ": template '" + tmpl.id +
                                                  "' has a misconception missing 'name'/'expr'/'description'");
                    }
                    mc.name = errNode["name"].as<std::string>();
                    mc.expr = errNode["expr"].as<std::string>();
                    mc.description = errNode["description"].as<std::string>();
                    tmpl.misconceptions.push_back(mc);
                }
            }

            theory.templates.push_back(tmpl);
        }
    }

    return theory;
}