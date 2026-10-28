# OmniLearn — Theory-Driven Adaptive Assessment

Z3-backed adaptive question generation and mastery tracking, driven
entirely by YAML theory files — no domain logic is hardcoded in C++.
Z3 is used generically (via SMT-LIB2 string parsing) to *construct*
valid question instances satisfying a template's constraints; grading
is done in C++ against the concrete model Z3 returned.

## Architecture

- **YAML theory files** (`theories/*.yaml`) define concepts, BKT
  parameters, prerequisite dependencies, and question templates
  (variables, Z3 constraints, correct-answer expression,
  misconceptions) per domain.
- **`YamlParser`** parses each file into a `FormalTheory`.
  `FormalTheory::merge()` combines multiple parsed files into one
  theory, so templates can reference concepts defined in a different
  file (cross-domain prerequisites).
- **`GenericExprEngine`** turns a `DynamicTemplate` into a live Z3
  query (`ctx.parse_string`), finds a satisfying instance, and
  evaluates the correct answer and all misconception answers against
  it — no per-domain operator code.
- **`LearnerModel`** runs Bayesian Knowledge Tracing (BKT) per concept
  (`pInit`, `pLearn`, `pSlip`, `pGuess`) and tracks attempts, recent
  accuracy, and error patterns.
- **`TemplateEngine`** selects the next question: filters to concepts
  whose prerequisites are already mastered, scores remaining concepts
  by weakness, and asks `GenericExprEngine` for an instance.
- **`TemplateValidator`** / **`InstanceValidator`** check theory files
  and generated instances respectively, independent of the generation
  code that produced them.

## Build & run

```bash
sudo apt-get install libz3-dev libyaml-cpp-dev cmake g++
mkdir build && cd build
cmake ..
make
./omnilearn_tests                  # unit tests (TemplateValidator)
./omnilearn_validate ../theories/  # validate all YAML files
./omnilearn_backend ../theories/ --verbose   # run an assessment session
```

`omnilearn_backend` defaults its theories directory to `theories/` if
no argument is given; `omnilearn_validate` requires the directory
argument explicitly. CMake locates Z3 via its own CMake package, with
Linux fallbacks; the build is Linux/WSL-only, native Windows isn't
supported. Configure stops with a clear error if engine source file
names don't match the expected case.

## What's covered

- **Set Theory**: Union, Intersection, Difference (over 6-bit sets).
- **Combinatorics**: counting/permutation templates.
- **Probability**: Inclusion-Exclusion, Conditional Probability —
  gated behind Set Theory (`Union`/`Intersection` must show sufficient
  mastery and recent accuracy before these unlock).

Adding a new template or domain means writing/editing a YAML file —
no C++ changes, as long as the needed SMT theory (BitVec, Int, Real,
Bool) and operators are already things Z3's parser understands, which
covers all current templates.

## Norms / patterns adopted

These are the recurring design rules the project has converged on
while fixing real bugs — stated here so new YAML/templates stay
consistent with them rather than reintroducing the same bugs:

- **No domain logic in C++.** Every operator, constraint, and correct
  answer is an SMT-LIB2 string in YAML, evaluated generically. A
  template needing new question logic is a YAML change, not a C++
  change.
- **Multi-file theories are always parsed, then merged, then
  validated.** Any tool that loads YAML (`main.cpp`'s
  `loadAllTheories`, `validate_main.cpp`) follows parse-all →
  `FormalTheory::merge()` → validate-combined, so cross-file
  prerequisites and targets resolve correctly. A tool that validates
  file-by-file in isolation is considered a bug (this is what
  `validate_main.cpp` had and was fixed for). Template IDs must be
  unique across all files, since merging would otherwise silently
  collide them.
- **Dependency-cycle checks run before anything else can mask them.**
  A cycle in the prerequisite graph is reported as a cycle, not as
  the downstream "no templates available" symptom it used to produce.
- **Certification requires three independent signals, not one.**
  `isMastered()` needs `pKnown ≥ 0.95` AND `attempts ≥ 3` AND
  `recentAccuracy ≥ 0.80` over the last 10 attempts, plus a final
  clean re-check question. A single probabilistic estimate crossing a
  threshold is not treated as sufficient evidence of mastery, and a
  concept whose mastery later drops is returned to the active work
  list rather than staying marked done.
- **No width or magic literal belongs in a constraint string.**
  Width-dependent literals (e.g. a BitVec's zero value) are written as
  `{zero:VAR}` tokens expanded by the parser from that variable's own
  declared `width`, never hand-typed per constraint. This was a real
  bug (hardcoded `(_ bv0 6)` scattered through `set_theory.yaml`) and
  the convention exists specifically to prevent it recurring.
- **Non-trivial instances are a correctness requirement, not
  polish.** A template must constrain out answers that collapse to a
  degenerate case (e.g. Union where one set is a subset of the other,
  Difference where the sets don't overlap). Generating a technically
  "satisfying" but pedagogically meaningless instance is treated as a
  template bug.
- **Misconception answers are checked for collision, not assumed
  distinct.** At generation time, `GenericExprEngine` drops any
  misconception whose value equals the correct answer or an earlier
  kept misconception for that specific instance. At load time, a
  misconception that clashes across all 3 sampled instances is
  dropped from the template entirely, with a warning — so a
  structurally broken misconception doesn't silently vanish on every
  instance without anyone noticing.
- **Every generated answer is independently re-derived.** Before a
  question is shown, `recomputeInFreshContext()` re-evaluates every
  expression in a brand-new Z3 context from the printed variable
  values, to catch evaluation bugs. This catches evaluation/parsing
  slips only — it does not catch a template whose formula answers the
  wrong question, which is a separate, template-correctness concern.
- **Repeated questions are avoided, repeated concepts are not.**
  Before accepting a generated instance, the engine tries other
  templates for the same concept if the exact question would repeat a
  prior one this session; if none remain, the question is flagged as
  a repeat rather than silently shown. Revisiting the same *concept*
  with different values is intentional (that's how mastery is built);
  repeating the exact same *question* is not.
- **Z3 errors never crash the process.** `z3::exception` does not
  derive from `std::exception`, so an uncaught one would terminate
  the program; all Z3 exceptions are converted to `TemplateError` at
  the boundary.
- **Strict input parsing, no silent defaults.** Answer parsing
  re-prompts on invalid input rather than defaulting to 0/false, so a
  typo is never silently graded as an answer.

## Known limitations (unaddressed, stated plainly)

- No automated pilot or simulated-learner evaluation has been run —
  the BKT parameters and mastery thresholds are design choices, not
  empirically tuned (slip/guess vary by answer type and difficulty,
  but this is not fitted IRT).
- Real-type (fraction) answer input and tolerance checking are
  implemented but currently unused — no Real-typed template exists
  yet to exercise them.
- Set-answer input accepts element values outside a template's actual
  bit-width range rather than validating against it.
- Random instance generation pins variables to feasible values where
  possible, but unbounded Int/Real variables are still left to Z3's
  own (often degenerate) model.
- Z3 context setup (`set_param`) and the random seed are process
  global state — fine for a single-user console app, not safe if the
  engine is ever reused concurrently.
- Only `TemplateValidator` has unit tests; no tests exist for
  `GenericExprEngine`, `LearnerModel`, or `TemplateEngine` selection
  logic.
- MCQ/MSQ answer formats and any LLM-assisted features are deferred
  (no API key provisioned) — not implemented, not partially stubbed.
- "Right answer, wrong understanding" (lucky guesses inflating
  mastery) is not specifically detected beyond what `pGuess`/`pSlip`
  already model.
- No comparison against existing adaptive-assessment tools or
  published systems has been done.

## Files

| File | Role |
|---|---|
| `Theory.hpp/.cpp` | `FormalTheory` — concepts, dependency graph, `merge()`, cycle detection. |
| `YamlParser.hpp/.cpp` | Parses theory YAML into `FormalTheory` + `DynamicTemplate`s. |
| `GenericExprEngine.hpp/.cpp` | Z3-generic instance generation, misconception evaluation, independent re-check. |
| `LearnerModel.hpp/.cpp` | BKT update, mastery/certification check, state save/load. |
| `TemplateEngine.hpp/.cpp` | Next-question selection (prerequisite gating + weakness scoring). |
| `TemplateValidator.hpp/.cpp` | Static validation of templates/concepts/dependencies. |
| `InstanceValidator.hpp/.cpp` | Runtime validation of a generated instance. |
| `main.cpp` | `omnilearn_backend` — the assessment loop. |
| `validate_main.cpp` | `omnilearn_validate` — standalone theory-file validator CLI. |
| `test_validator.cpp` | `omnilearn_tests` — unit tests for `TemplateValidator`. |
| `theories/*.yaml` | Theory content: Set Theory, Combinatorics, Probability. |

## Suggested next steps (not done here — flagging for scope)

- Simulated-learner evaluation to sanity-check BKT parameter choices
  before relying on them for a real cohort.
- Unit tests for `GenericExprEngine` and `TemplateEngine` selection
  logic, not just `TemplateValidator`.
- Bound set-answer element input to the template's actual bit-width.