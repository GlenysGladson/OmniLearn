#include <iostream>
#include <cassert>
#include <vector>
#include "../TemplateValidator.hpp"
#include "../Theory.hpp"

using namespace std;

void runTests() {
    FormalTheory theory;
    theory.concepts["TestConcept"] = { "TestConcept", ConceptCategory::OPERATION, true };

    auto testValidator = [&](DynamicTemplate tmpl, bool expectedValid, const string& testName) {
        set<string> seenIds;
        ValidationResult res = TemplateValidator::validateTemplate(tmpl, theory, seenIds);
        if (res.isValid != expectedValid) {
            cerr << "FAIL: " << testName << ". Expected isValid=" << expectedValid << ", got " << res.isValid << endl;
            if (!res.isValid) {
                for (const auto& e : res.errors) cerr << "  Error: " << e << endl;
            }
            exit(1);
        } else {
            cout << "PASS: " << testName << endl;
        }
    };

    {
        DynamicTemplate tmpl;
        tmpl.id = "valid_01";
        tmpl.targetConcepts = {"TestConcept"};
        tmpl.questionText = "What is {a} + {b}?";
        tmpl.variables = { {"a", "Int", 0}, {"b", "Int", 0} };
        tmpl.constraints = { "(> a 0)", "(> b 0)" };
        tmpl.correctAnswerExpr = "(+ a b)";
        tmpl.answerType = "Int";
        testValidator(tmpl, true, "Valid template");
    }

    {
        DynamicTemplate tmpl;
        tmpl.id = "dup_var";
        tmpl.targetConcepts = {"TestConcept"};
        tmpl.questionText = "What is {a}?";
        tmpl.variables = { {"a", "Int", 0}, {"a", "Int", 0} };
        tmpl.constraints = { "(> a 0)" };
        tmpl.correctAnswerExpr = "a";
        tmpl.answerType = "Int";
        testValidator(tmpl, false, "Duplicate variable");
    }

    {
        DynamicTemplate tmpl;
        tmpl.id = "unknown_var_expr";
        tmpl.targetConcepts = {"TestConcept"};
        tmpl.questionText = "What is {a} + {b}?";
        tmpl.variables = { {"a", "Int", 0} };
        tmpl.constraints = { "(> a 0)" };
        tmpl.correctAnswerExpr = "(+ a b)"; // b is undeclared
        tmpl.answerType = "Int";
        testValidator(tmpl, false, "Unknown variable in correct_answer_expr");
    }

    {
        DynamicTemplate tmpl;
        tmpl.id = "unknown_concept";
        tmpl.targetConcepts = {"NonExistentConcept"};
        tmpl.questionText = "What is {a}?";
        tmpl.variables = { {"a", "Int", 0} };
        tmpl.constraints = { "(> a 0)" };
        tmpl.correctAnswerExpr = "a";
        tmpl.answerType = "Int";
        testValidator(tmpl, false, "Unknown concept");
    }

    {
        DynamicTemplate tmpl;
        tmpl.id = "invalid_placeholder";
        tmpl.targetConcepts = {"TestConcept"};
        tmpl.questionText = "What is {c}?"; // c is not a variable
        tmpl.variables = { {"a", "Int", 0} };
        tmpl.constraints = { "(> a 0)" };
        tmpl.correctAnswerExpr = "a";
        tmpl.answerType = "Int";
        testValidator(tmpl, false, "Invalid placeholder");
    }

    {
        DynamicTemplate tmpl;
        tmpl.id = "invalid_answer_expr";
        tmpl.targetConcepts = {"TestConcept"};
        tmpl.questionText = "What is {a}?";
        tmpl.variables = { {"a", "Int", 0} };
        tmpl.constraints = { "(> a 0)" };
        tmpl.correctAnswerExpr = "(++ a)"; // invalid syntax
        tmpl.answerType = "Int";
        testValidator(tmpl, false, "Invalid answer expression");
    }

    {
        DynamicTemplate tmpl;
        tmpl.id = "unsupported_answer_type";
        tmpl.targetConcepts = {"TestConcept"};
        tmpl.questionText = "What is {a}?";
        tmpl.variables = { {"a", "Int", 0} };
        tmpl.constraints = { "(> a 0)" };
        tmpl.correctAnswerExpr = "a";
        tmpl.answerType = "Float32"; // unsupported
        testValidator(tmpl, false, "Unsupported answer type");
    }

    {
        DynamicTemplate tmpl;
        tmpl.id = "unsat_constraints";
        tmpl.targetConcepts = {"TestConcept"};
        tmpl.questionText = "What is {a}?";
        tmpl.variables = { {"a", "Int", 0} };
        tmpl.constraints = { "(> a 5)", "(< a 2)" }; // UNSAT
        tmpl.correctAnswerExpr = "a";
        tmpl.answerType = "Int";
        testValidator(tmpl, false, "UNSAT constraints");
    }
}

int main() {
    runTests();
    cout << "All validation tests passed." << endl;
    return 0;
}
