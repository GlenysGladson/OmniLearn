#include "GenericExprEngine.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <map>

namespace {

const unsigned kSolverTimeoutMs = 5000;

// Z3 is deterministic for a fixed constraint set -- without reseeding,
// every instantiation of the same template returns the exact same model
// (verified: without this, a set-theory template kept returning the same
// A/B every single time). Re-randomizing the SAT/SMT search seed per call
// gives each generated question genuinely different concrete values.
void reseedSolverRandomness() {
    std::string seed = std::to_string(rand() % 1000000);
    z3::set_param("sat.random_seed", seed.c_str());
    z3::set_param("smt.random_seed", seed.c_str());
}

std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
    return r;
}

z3::sort resolveVariableSort(z3::context& ctx, const VariableDef& v) {
    if (v.sort == "Int") return ctx.int_sort();
    if (v.sort == "Real") return ctx.real_sort();
    if (v.sort == "Bool") return ctx.bool_sort();
    if (v.sort == "BitVec") {
        if (v.width == 0) {
            throw TemplateError("Variable '" + v.name + "' declares sort BitVec but has no positive width.");
        }
        return ctx.bv_sort(v.width);
    }
    throw TemplateError("Variable '" + v.name + "' has unsupported sort '" + v.sort +
                         "'. Supported sorts: Int, Real, Bool, BitVec.");
}

z3::sort resolveAnswerSort(z3::context& ctx, const std::string& answerType, unsigned setWidthHint) {
    if (answerType == "Int") return ctx.int_sort();
    if (answerType == "Real") return ctx.real_sort();
    if (answerType == "Bool") return ctx.bool_sort();
    if (answerType == "Set") return ctx.bv_sort(setWidthHint);
    throw TemplateError("Unsupported answer_type '" + answerType + "'. Supported: Int, Real, Bool, Set.");
}

std::string smtDeclare(const VariableDef& v) {
    std::string sortName;
    if (v.sort == "BitVec") sortName = "(_ BitVec " + std::to_string(v.width) + ")";
    else sortName = v.sort;
    return "(declare-fun " + v.name + " () " + sortName + ")";
}

std::vector<int> decodeBits(unsigned long long value, unsigned width) {
    std::vector<int> out;
    for (unsigned i = 0; i < width; ++i) {
        if ((value >> i) & 1ULL) out.push_back(static_cast<int>(i) + 1);
    }
    return out;
}

ConcreteValue z3ValueToConcrete(z3::model& m, const z3::expr& value,
                                 const std::string& answerType, unsigned bitWidth) {
    z3::expr v = m.eval(value, true);
    ConcreteValue cv;
    cv.sort = answerType;
    if (answerType == "Int") {
        cv.intValue = v.get_numeral_int64();
    } else if (answerType == "Real") {
        cv.realValue = std::stod(v.get_decimal_string(10));
    } else if (answerType == "Bool") {
        cv.boolValue = v.is_true();
    } else if (answerType == "Set") {
        cv.setValue = decodeBits(v.get_numeral_uint64(), bitWidth);
        std::sort(cv.setValue.begin(), cv.setValue.end());
    }
    return cv;
}

// Evaluates `exprStr` against the already-solved model `m`, reusing the
// exact z3::expr objects already declared for the template's variables, by
// pinning every variable to its concrete value from `m` and solving a
// small fresh system for a dedicated result constant. This avoids
// re-parsing variable declarations or hand-serializing numeric literals.
ConcreteValue evaluateExpr(z3::context& ctx, z3::model& m,
                           const std::string& exprStr,
                           const std::string& answerType,
                           unsigned setWidthHint,
                           const std::map<std::string, z3::expr>& varConsts,
                           const std::vector<z3::func_decl>& varDecls) {
    z3::sort resultSort = resolveAnswerSort(ctx, answerType, setWidthHint);
    z3::expr resultConst = ctx.constant("__RESULT__", resultSort);

    z3::func_decl_vector decls(ctx);
    for (const auto& d : varDecls) decls.push_back(d);
    decls.push_back(resultConst.decl());
    z3::sort_vector sorts(ctx);

    std::string wrapped = "(assert (= __RESULT__ " + exprStr + "))";
    z3::expr_vector parsed(ctx);
    try {
        parsed = ctx.parse_string(wrapped.c_str(), sorts, decls);
    } catch (const z3::exception& e) {
        throw TemplateError(std::string("Failed to parse expression '") + exprStr + "': " + e.msg());
    }

    z3::solver tmp(ctx);
    tmp.set("timeout", kSolverTimeoutMs);
    for (const auto& kv : varConsts) {
        tmp.add(kv.second == m.eval(kv.second, true));
    }
    for (unsigned i = 0; i < parsed.size(); ++i) tmp.add(parsed[i]);

    z3::check_result r = tmp.check();
    if (r != z3::sat) {
        throw TemplateError("Expression '" + exprStr + "' could not be evaluated (solver returned " +
                             (r == z3::unknown ? "unknown/timeout" : "unsat") + ").");
    }
    z3::model m2 = tmp.get_model();
    return z3ValueToConcrete(m2, resultConst, answerType, setWidthHint);
}

std::string renderValue(const VariableDef& v, z3::model& m, const z3::expr& value) {
    z3::expr ev = m.eval(value, true);
    if (v.sort == "Int") return std::to_string(ev.get_numeral_int64());
    if (v.sort == "Real") return ev.get_decimal_string(6);
    if (v.sort == "Bool") return ev.is_true() ? "true" : "false";
    if (v.sort == "BitVec") {
        auto bits = decodeBits(ev.get_numeral_uint64(), v.width);
        std::ostringstream oss;
        oss << "{";
        for (size_t i = 0; i < bits.size(); ++i) {
            if (i) oss << ", ";
            oss << bits[i];
        }
        oss << "}";
        return oss.str();
    }
    return "?";
}

} // namespace

std::string ConcreteValue::toDisplayString() const {
    if (sort == "Int") return std::to_string(intValue);
    if (sort == "Real") {
        std::ostringstream oss;
        oss << realValue;
        return oss.str();
    }
    if (sort == "Bool") return boolValue ? "true" : "false";
    if (sort == "Set") {
        std::ostringstream oss;
        oss << "{";
        for (size_t i = 0; i < setValue.size(); ++i) {
            if (i) oss << ", ";
            oss << setValue[i];
        }
        oss << "}";
        return oss.str();
    }
    return "?";
}

bool concreteValuesEqual(const ConcreteValue& a, const ConcreteValue& b) {
    if (a.sort != b.sort) return false;
    if (a.sort == "Int") return a.intValue == b.intValue;
    if (a.sort == "Real") return std::fabs(a.realValue - b.realValue) < 1e-6;
    if (a.sort == "Bool") return a.boolValue == b.boolValue;
    if (a.sort == "Set") return a.setValue == b.setValue; // both pre-sorted
    return false;
}

void GenericExprEngine::rejectUnsupportedConstructs(const std::string& raw_expr) {
    std::string lower = toLower(raw_expr);
    if (lower.find("forall") != std::string::npos || lower.find("exists") != std::string::npos) {
        throw TemplateError("Expression contains a quantifier ('forall'/'exists'), which is outside "
                             "this engine's guaranteed quantifier-free fragment: '" + raw_expr + "'");
    }
}

GeneratedQuestion GenericExprEngine::generateInstance(const DynamicTemplate& tmpl,
                                                       const std::set<std::string>& excludeQuestionTexts) {
    if (tmpl.answerType.empty()) {
        throw TemplateError("Template '" + tmpl.id + "' has no answer_type declared.");
    }
    if (tmpl.correctAnswerExpr.empty()) {
        throw TemplateError("Template '" + tmpl.id + "' has no correct_answer_expr.");
    }

    rejectUnsupportedConstructs(tmpl.correctAnswerExpr);
    for (const auto& c : tmpl.constraints) rejectUnsupportedConstructs(c);
    for (const auto& mc : tmpl.misconceptions) rejectUnsupportedConstructs(mc.expr);

    reseedSolverRandomness();
    z3::config cfg;
    z3::context ctx(cfg);

    // 1. Declare every variable and assert every constraint via Z3's own
    // SMT-LIB2 parser -- no hand-written operator interpreter involved.
    std::ostringstream program;
    for (const auto& v : tmpl.variables) program << smtDeclare(v) << "\n";
    for (const auto& c : tmpl.constraints) program << "(assert " << c << ")\n";

    z3::expr_vector assertions(ctx);
    try {
        assertions = ctx.parse_string(program.str().c_str());
    } catch (const z3::exception& e) {
        throw TemplateError("Template '" + tmpl.id + "': failed to parse variables/constraints: " + e.msg());
    }

    z3::solver solver(ctx);
    solver.set("timeout", kSolverTimeoutMs);
    for (unsigned i = 0; i < assertions.size(); ++i) solver.add(assertions[i]);

    z3::check_result cr = solver.check();
    if (cr != z3::sat) {
        throw TemplateError("Template '" + tmpl.id + "': constraints are " +
                             (cr == z3::unknown ? "not solvable within the timeout (unknown)" : "unsatisfiable") +
                             " -- check the constraints for this template.");
    }
    z3::model m = solver.get_model();

    // 2. Collect the concrete z3::expr for each variable (reused when
    // evaluating derived expressions) and the "Set"-answer bit width hint.
    std::map<std::string, z3::expr> varConsts;
    std::vector<z3::func_decl> varDecls;
    unsigned setWidthHint = 6;
    bool haveWidthHint = false;
    for (const auto& v : tmpl.variables) {
        z3::sort s = resolveVariableSort(ctx, v);
        z3::expr c = ctx.constant(v.name.c_str(), s);
        varConsts.emplace(v.name, c);
        varDecls.push_back(c.decl());
        if (v.sort == "BitVec" && !haveWidthHint) {
            setWidthHint = v.width;
            haveWidthHint = true;
        }
    }

    // 3. Render the question text for a given model, substituting {name}
    // placeholders and auto-appending a "Given: ..." line for any variable
    // the template text never mentions -- so a value is never asked about
    // without ever being shown to the student.
    auto renderFor = [&](z3::model& model) {
        std::string rendered = tmpl.questionText;
        std::vector<std::string> unmentioned;
        for (const auto& v : tmpl.variables) {
            std::string placeholder = "{" + v.name + "}";
            std::string valueStr = renderValue(v, model, varConsts.at(v.name));
            size_t pos = rendered.find(placeholder);
            bool mentioned = (pos != std::string::npos);
            while (pos != std::string::npos) {
                rendered.replace(pos, placeholder.length(), valueStr);
                pos = rendered.find(placeholder);
            }
            if (!mentioned) unmentioned.push_back(v.name + " = " + valueStr);
        }
        if (!unmentioned.empty()) {
            rendered += "\nGiven: ";
            for (size_t i = 0; i < unmentioned.size(); ++i) {
                if (i) rendered += ", ";
                rendered += unmentioned[i];
            }
        }
        return rendered;
    };

    // 4. Keep re-solving (via blocking clauses) until a not-yet-used
    // rendered question is found, the problem runs out of distinct
    // assignments, or the retry cap is hit.
    const int kMaxDiversityRetries = 25;
    std::string rendered = renderFor(m);
    for (int attempt = 0; attempt < kMaxDiversityRetries; ++attempt) {
        if (excludeQuestionTexts.find(rendered) == excludeQuestionTexts.end()) break;

        z3::expr_vector diffTerms(ctx);
        for (const auto& kv : varConsts) diffTerms.push_back(kv.second != m.eval(kv.second, true));
        solver.add(z3::mk_or(diffTerms));

        z3::check_result cr2 = solver.check();
        if (cr2 != z3::sat) break; // no more distinct assignments exist -- use what we have
        m = solver.get_model();
        rendered = renderFor(m);
    }

    GeneratedQuestion q;
    q.templateId = tmpl.id;
    q.answerType = tmpl.answerType;
    q.targetConcepts = tmpl.targetConcepts;
    q.questionText = rendered;

    // 5. Evaluate the correct answer and every misconception's outcome
    // against the same concrete instantiation.
    q.correctValue = evaluateExpr(ctx, m, tmpl.correctAnswerExpr, tmpl.answerType,
                                   setWidthHint, varConsts, varDecls);

    for (const auto& mc : tmpl.misconceptions) {
        GeneratedQuestion::MisconceptionOutcome outcome;
        outcome.description = mc.description;
        outcome.value = evaluateExpr(ctx, m, mc.expr, tmpl.answerType, setWidthHint, varConsts, varDecls);
        q.misconceptions.push_back(outcome);
    }

    return q;
}