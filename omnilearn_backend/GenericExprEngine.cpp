#include "GenericExprEngine.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <random>
#include <sstream>

namespace {

const unsigned kSolverTimeoutMs = 5000;
const unsigned kBoundsTimeoutMs = 300;
const int kMaxDiversityRetries = 25;
const long long kMaxRandomSpan = 2000000;

// Z3 is deterministic for a fixed constraint set. Re-seeding is kept as a
// small extra source of variety. NOTE: z3::set_param is process-global.
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

// Evaluates `exprStr` against the already-solved model `m` by pinning every
// variable to its value from `m` and solving a small fresh system for a
// dedicated result constant (same context as the model).
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

// Independent re-check: a brand-new Z3 context, variables re-declared from
// the template, values re-parsed from their PRINTED form, expression
// re-parsed from text. It shares no Z3 objects with the first evaluation.
// It catches evaluation / printing / parsing slips. It cannot catch a
// formula that is the wrong answer to the question text.
ConcreteValue recomputeInFreshContext(const DynamicTemplate& tmpl, z3::model& m,
                                      const std::map<std::string, z3::expr>& varConsts,
                                      const std::string& exprStr, unsigned setWidthHint) {
    z3::context ctx2;
    z3::func_decl_vector decls(ctx2);
    for (const auto& v : tmpl.variables) {
        z3::expr c = ctx2.constant(v.name.c_str(), resolveVariableSort(ctx2, v));
        decls.push_back(c.decl());
    }
    z3::expr resultConst = ctx2.constant("__RESULT__", resolveAnswerSort(ctx2, tmpl.answerType, setWidthHint));
    decls.push_back(resultConst.decl());
    z3::sort_vector sorts(ctx2);

    z3::solver s(ctx2);
    s.set("timeout", kSolverTimeoutMs);

    try {
        for (const auto& v : tmpl.variables) {
            std::string literal = m.eval(varConsts.at(v.name), true).to_string();
            std::string text = "(assert (= " + v.name + " " + literal + "))";
            z3::expr_vector pinned = ctx2.parse_string(text.c_str(), sorts, decls);
            for (unsigned i = 0; i < pinned.size(); ++i) s.add(pinned[i]);
        }
        std::string wrapped = "(assert (= __RESULT__ " + exprStr + "))";
        z3::expr_vector parsed = ctx2.parse_string(wrapped.c_str(), sorts, decls);
        for (unsigned i = 0; i < parsed.size(); ++i) s.add(parsed[i]);
    } catch (const z3::exception& e) {
        throw TemplateError(std::string("Independent re-check could not parse '") + exprStr + "': " + e.msg());
    }

    z3::check_result r = s.check();
    if (r != z3::sat) {
        throw TemplateError("Independent re-check of '" + exprStr + "' returned " +
                             (r == z3::unknown ? "unknown/timeout" : "unsat") + ".");
    }
    z3::model m2 = s.get_model();
    return z3ValueToConcrete(m2, resultConst, tmpl.answerType, setWidthHint);
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

// ---- random assignment ------------------------------------------------

struct VarBounds {
    bool known = false;
    long long lo = 0;
    long long hi = 0;
};
typedef std::map<std::string, VarBounds> BoundsMap;

// Feasible [min, max] of an Int variable under the template constraints, via
// Z3's optimizer with a short timeout. known == false if unbounded, unknown
// or on any Z3 error: the variable is then left to Z3's own model.
VarBounds probeIntBounds(z3::context& ctx, const z3::expr_vector& assertions, const z3::expr& var) {
    VarBounds b;
    try {
        z3::params p(ctx);
        p.set("timeout", kBoundsTimeoutMs);
        {
            z3::optimize o(ctx);
            o.set(p);
            for (unsigned i = 0; i < assertions.size(); ++i) o.add(assertions[i]);
            z3::optimize::handle h = o.minimize(var);
            if (o.check() != z3::sat) return b;
            z3::expr lo = o.lower(h);
            if (!lo.is_numeral()) return b;
            b.lo = lo.get_numeral_int64();
        }
        {
            z3::optimize o(ctx);
            o.set(p);
            for (unsigned i = 0; i < assertions.size(); ++i) o.add(assertions[i]);
            z3::optimize::handle h = o.maximize(var);
            if (o.check() != z3::sat) return b;
            z3::expr hi = o.upper(h);
            if (!hi.is_numeral()) return b;
            b.hi = hi.get_numeral_int64();
        }
        b.known = (b.lo <= b.hi);
    } catch (const z3::exception&) {
        b.known = false;
    }
    return b;
}

// Bounds depend only on the template, so compute them once per process.
const BoundsMap& boundsFor(const DynamicTemplate& tmpl, z3::context& ctx,
                           const z3::expr_vector& assertions,
                           const std::map<std::string, z3::expr>& varConsts) {
    static std::map<std::string, BoundsMap> cache;
    std::string key = tmpl.id;
    for (const auto& c : tmpl.constraints) key += "|" + c;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    BoundsMap bm;
    for (const auto& v : tmpl.variables) {
        if (v.sort == "Int") bm[v.name] = probeIntBounds(ctx, assertions, varConsts.at(v.name));
    }
    return cache.emplace(key, bm).first->second;
}

// Tries to pin each variable (in random order) to a random value. A pin that
// makes the problem unsat is dropped. Each kept pin leaves one solver scope
// pushed; `depth` counts them so the caller can pop them all.
void randomPins(z3::context& ctx, z3::solver& solver, const DynamicTemplate& tmpl,
                const std::map<std::string, z3::expr>& varConsts, const BoundsMap& bounds,
                std::mt19937_64& rng, unsigned& depth) {
    std::vector<const VariableDef*> order;
    for (const auto& v : tmpl.variables) order.push_back(&v);
    std::shuffle(order.begin(), order.end(), rng);

    for (const VariableDef* v : order) {
        const z3::expr& c = varConsts.at(v->name);
        for (int tries = 0; tries < 4; ++tries) {
            bool have = false;
            z3::expr cand(ctx);
            if (v->sort == "Int") {
                auto it = bounds.find(v->name);
                if (it != bounds.end() && it->second.known &&
                    it->second.hi - it->second.lo < kMaxRandomSpan) {
                    std::uniform_int_distribution<long long> d(it->second.lo, it->second.hi);
                    cand = ctx.int_val(static_cast<int64_t>(d(rng)));
                    have = true;
                }
            } else if (v->sort == "BitVec" && v->width <= 64) {
                uint64_t r = rng();
                if (v->width < 64) r &= ((1ULL << v->width) - 1ULL);
                cand = ctx.bv_val(static_cast<uint64_t>(r), v->width);
                have = true;
            } else if (v->sort == "Bool") {
                cand = ctx.bool_val((rng() & 1ULL) != 0);
                have = true;
            }
            if (!have) break; // Real, or unbounded Int: leave to Z3

            solver.push();
            solver.add(c == cand);
            if (solver.check() == z3::sat) {
                ++depth;
                break;
            }
            solver.pop();
        }
    }
}

struct Evaluated {
    ConcreteValue correct;
    std::vector<GeneratedQuestion::MisconceptionOutcome> kept;
    std::vector<GeneratedQuestion::DroppedMisconception> dropped;
};

GeneratedQuestion generateInstanceImpl(const DynamicTemplate& tmpl,
                                        const std::set<std::string>& excludeQuestionTexts) {
    if (tmpl.answerType.empty()) {
        throw TemplateError("Template '" + tmpl.id + "' has no answer_type declared.");
    }
    if (tmpl.correctAnswerExpr.empty()) {
        throw TemplateError("Template '" + tmpl.id + "' has no correct_answer_expr.");
    }

    GenericExprEngine::rejectUnsupportedConstructs(tmpl.correctAnswerExpr);
    for (const auto& c : tmpl.constraints) GenericExprEngine::rejectUnsupportedConstructs(c);
    for (const auto& mc : tmpl.misconceptions) GenericExprEngine::rejectUnsupportedConstructs(mc.expr);

    reseedSolverRandomness();
    z3::config cfg;
    z3::context ctx(cfg);

    // 1. Declare variables and assert constraints through Z3's own parser.
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

    // 2. Variable constants and the "Set" bit-width hint.
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

    // 3. Render question text for a model; variables the text never mentions
    // are appended as "Given: ..." so no value is used without being shown.
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
            if (!mentioned && v.name != tmpl.correctAnswerExpr) unmentioned.push_back(v.name + " = " + valueStr);
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

    // 4. Evaluate correct answer + misconceptions; drop misconceptions that
    // equal the correct answer or an earlier misconception.
    auto evaluateAll = [&](z3::model& model) {
        Evaluated ev;
        ev.correct = evaluateExpr(ctx, model, tmpl.correctAnswerExpr, tmpl.answerType,
                                  setWidthHint, varConsts, varDecls);
        for (const auto& mc : tmpl.misconceptions) {
            GeneratedQuestion::MisconceptionOutcome out;
            out.name = mc.name;
            out.description = mc.description;
            out.value = evaluateExpr(ctx, model, mc.expr, tmpl.answerType, setWidthHint, varConsts, varDecls);

            if (concreteValuesEqual(out.value, ev.correct)) {
                ev.dropped.push_back({mc.name, "gives the same value as the correct answer"});
                continue;
            }
            bool duplicate = false;
            for (const auto& k : ev.kept) {
                if (concreteValuesEqual(out.value, k.value)) {
                    ev.dropped.push_back({mc.name, "gives the same value as misconception '" + k.name + "'"});
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) ev.kept.push_back(out);
        }
        return ev;
    };

    // 5. Search for an assignment: unused text and no misconception clash is
    // best (rank 3); unused text with a clash is next (2); a repeat is last (1).
    const BoundsMap& bounds = boundsFor(tmpl, ctx, assertions, varConsts);
    std::mt19937_64 rng(static_cast<uint64_t>(rand()) * 2654435761ULL + 12345ULL);

    z3::model chosen = solver.get_model();
    Evaluated chosenEval;
    int chosenRank = 0;

    for (int attempt = 0; attempt < kMaxDiversityRetries; ++attempt) {
        if (attempt > 0 && solver.check() != z3::sat) break; // no more distinct assignments

        unsigned depth = 0;
        randomPins(ctx, solver, tmpl, varConsts, bounds, rng, depth);
        z3::model cand = chosen;
        bool gotModel = (solver.check() == z3::sat);
        if (gotModel) cand = solver.get_model();
        if (depth > 0) solver.pop(depth);
        if (!gotModel) {
            if (solver.check() != z3::sat) break;
            cand = solver.get_model();
        }

        std::string rendered = renderFor(cand);
        bool fresh = excludeQuestionTexts.find(rendered) == excludeQuestionTexts.end();

        int rank = 1;
        Evaluated ev;
        if (fresh || chosenRank == 0) {
            ev = evaluateAll(cand);
            if (fresh) rank = ev.dropped.empty() ? 3 : 2;
        }
        if (rank > chosenRank) {
            chosen = cand;
            chosenEval = ev;
            chosenRank = rank;
        }
        if (rank == 3) break;
        if (tmpl.variables.empty()) break;

        z3::expr_vector diffTerms(ctx);
        for (const auto& kv : varConsts) diffTerms.push_back(kv.second != cand.eval(kv.second, true));
        solver.add(z3::mk_or(diffTerms));
    }

    GeneratedQuestion q;
    q.templateId = tmpl.id;
    q.answerType = tmpl.answerType;
    q.targetConcepts = tmpl.targetConcepts;
    q.difficulty = tmpl.difficulty;
    q.questionText = renderFor(chosen);
    q.isRepeat = (chosenRank == 1);
    q.correctValue = chosenEval.correct;
    q.misconceptions = chosenEval.kept;
    q.droppedMisconceptions = chosenEval.dropped;

    // 6. Independent re-check of the correct answer and every kept misconception.
    ConcreteValue again = recomputeInFreshContext(tmpl, chosen, varConsts, tmpl.correctAnswerExpr, setWidthHint);
    if (!concreteValuesEqual(again, q.correctValue)) {
        throw TemplateError("Template '" + tmpl.id + "': independent re-check disagrees on the correct answer ("
                             + q.correctValue.toDisplayString() + " vs " + again.toDisplayString() + ").");
    }
    for (const auto& mc : q.misconceptions) {
        const std::string* expr = nullptr;
        for (const auto& def : tmpl.misconceptions) if (def.name == mc.name) { expr = &def.expr; break; }
        if (!expr) continue;
        ConcreteValue a2 = recomputeInFreshContext(tmpl, chosen, varConsts, *expr, setWidthHint);
        if (!concreteValuesEqual(a2, mc.value)) {
            throw TemplateError("Template '" + tmpl.id + "': independent re-check disagrees on misconception '"
                                 + mc.name + "'.");
        }
    }
    return q;
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
    try {
        return generateInstanceImpl(tmpl, excludeQuestionTexts);
    } catch (const z3::exception& e) {
        throw TemplateError("Template '" + tmpl.id + "': Z3 error: " + e.msg());
    } catch (const std::out_of_range& e) {
        throw TemplateError("Template '" + tmpl.id + "': internal lookup error: " + e.what());
    } catch (const std::invalid_argument& e) {
        throw TemplateError("Template '" + tmpl.id + "': value conversion error: " + e.what());
    }
}
