# Round 4 — Weight purchase data: diagnose first, repair only after the owner decides

> Read `00-rules.md` first. Same rules apply, with the extra safety rules below.
> Context (confirmed by the owner): the live shop has used the old purchase dialog for about one month and recorded **many** purchases of weight products. In the old code the dialog passed an integer `QSpinBox` value straight into `line.quantity` and `PurchaseService` wrote `movement.delta = item.quantity`. For a weight product (stock in grams, price per kg) the expected consequences are:
> - **Stock** too low: the entered figure (probably kilograms) was added as grams.
> - **Average cost** unreliable: it mixes true grams (old stock, sales) with the understated purchase quantity.
> - **Not affected** (to be verified, not assumed): purchase totals and supplier balances, because they come from the line total, **if** the operator typed kilograms with a per-kg price.
> The unit the operators actually typed is **not known yet**. That is what R14 establishes.

## Safety rules for this round (in addition to 00-rules.md)

1. **Never open the live database.** Work only on a copy that the owner provides, and open it read-only (`sqlite3 -readonly`, or `file:...?mode=ro`).
2. **Never commit shop data.** Reports contain real supplier names and prices. Write them under a path that is in `.gitignore` (add the ignore rule in its own commit if missing). Commit only scripts and code.
3. R14 changes **no production code**.
4. R15 does not start until all of these are true: R10b and R10c are committed, the owner has chosen a repair option in writing, and a backup made with `Database::backupTo` exists.

## R14 — Read-only diagnostic (script + report, no production change)

Create a script under `tools/` (SQL or small script; read the **real** column names from `createSchema`, do not guess them) that runs against the copy and produces `docs-private/weight-data-diagnostic.md` (git-ignored).

For every product with `sold_by_weight = 1`, report:

1. **Purchases:** number of lines, min / max / average `quantity`, average `unit_price_cents`, sum of `quantity`, sum of `total_cents`.
2. **Every purchase line** in a second table: date, supplier, quantity, unit price, line total. (The owner will compare a sample against paper invoices.)
3. **Sales by weight:** total grams sold, number of sales, number of **oversold** sales and their grams.
4. **Other stock movements** on the product: reason, count, sum, date range (especially stock adjustments or inventory corrections made to compensate).
5. **Stock now** (sum of movements), and the stored `quantity` if the column exists; **cost price now**.
6. **Running stock over time** per product: the lowest value reached and the dates on which it was negative.
7. A check that `total_cents == quantity × unit_price_cents` for every weight purchase line (the old rule), listing any line that violates it.

Also answer, with file and function names: **is there a stock adjustment / inventory count feature** in the code (the path, whether it writes an audit log, whether it can set a product's stock to a counted value)?

Report facts only. Do not recommend a repair. Stop after the report.
- **Commit (script only):** `chore(tools): read-only diagnostic for weight purchase data`

## R15 — Repair: not specified yet

No tasks until R14 is reviewed. The options the owner will choose between:

- **Option B — physical recount (recommended by the reviewer).** The owner counts each weight product; a tool writes one adjustment movement per product (`counted grams − current stock`, reason "inventory correction") and sets `cost_price_cents` to a value the owner approves. History is untouched; it does not depend on how operators typed quantities.
- **Option A — corrective movements per past purchase line** (`delta = quantity × 999`, once, idempotent). Valid only if no compensating adjustments were made during the month; otherwise it double counts. Useful as a cross-check against the recount.

Whatever is chosen, the tool must: go through the service layer (with the audit log), not raw SQL; have a **dry-run** mode that prints every change first; refuse to run unless a fresh `backupTo` file exists; be **idempotent** (a marker in settings); and be tested on a fixture database.
