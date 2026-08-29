# OmniLearn — Theory-Driven Adaptive Assessment (Set Theory + Probability)

Automatic question generation for mastery-based assessment, using the Z3
SMT solver to *construct* valid question instances (find concrete sets /
probabilities satisfying a template's constraints) rather than to grade
answers. Grading is done directly in C++ against the model Z3 returned.

This implements the framework described in `notes.pdf` / the accompanying
lecture note: a formal theory `T = ⟨D, O, Rel, P, R, G⟩`, a learner model
tracking `Mastery`/`Coverage`/`ErrorPatterns` per concept, and a loop that
generates questions targeting the learner's weakest concepts until every
concept is certified.

## Build & run

```bash
sudo apt-get install libz3-dev cmake g++      # if not already installed
mkdir build && cd build
cmake ..
make
./omnilearn_backend
```

## What's covered

- **Set Theory**: Set, Element, Membership, Subset, Union, Intersection,
  Difference, Complement.
- **Probability**: `P(A)`, `P(B)`, `P(A∩B)` given → student computes
  `P(A∪B)` via Inclusion-Exclusion.

Both areas share one `LearnerModel` and one certification pass — the
assessment doesn't end until *every* concept in *both* areas reaches
`Mastery ≥ 0.8` with `Coverage ≥ 3`, and no error pattern remains
persistent (see "Certification" below).

Properties and Inference Rules (Commutativity, DeMorgan, Union
Introduction, Subset Transitivity, and the Boolean/Equality concepts)
stay in the concept space for completeness but are marked
non-assessable, same as the original design — they need a true/false or
proof-style answer format the current question UI doesn't support, and
including them in certification would make the loop unable to terminate.

## What changed from the uploaded files

The uploaded code already had the right shape (Z3-backed template
generation, dependency-ordered concept selection, mastery/coverage
tracking). Three things kept it from doing what you actually asked for:

1. **Probability was written but never wired in.** `TemplateEngine.hpp`
   explicitly said not to use `generateProbabilityInclusionExclusion()`
   because no `FormalTheory` defined its target concepts. Added
   `FormalTheory::createFullTheory()` (Set Theory + Probability, with
   `Complement → Probability → InclusionExclusion` in the dependency
   graph) and wired the template into `selectAndInitializeTemplate()`.
   `main.cpp` now builds both question formats and prompts/grades each
   correctly (`AnswerFormat::SET_ANSWER` vs `PROBABILITY_ANSWER`).

2. **The probability answer was actually wrong.** The original code left
   `P(A∪B)` as an unconstrained free real, so Z3 could (and did) return
   any value ≤ 1 — unrelated to `P(A)+P(B)-P(A∩B)`. Grading against it
   would have graded students against a nonsense number. Fixed to derive
   `P(A∪B)` directly via Inclusion-Exclusion, with Z3 used to verify the
   result is a valid probability (consistent with how the rest of the
   engine uses Z3 as the constraint-checking layer, not the randomness
   source).

3. **Certification could hang forever.** `isCertified()` requires every
   error pattern's frequency to stay `≤ 2`, but nothing ever reduced that
   count — so a student who made the same slip 3 times could never be
   certified, no matter how many correct answers followed. The
   assessment loop would then spin indefinitely (verified by running an
   adversarial auto-answering script — it hung past 290 questions with
   no path to completion). Fixed by letting a clean correct answer decay
   old error-pattern counts by 1 (`LearnerModel::update`), so persistent
   *really* means persistent, not "ever happened 3 times." Also added a
   hard 200-question safety cap in `main.cpp` so a genuinely-stuck
   session (e.g. a concept the learner truly can't answer) ends with a
   clear "no certificate, here's what needs work" message instead of
   running forever.

Also added, to satisfy "generate one by one, no repetition, only concept
repetition":

- `TemplateEngine` now tracks a signature (question text + exact given
  values) for every question generated this session and retries
  (up to 25 attempts, fresh random seed each time) if Z3/the random draw
  would produce an exact repeat. The same *concept* is deliberately
  revisited with different values until mastered — that's the point of
  the loop — but the same *question* never is. Verified: 0 duplicate
  questions across a clean 20-question run and an adversarial
  47-question run with wrong answers mixed in.

Final output now also includes a session-statistics block (question
count, accuracy, Set Theory vs Probability breakdown) and a plain-text
certificate once certification is reached.

## Files

| File | Role |
|---|---|
| `Theory.hpp` | `FormalTheory` — concept space + dependency graph. `createSetTheory()` unchanged; `createFullTheory()` is new (adds Probability). |
| `LearnerModel.hpp/.cpp` | Mastery/Coverage/ErrorPatterns tracking, certification check. |
| `TemplateEngine.hpp/.cpp` | Z3-backed question generation for both Set Theory and Probability, with duplicate-question guarding. |
| `main.cpp` | The assessment loop — prompts, grades, updates the learner model, prints final stats and certificate. |
| `CMakeLists.txt` | Unchanged — already builds all of the above against Z3. |

## Reference material (not part of the build)

`notes.pdf`, `set_theory.pdf`, and `SAT_SMT_Solver_Explanation.pdf` are
your design notes and background reading, not project deliverables — I
left them out of the code package. Worth keeping around for your own
review/defense of the design, but I'd leave the raw shared-ChatGPT export
(`SAT_SMT_Solver_Explanation.pdf`) out of anything you actually hand in;
it reads as a chat transcript rather than a document you authored. If you
want, I can turn its content into a proper written explanation section
for your report instead.

## Suggested next steps (not done here — flagging for scope)

- A true/false or step-justification answer format for Properties/Rules
  (Commutativity, DeMorgan, etc.), so those concepts can join
  certification instead of staying permanently non-assessable.
- Persist session results (JSON/CSV) instead of only printing to stdout,
  if you want to show progress across multiple sessions.
