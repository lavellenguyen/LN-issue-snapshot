# Architecture

## Overview

The project does two things with one SQLite file:

- **Import:** fetch open issues from the GitHub API and save them.
- **Read:** return saved issues from SQLite, with no network call.

All logic lives in a library (`snapshot_lib`). The command-line program is a thin wrapper around it, and the tests call the same library functions.

## Components

| File | Role |
|---|---|
| `include/http_client.h`, `src/http_client.cpp` | `HttpClient` abstract interface and `CurlHttpClient` (libcurl). Returns the HTTP status, body, and any network error. Sets the `User-Agent` header GitHub requires. |
| `include/snapshot.h`, `src/snapshot.cpp` | `import_issues()` and `read_issues()`, repo validation, SQLite access. |
| `src/main.cpp` | CLI: parses arguments, calls the library, prints JSON, sets the exit code. |
| `tests/test_snapshot.cpp` | doctest suite with a `FakeHttpClient` and temporary databases. |
| `third_party/` | Vendored single-header libraries: nlohmann/json and doctest. |
| `CMakeLists.txt` | Builds the library, the `issue_snapshot` CLI, and the `run_tests` executable. |

## Data flow

**Import** (`import_issues`):

1. Validate that the repo is `owner/name` with safe characters.
2. `GET /repos/{repo}/issues?state=open&per_page=100` through the injected `HttpClient`.
3. Map failures to errors: network error, 404, 403/429, other non-200 statuses.
4. Parse the JSON and drop items that have a `pull_request` key, because the issues endpoint also returns pull requests.
5. Upsert the remaining issues in a single transaction.
6. Return the saved issues.

**Read** (`read_issues`):

1. Validate the repo name.
2. `SELECT number, title, url FROM issues WHERE repo = ? ORDER BY number`.
3. Return the rows. No `HttpClient` is involved.

## Interface

Both functions return `nlohmann::json`:

- Success: `{"ok": true, "repo": "...", "count": N, "issues": [{"number": 1, "title": "...", "url": "..."}]}`
- Failure: `{"ok": false, "error": "..."}`

## Schema

```sql
CREATE TABLE IF NOT EXISTS issues (
  repo   TEXT    NOT NULL,
  number INTEGER NOT NULL,
  title  TEXT    NOT NULL,
  url    TEXT    NOT NULL,
  PRIMARY KEY (repo, number)
);
```

Writes use `INSERT ... ON CONFLICT(repo, number) DO UPDATE SET title = excluded.title, url = excluded.url`.

## Configuration

The database path comes from the `--db` flag (default `issues.db`) or from the `db_path` argument when using the library directly. The table is created automatically if it doesn't exist, and data persists across program restarts because it lives in the SQLite file.

## Error handling

These cases return `ok: false` with a message and make the CLI exit with code 1:

- Invalid repo format
- Network failure (for example, DNS or timeout)
- Repository not found (404)
- Rate limited or access denied (403/429)
- Any other non-200 response
- Response that is not a JSON array
- Database errors

A failed import rolls back its transaction, so the database is never left half-written.

## Design decisions and tradeoffs

- **Abstract `HttpClient`.** The real client uses libcurl, and tests pass a fake that returns canned responses. This keeps tests fast, deterministic, and offline, and it makes failures like HTTP 500 easy to test without hitting GitHub.
- **Composite primary key plus upsert.** `(repo, number)` uniquely identifies an issue, so repeated imports update existing rows instead of duplicating them, and changed titles are refreshed.
- **One transaction per import.** Either all fetched issues are saved or none are.
- **Prepared statements and repo validation.** Values are bound as parameters rather than concatenated into SQL, and the repo name is checked before it is placed in a URL.
- **RAII wrappers for SQLite handles.** Small `Db` and `Stmt` structs close the database and finalize statements automatically on every return path.
- **Read does not depend on HTTP.** `read_issues` takes no client, so offline reads are guaranteed by its signature.
- **Intentionally not done:** pagination, authentication, and removing issues that were closed since the last import. These keep the scope small; the first would be the next addition.