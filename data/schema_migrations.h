#pragma once

#include <QSqlDatabase>

namespace app::data {

// The schema version this build expects, stamped into PRAGMA user_version once
// every migration below it has been applied.
//
// Bumped whenever a migration is appended to the table in schema_migrations.cpp.
// createSchema() keeps creating the *current* shape for a fresh install, so this
// version is what separates "built by this release" from "carried over from an
// older one"; the difference is exactly the set of migrations that still has to
// run.
constexpr int kSchemaVersion = 16;

// Applies every migration the database has not seen yet, in order, and then
// stamps user_version. Called once from Database's constructor, after
// createSchema().
//
// A failed migration stops the sequence and throws without stamping the version,
// so the next start retries it instead of treating a partial database as current.
// The one exception is the duplicate-reversal index, which warns and is skipped:
// a shop whose ledger already holds a duplicate reversal must still be able to
// open its till and sell. The protection that index would have added stays in the
// services, which check before they write; what is lost is only the database's
// own second opinion.
void runSchemaMigrations(const QSqlDatabase& db);

// The version the database was last migrated to, or 0 when it predates
// versioning. Read with PRAGMA user_version.
int readSchemaVersion(const QSqlDatabase& db);

} // namespace app::data