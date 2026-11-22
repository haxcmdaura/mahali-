# Round 3 — weight purchases, remaining races, report gaps

> Read `00-rules.md` first. Same rules: one task at a time, failing test first, one commit per task, stop and report after each.
> Context from Round 2: the purchase dialog passes `qty->value()` from an **integer `QSpinBox`** straight into `line.quantity`, and `PurchaseService` writes `movement.delta = item.quantity`. For a weight product (stock in **grams**, price **per kg**) this makes a purchase of 10 kg add 10 g, and an integer spinbox cannot even express 2.5 kg or 250 g.
> Do **not** repair historical data. That is an owner decision (see `decisions.md`, D6).

## R10a — Fix the R2 test setup (test only)

The R2 test reported `productAfter->quantity: 10` although the test assumed an initial stock of 5000. Stock is derived from `stock_movements`, so setting a quantity field on the product struct most likely created **no movement**; the 10 is just the purchase delta.

- Seed the initial 5000 g through a real stock movement (the same helper the other tests use for opening stock) and give the product a known cost (1000 c/kg).
- Assert exactly: movement delta, stock after, and average cost after. Expected for "10 kg at 2000 c/kg": delta 10000, stock 15000, cost ≈ 1667 c/kg.
- Keep `QEXPECT_FAIL`, and report the **actual** delta, stock and cost numbers.
- **Commit:** `test(purchase): seed opening stock through a movement in the weight purchase test`

## R10b — Weight purchases: contract in grams (service + repository)

**Status check first (the working tree is dirty).** Uncommitted changes exist in `data/purchase_item_repository.cpp`, `data/purchase_service.cpp`, `data/sale_rules.h`, `tests/tst_purchase_service.cpp`. Before anything else:
- run `git diff --stat` and give one line per file on what changed;
- show `git diff data/sale_rules.h` in full: it touches the **sale** path, which this task must not change in behaviour;
- the only weight test is `purchase_weight_product_uses_kg_input_unit` (it passes grams, so rename it `purchase_weight_product_uses_grams`). Add the missing tests listed below (250 g, old-style row void, linked supplier return of a weight line). **Do not commit R10b until they exist and pass.** Never leave a task's changes uncommitted when moving to the next task.
- confirm `QEXPECT_FAIL` is gone from the R2 test (an unexpected pass counts as a failure in QtTest).

Goal: a purchase line for a weight product follows the same convention as a sale line: `quantity` is **grams**, price is **per kg**, the line is stored with `unit_kind = 'weight'`.

1. **Repository:** `purchase_item_repository` must persist and read `unit_kind` (the column already exists and currently always defaults to `'piece'`). Do not touch `pieces_received` yet (see D2).
2. **Validation:** reuse the T11 rules for purchase lines: a `soldByWeight` product requires `weight`; a normal product rejects `weight`; unknown kinds are refused; empty means `piece`.
3. **Totals:** the line total for a weight line is `grams × unitPriceCents / 1000` (integer division, truncating: the same convention as the sale path), computed **inline with the existing overflow guard for now**. `totalCentsFor` takes `SaleItem`s and cannot be used for purchase lines; R10c-1 will extract a shared function and replace the inline code with the tests unchanged. The T16 check `item.totalCents == quantity × unitPriceCents` must become weight-aware, otherwise it will reject correct weight purchases.
4. **Stock and cost:** `movement.delta` is the grams; `updateAverageCost` weights by grams, with cost per kg. Confirm the units are consistent and say so in the report.
5. **Old rows:** existing purchase rows keep `unit_kind = 'piece'` and their stored quantities. Voiding an old purchase must keep reversing exactly what was stored. Do not "correct" anything.
6. **Void and supplier return:** check that `voidPurchase` and the supplier return paths use the stored quantity and `unit_kind` consistently for weight lines.

7. **Scope limit:** purchase lines accept only `piece` and `weight`. A `package` purchase line is **refused with a clear error** until decision D2 is made; remove any package stock-delta logic added to the purchase path, and do not read or write `pieces_received`. Only change `purchase_item_repository.cpp` as far as `unit_kind` needs. If the `reversed_id` mapping is not required by this task, leave it out and say so in the report.
8. **Average cost test:** assert the **exact** value the formula yields (integer truncation: 1666 for the R10a seed of 5000 g at 1000 c/kg plus 10000 g at 2000 c/kg), not a "1666 or 1667" range. A loose assertion hides the rounding policy.

- **Tests (service level, quantities in grams):** 10000 g at 2000 c/kg → delta 10000, stock +10000, cost ≈ 1667 with the R10a seed; 250 g → delta 250 and total 500; `piece` for a weight product refused; `weight` for a normal product refused; void of a new weight purchase restores stock exactly; a linked supplier return of a weight line returns grams; void of an **old-style** row (`unit_kind` piece, quantity 10) still reverses 10.
- Remove `QEXPECT_FAIL` from the R2 test only when it passes on its own.
- **Commit:** `fix(purchase): weight purchases use grams, persist unit_kind, weight-aware totals`

## R10c — Purchase dialog for weight products

Findings from your report that shape this task: `showPurchaseDialog` computes the line total in **three** places (the `lineTotal` lambda, `recompute`, the save loop) and the service computes it a fourth time; `line.unitKind` is never set, so the service currently refuses every weight purchase from the dialog (a loud failure, not a silent one); `tst_ui` has no purchase dialog test and the dialog is a blocking function, so widget wiring cannot be unit-tested. Therefore all the logic is extracted into **pure functions with tests**, and the dialog only wires widgets to them.
Rounding policy: the kg→gram conversion is exact (text with at most 3 decimals, integer arithmetic). Line totals keep the **sale convention** (integer division by 1000, truncation) so purchase and sale agree to the cent.

### R10c-1 — Shared line-total function (no UI)
- Add `core::lineTotalCents(long long quantity, long long unitPriceCents, UnitKind kind) -> std::optional<long long>` in a header both `data/` and `ui/` can include. `piece`/`package`: `quantity × price`. `weight`: `quantity (grams) × price / 1000`, truncating, identical to `totalCentsFor`. Overflow returns `nullopt` (use the existing `productFits`).
- `PurchaseService` replaces its inline computation with it. **Do not modify the sale path.**
- Tests: a table including `2675 g × 1234 → 3300`, zero and overflow cases, and an equivalence test against `totalCentsFor` on the same inputs.
- **Commit:** `refactor(core): shared lineTotalCents used by the purchase service`

### R10c-2 — `parseWeightGrams`
- `core::parseWeightGrams(const QString&) -> std::optional<long long>`: kilograms as typed → integer grams, **no floating point**. Accept `,` `.` `٫` as the decimal separator and Arabic-Indic digits. Exactly one separator is allowed and it is **always decimal** (unlike `parseMoney`, `"1,000"` is 1 kg). Two or more separators → `nullopt`. At most 3 decimals (extra zeros allowed, extra non-zero digits → `nullopt`). Reject zero, negatives, empty. Cap at 10,000 kg per line (named constant).
- Test table: `"2.5"→2500` · `"2,5"→2500` · `"٢٫٥"→2500` · `"0.250"→250` · `"0.001"→1` · `"0.0005"→nullopt` · `"2.5000"→2500` · `"2.5001"→nullopt` · `"1,000"→1000` · `"1.000"→1000` · `"1,234.5"→nullopt` · `"0"→nullopt` · `"-1"→nullopt` · `""→nullopt` · `"10001"→nullopt`
- **Commit:** `feat(core): parseWeightGrams without floating point`

### R10c-3 — Pure purchase-line builder
- Extract the line construction from the save loop (`purchase_dialog.cpp` ~566–609) into a free function, e.g. `makePurchaseItem(const core::Product&, long long quantity, long long unitPriceCents) -> {PurchaseItem or error}`. It sets `unitKind = Weight` **iff** `product.soldByWeight`, sets `unit`, and computes `totalCents` with `lineTotalCents`.
- Tests: weight product → `unitKind` weight, quantity in grams, correct total; normal product unchanged from today's behaviour; invalid quantity or overflow → error.
- **Commit:** `refactor(purchase): extract makePurchaseItem from the dialog`

### R10c-4 — Dialog wiring (owner reviews visually)
- Choose the quantity editor **per row in `bindRow`**: normal product → the existing integer `QSpinBox`; weight product → a `QLineEdit` in kg parsed with `parseWeightGrams`. Swapping the widget when the row's product changes must reconnect the recompute handler and reset the value. A single helper creates the editor.
- Delete the `lineTotal` lambda; `recompute` and the save loop both go through the same helper that calls `lineTotalCents` / `makePurchaseItem`. No third copy of the formula.
- The unit cell shows `kg` for weight rows and the price field makes it clear the price is per kg (placeholder or suffix).
- No stock logic and no direct money arithmetic in the dialog.
- Because the dialog cannot be unit-tested, put a **manual test script** in the report: (1) add a weight product, enter `2,5` → quantity 2500 g, total correct; (2) change the same row to a piece product → integer spinbox returns, total correct; (3) save, then confirm stock +2500 and the purchase row has `unit_kind` weight.
- **Commit:** `feat(ui): purchase dialog takes kg with decimals for weight products`

## R10d — Supplier returns of weight products (audit only, no code changes)

R10b made **new** weight purchase lines store grams with `unit_kind = 'weight'`; **old** lines keep `piece` and their old quantities. `SupplierReturnItemRepository` does not persist `unit_kind`, and the return paths subtract the stored quantity from stock. The return dialog probably has the same unit problem as the purchase dialog. Audit and report as a table:

1. Which unit does the supplier-return dialog pass for a weight product (integer spinbox? kg? g?), and what does the service subtract from stock?
2. How is "returned quantity ≤ purchased quantity" checked, and is it consistent when the purchased line is old-style (kg typed as grams) versus new-style (grams)?
3. How is the credit amount computed for a weight line (quantity × price, with or without the division by 1000)?
4. Which combinations are wrong today: return of a new weight purchase, return of an old weight purchase, return with no linked purchase.
5. Proposal only: one task per defect, each with its failing test; and whether returns of weight products should be **blocked with a clear message** until fixed.

No commit (report only, kept out of git).

## R11 — Remaining check-then-write races

Your R4a audit lists two services that decide on mutable state **before** starting the write transaction. Fix them, one per commit, same pattern as `voidPurchase`:

1. `SupplierReturnService::recordLinkedReturn` (and `recordGeneralReturn` if its amount check depends on a balance): move every check that depends on mutable state (already-returned quantities, stock available, supplier balance) **after** `beginWriteTransaction()`, with `rollback()` on refusal.
2. `PaymentService::refundCustomerPayment`: the `original` is read before the transaction and only the session is re-checked inside. Check whether the payment is already reversed **inside** the transaction, and whether a unique index on the payment reversal column exists (as `uq_sales_one_reversal` does for sales). If the index is missing, add it by the usual migration rule, and handle the duplicate-data case the way migration 4 does.
- **Tests:** two connections when deterministic (A moves the state without committing, B's operation must not pass on stale data); otherwise the refusal and double-refund paths.
- **Commits:** `fix(suppliers): evaluate return guards inside the write transaction`, `fix(payments): re-check reversal state inside the transaction`

## R12 — Money readers: your R6 conclusion is not accepted yet

Evidence against "none found": the `balanceCentsFor` snippet you quoted in the V2 report (`customer_repository.cpp` ~196–215) still contained `if (cq.exec() && cq.next()) { openingCents = ... }` followed by a return of 0, and your R6 search excluded that function by assumption.

1. Show the **current** `balanceCentsFor` and `stockValueCents` and the **test names** that break the query and expect failure (`nullopt`) instead of zero. If those tests do not exist, write them first (they must fail if the swallow is still there).
2. Broaden the search beyond the literal `if (exec() && next())`:
   - any `exec()` result that is ignored before a `while (q.next())` loop (a failed query then looks like "no rows" and sums to 0);
   - `if (!q.exec()) { ... }` branches that fall through to a default or partial total;
   - variants with other variable names (`query`, `cq`, `q`).
   Restrict the report to money, stock, balances and report totals.
3. Fix each real hit **one function per commit**, with a test that breaks the query and expects failure, not zero.
- **Commit:** `fix(data): <function> reports failure instead of returning zero`

## R13 — Audit the commit "WIP: audit fixes (unverified)" (`13cba43`)

Your `git log` shows a commit with that message. For `git show --stat 13cba43`:
- list every behaviour it changed (one line each);
- for each, name the test that covers it;
- for each behaviour with no test, write a regression test (no production changes). If a test fails, stop and report; fix it in a separate commit.
- **Commit:** `test: regression coverage for behaviours changed in 13cba43`

## Order

R10a → R10b → R10c-1 → R10c-2 → R10c-3 → R10c-4 → R10d → R11 → R12 → R13
