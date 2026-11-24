#pragma once

#include <QSqlDatabase>

namespace app::data {

// The schema version this build expects, stamped into PRAGMA user_version as
// each migration commits.
//
// Bumped whenever a migration is appended to the table in schema_migrations.cpp.
// createSchema() keeps creating the *current* shape for a fresh install, so this
// version is what separates "built by this release" from "carried over from an
// older one"; the difference is exactly the set of migrations that still has to
// run.
constexpr int kSchemaVersion = 20;

// Applies every migration the database has not seen yet, in order. Each one runs
// in its own transaction and stamps its own version before that transaction
// commits. Called once from Database's constructor, after createSchema().
//
// A database stamped by a newer build is refused before any migration runs.
// A failed migration rolls back and throws without advancing the version, so the
// next start retries it instead of treating a partial database as current.
// The one exception is the duplicate-reversal index, which warns and is skipped:
// a shop whose ledger already holds a duplicate reversal must still be able to
// open its till and sell. The protection that index would have added stays in the
// services, which check before they write; what is lost is only the database's
// own second opinion.
void runSchemaMigrations(const QSqlDatabase& db);

// The version the database was last migrated to, or 0 when it predates
// versioning. Read with PRAGMA user_version.
int readSchemaVersion(const QSqlDatabase& db);

// Reads the version and refuses a database stamped by a newer build. The check
// is read-only, so callers can run it before changing pragmas or the schema.
int readSupportedSchemaVersion(const QSqlDatabase& db);

} // namespace app::data