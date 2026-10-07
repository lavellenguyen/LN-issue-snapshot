# GitHub Issue Snapshot

A small C++17 connector that imports open issues from a public GitHub repository into SQLite and reads them back without calling GitHub again.

## Prerequisites

- C++17 compiler (Apple Clang or GCC)
- CMake 3.14 or newer
- libcurl and SQLite3 development libraries
- Internet access (import only; read works offline)

macOS: `brew install cmake` (libcurl and SQLite ship with macOS)

Ubuntu/Debian: `sudo apt install build-essential cmake libcurl4-openssl-dev libsqlite3-dev`

`json.hpp` (nlohmann/json) and `doctest.h` are vendored in `third_party/`, so there is nothing else to install.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Usage

```bash
# Import open issues (one page, up to 100) for owner/name
./build/issue_snapshot import nlohmann/json --db issues.db

# Read saved issues (SQLite only, no network)
./build/issue_snapshot read nlohmann/json --db issues.db
```

`--db` is optional and defaults to `issues.db`. The program prints JSON and exits with code 0 on success or 1 on failure.

### Example output

Real import of `nlohmann/json` returned `"count": 18`. The issue list is shortened here to two entries:

```json
{
  "count": 18,
  "issues": [
    {
      "number": 5298,
      "title": "Idea: arena / PMR document mode (O(1) teardown, allocation-free destruction)",
      "url": "https://github.com/nlohmann/json/issues/5298"
    },
    {
      "number": 5316,
      "title": "CBOR: cbor_tag_handler_t::store rejects documents that ignore accepts, and drops tag numbers 6-20",
      "url": "https://github.com/nlohmann/json/issues/5316"
    }
  ],
  "ok": true,
  "repo": "nlohmann/json"
}
```

Errors return `"ok": false` with a message:

```json
{
  "error": "repository not found: notarepo/doesnotexist",
  "ok": false
}
```

```json
{
  "error": "invalid repo 'badformat': expected format owner/name",
  "ok": false
}
```

## Library use

`include/snapshot.h` exposes two reusable functions, both returning `nlohmann::json`:

```cpp
nlohmann::json import_issues(HttpClient& client, const std::string& repo, const std::string& db_path);
nlohmann::json read_issues(const std::string& repo, const std::string& db_path);
```

## Tests

```bash
ctest --test-dir build --output-on-failure
./build/run_tests        # detailed doctest output
```

The suite has 5 test cases and 23 assertions, all using a fake HTTP client so no network is needed:

1. Import then read returns the saved issues.
2. Repeated import creates no duplicates and updates changed titles in place.
3. Pull requests are excluded.
4. API failures (HTTP 500, 404, 403, and a network error) return useful errors.
5. Invalid repo names are rejected.

## AI tools used

- **Claude (Anthropic)**: my only AI tool. I used it to plan the architecture (a library plus a thin CLI, with an abstract `HttpClient` for testability), draft the libcurl wrapper, the SQLite upsert logic, the CLI, and the doctest suite, and to walk me through Git, CMake, and debugging while I worked in a toolchain newer to me. I'm more experienced in C and C++ than in this setup.
- My own work: creating the repo and environment, running every build and test, diagnosing failures from the compiler output, and verifying behavior manually (live imports, repeat-import row counts checked with `sqlite3`, offline reads, and error cases).

## An unfamiliar problem solved with AI

While building, `cmake --build` failed with errors in `snapshot.cpp` such as "missing terminating character" and "function definition is not allowed here". I gave the compiler output to Claude, which identified that the file was corrupted: a large paste into Terminal had been cut off partway and the rest appended, leaving a truncated string literal and a duplicate `read_issues` definition (the file was 307 lines instead of about 175).

I verified this myself with `wc -l` and `grep -n "^json read_issues"`, then deleted the file, rewrote it in an editor instead of pasting into Terminal, and confirmed the fix by rebuilding with no errors, running the tests (5 test cases, 23 assertions), and doing a live import.

I also learned something about my workflow: the earlier "100% tests passed" output came from stale placeholder binaries left over from a previous build, not my new code. I now check that the build itself finished before trusting any test or run output.

## Limitations

- Fetches one page only (up to 100 issues); no pagination.
- Public repositories only, unauthenticated (GitHub allows 60 requests per hour per IP).
- Stores only repo, issue number, title, and URL.
- Issues closed on GitHub since the last import are not removed from the local database.