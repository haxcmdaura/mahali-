# 00 — Rules (apply to every task in every phase)

## About the paths and line numbers

They come from a review of code excerpts, not from the repository. Locate every
function **by name**. If reality differs from what a task describes, **stop and
report**. Do not guess, and do not "adapt" the task silently.

## Rules

1. **One task at a time.** Finish it with a commit, write the report, then stop.
   Continue only when the owner writes `next`.
2. **Failing test first.** Write the test, run it, and show that it fails **for the
   reason of the defect itself** (not a compile error). Only then fix the code, then
   run the whole suite.
3. **Stay in scope.** No reformatting, no renaming, no "while I am here" fixes.
   Anything out of scope goes into the report under `Notes`; do not touch it.
4. **Never mix formatting changes with logic changes** in one commit.
5. **Verification commands** (all must pass before committing):
   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j"$(nproc)"
   QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
   ```
   If a test already fails **before** you start, record it in the report and do not
   fix it as part of the task.
6. **Commits**
   - One commit per task, containing the test and the fix together.
   - Format: Conventional Commits, English: `type(scope): summary` (max 72 chars).
   - Body: what the defect is, why it matters, the new test name, and `Task: Txx`.
   - Never use `--amend`, `push`, `--force`, or `--no-verify`. Commits stay local.
   - Work on a branch named `hardening` (create it from the current branch if missing).
7. **Money is integer cents (`long long`).** No `double` or `float` in any money
   path. Do not change the meaning of any existing column.
8. **Schema changes** = a new migration appended to `kMigrations` + bump
   `kSchemaVersion` + update `createSchema` so a fresh install matches + a test.
9. **When in doubt, ask.** A wrong assumption in financial code is worse than a pause.
10. **Do not read or implement `decisions.md`.** Those items are the owner's.

## Report format (after every task, five lines)

```
Files changed:        <list>
Failing test output:  <one line showing the expected failure>
Full ctest result:    <passed/failed counts>
Commit:               <hash> <subject>
Notes:                <anything out of scope, surprises, doubts>
```
