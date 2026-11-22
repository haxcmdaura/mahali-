# Decisions — owner only

> Not for the agent. Do not implement anything here until the owner writes an explicit task.

| # | Topic | Options |
|---|---|---|
| D1 | Zakat: (a) stock is valued at sale price; (b) supplier debts are computed but not deducted; (c) the year is Gregorian, not Hijri; (d) if the nisab is not saved, zakat is charged on any positive base; (e) "mark zakat paid" is a flag only and creates no cash-drawer entry | Fiqh and accounting decisions. Review with a qualified religious reference, then write the exact requirement |
| D2 | Purchases / supplier returns by carton: `unit_kind`, `pieces_received`, `pieces_consumed` exist in the schema but the repositories do not store them | Implement full support, or drop the columns with a migration |
| D3 | `parseMoney` with more than two decimals | Reject (current choice) or round |
| D4 | `customer_transactions` has no `occasion_id` and no `oversold` marker (cash sales have both) | Should credit sales be attributed to occasions? |
| D5 | Dead column `package_size` | Needs an impact check on the sync wire format first |
| D6 | Historical weight purchases: **confirmed** — the live shop recorded many purchases of weight products over about one month with the old dialog. Their stock is understated and their average cost is unreliable; supplier balances and totals should be unaffected if operators typed kg with a per-kg price (not yet verified) | Recount (recommended) or corrective movements. Decided only after the R14 diagnostic in `round-4-data-repair.md`. Not for the agent until then |
| D7 | Profit reports for the bug month: sales of weight products carry a cost snapshot taken from an unreliable average cost | Leave as approximate, or recompute later from corrected costs |

## Deferred (do not start)

- `TransactionGuard` (RAII) to replace the repeated manual `rollback()`; touches every service, do it one service per commit after the money rounds are stable.
- Replace text stock-movement references (`"Purchase #N"`) with typed `ref_type/ref_id` (migration plus backfill).
- Error codes instead of English error strings shown inside the Arabic UI.
- Split `createSchema` (about 420 lines) and `tst_data.cpp` (over 5600 lines).
- Installer signing and automatic updates.
- Review of the sync layer (`network/`, outbox, apply path) before the phone app is connected.
