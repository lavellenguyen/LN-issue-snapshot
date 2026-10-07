#include "snapshot.h"
#include <sqlite3.h>
#include <cctype>

using json = nlohmann::json;

namespace {

json fail(const std::string& msg) {
    return json{{"ok", false}, {"error", msg}};
}

// RAII wrappers so the database and statements are always cleaned up.
struct Db {
    sqlite3* h = nullptr;
    ~Db() { if (h) sqlite3_close(h); }
};
struct Stmt {
    sqlite3_stmt* s = nullptr;
    ~Stmt() { if (s) sqlite3_finalize(s); }
};

// Accept exactly "owner/name" with safe characters only.
bool valid_repo(const std::string& repo) {
    size_t slash = repo.find('/');
    if (slash == std::string::npos || slash == 0 || slash == repo.size() - 1) return false;
    if (repo.find('/', slash + 1) != std::string::npos) return false;
    for (char c : repo) {
        if (c == '/') continue;
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            return false;
    }
    return true;
}

bool open_db(Db& db, const std::string& path, std::string& err) {
    if (sqlite3_open(path.c_str(), &db.h) != SQLITE_OK) {
        err = std::string("cannot open database: ") + sqlite3_errmsg(db.h);
        return false;
    }
    // The composite primary key (repo, number) is what prevents duplicates.
    const char* create =
        "CREATE TABLE IF NOT EXISTS issues ("
        " repo TEXT NOT NULL,"
        " number INTEGER NOT NULL,"
        " title TEXT NOT NULL,"
        " url TEXT NOT NULL,"
        " PRIMARY KEY (repo, number));";
    char* msg = nullptr;
    if (sqlite3_exec(db.h, create, nullptr, nullptr, &msg) != SQLITE_OK) {
        err = std::string("cannot create table: ") + (msg ? msg : "unknown");
        sqlite3_free(msg);
        return false;
    }
    return true;
}

json make_result(const std::string& repo, const json& issues) {
    return json{{"ok", true},
                {"repo", repo},
                {"count", issues.size()},
                {"issues", issues}};
}

}  // namespace

json import_issues(HttpClient& client, const std::string& repo,
                   const std::string& db_path) {
    if (!valid_repo(repo))
        return fail("invalid repo '" + repo + "': expected format owner/name");

    // 1. Fetch one page of open issues.
    HttpResponse r = client.get("https://api.github.com/repos/" + repo +
                                "/issues?state=open&per_page=100");
    if (!r.error.empty()) return fail("network error: " + r.error);
    if (r.status == 404) return fail("repository not found: " + repo);
    if (r.status == 403 || r.status == 429)
        return fail("GitHub rate limit or access denied (HTTP " +
                    std::to_string(r.status) + ")");
    if (r.status != 200)
        return fail("GitHub API returned HTTP " + std::to_string(r.status));

    // 2. Parse and keep only real issues (the endpoint also returns PRs).
    json data = json::parse(r.body, nullptr, false);
    if (data.is_discarded() || !data.is_array())
        return fail("unexpected response from GitHub (not a JSON array)");

    json issues = json::array();
    for (const auto& item : data) {
        if (item.contains("pull_request")) continue;
        if (!item.contains("number") || !item.at("number").is_number_integer() ||
            !item.contains("title") || !item.at("title").is_string() ||
            !item.contains("html_url") || !item.at("html_url").is_string())
            continue;
        issues.push_back({{"number", item.at("number").get<long long>()},
                          {"title", item.at("title").get<std::string>()},
                          {"url", item.at("html_url").get<std::string>()}});
    }

    // 3. Upsert everything in one transaction.
    Db db;
    std::string err;
    if (!open_db(db, db_path, err)) return fail(err);
    if (sqlite3_exec(db.h, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
        return fail(std::string("database error: ") + sqlite3_errmsg(db.h));

    Stmt st;
    const char* upsert =
        "INSERT INTO issues (repo, number, title, url) VALUES (?, ?, ?, ?) "
        "ON CONFLICT(repo, number) DO UPDATE SET "
        "title = excluded.title, url = excluded.url;";
    if (sqlite3_prepare_v2(db.h, upsert, -1, &st.s, nullptr) != SQLITE_OK) {
        std::string m = sqlite3_errmsg(db.h);
        sqlite3_exec(db.h, "ROLLBACK", nullptr, nullptr, nullptr);
        return fail("database error: " + m);
    }

    for (const auto& i : issues) {
        std::string title = i["title"].get<std::string>();
        std::string url = i["url"].get<std::string>();
        sqlite3_reset(st.s);
        sqlite3_clear_bindings(st.s);
        sqlite3_bind_text(st.s, 1, repo.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st.s, 2, i["number"].get<long long>());
        sqlite3_bind_text(st.s, 3, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.s, 4, url.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.s) != SQLITE_DONE) {
            std::string m = sqlite3_errmsg(db.h);
            sqlite3_exec(db.h, "ROLLBACK", nullptr, nullptr, nullptr);
            return fail("database error: " + m);
        }
    }

    if (sqlite3_exec(db.h, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
        return fail(std::string("database error: ") + sqlite3_errmsg(db.h));

    return make_result(repo, issues);
}

json read_issues(const std::string& repo, const std::string& db_path) {
    if (!valid_repo(repo))
        return fail("invalid repo '" + repo + "': expected format owner/name");

    Db db;
    std::string err;
    if (!open_db(db, db_path, err)) return fail(err);

    Stmt st;
    const char* sel =
        "SELECT number, title, url FROM issues WHERE repo = ? ORDER BY number;";
    if (sqlite3_prepare_v2(db.h, sel, -1, &st.s, nullptr) != SQLITE_OK)
        return fail(std::string("database error: ") + sqlite3_errmsg(db.h));
    sqlite3_bind_text(st.s, 1, repo.c_str(), -1, SQLITE_TRANSIENT);

    json issues = json::array();
    while (sqlite3_step(st.s) == SQLITE_ROW) {
        issues.push_back(
            {{"number", sqlite3_column_int64(st.s, 0)},
             {"title", reinterpret_cast<const char*>(sqlite3_column_text(st.s, 1))},
             {"url", reinterpret_cast<const char*>(sqlite3_column_text(st.s, 2))}});
    }
    return make_result(repo, issues);
}