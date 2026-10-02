#ifndef YAML_PARSER_HPP
#define YAML_PARSER_HPP

#include "Theory.hpp"
#include <string>

class YamlParser {
public:
    // Parses one theory YAML file into structural data. Throws
    // std::runtime_error on structural problems (missing required fields,
    // wrong types). This is separate from -- and runs before -- the
    // semantic/solver-level validation GenericExprEngine performs when a
    // template is actually instantiated.
    static FormalTheory parseTheoryFile(const std::string& filepath);
};

#endif // YAML_PARSER_HPP