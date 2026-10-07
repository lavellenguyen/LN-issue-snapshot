#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <filesystem>
#include "doctest.h"
#include "snapshot.h"

using json = nlohmann::json;

// Fake HTTP client: returns whatever response we set, never touches the network.
struct FakeHttpClient : HttpClient {
    HttpResponse response;
    HttpResponse get(const std::string&) override { return response; }
};

// Temp database file that is deleted before and after each test.
struct TempDb {
    std::string path;
    explicit TempDb(const std::string& name) {
        path = (std::filesystem::temp_directory_path() / name).string();
        std::filesystem::remove(path);
    }
    ~TempDb() { std::filesystem::remove(path); }
};

static FakeHttpClient ok_client(const json& payload) {
    FakeHttpClient c;
    c.response.status = 200;
    c.response.body = payload.dump();
    return c;
}

static json issue(int n, const std::string& title) {
    return {{"number", n}, {"title", title},
            {"html_url", "https://github.com/o/r/issues/" + std::to_string(n)}};
}

TEST_CASE("import then read returns saved issues") {
    TempDb tmp("snap_test_1.db");
    auto client = ok_client(json::array({issue(1, "First"), issue(2, "Second")}));

    json imp = import_issues(client, "o/r", tmp.path);
    CHECK(imp["ok"] == true);
    CHECK(imp["count"] == 2);

    json rd = read_issues("o/r", tmp.path);  // no client: reads SQLite only
    CHECK(rd["ok"] == true);
    CHECK(rd["count"] == 2);
    CHECK(rd["issues"][0]["number"] == 1);
    CHECK(rd["issues"][0]["title"] == "First");
    CHECK(rd["issues"][1]["url"] == "https://github.com/o/r/issues/2");
}

TEST_CASE("repeated import does not create duplicates and updates titles") {
    TempDb tmp("snap_test_2.db");
    auto c1 = ok_client(json::array({issue(1, "Old title"), issue(2, "Two")}));
    import_issues(c1, "o/r", tmp.path);
    import_issues(c1, "o/r", tmp.path);
    CHECK(read_issues("o/r", tmp.path)["count"] == 2);

    auto c2 = ok_client(json::array({issue(1, "New title"), issue(2, "Two")}));
    import_issues(c2, "o/r", tmp.path);
    json rd = read_issues("o/r", tmp.path);
    CHECK(rd["count"] == 2);
    CHECK(rd["issues"][0]["title"] == "New title");
}

TEST_CASE("pull requests are excluded") {
    TempDb tmp("snap_test_3.db");
    json pr = issue(2, "A pull request");
    pr["pull_request"] = {{"url", "https://example.com"}};
    auto client = ok_client(json::array({issue(1, "Real issue"), pr}));

    json imp = import_issues(client, "o/r", tmp.path);
    CHECK(imp["count"] == 1);
    CHECK(read_issues("o/r", tmp.path)["count"] == 1);
}

TEST_CASE("API failures return useful errors") {
    TempDb tmp("snap_test_4.db");

    FakeHttpClient server_error;
    server_error.response.status = 500;
    json r1 = import_issues(server_error, "o/r", tmp.path);
    CHECK(r1["ok"] == false);
    CHECK(r1["error"].get<std::string>().find("500") != std::string::npos);

    FakeHttpClient not_found;
    not_found.response.status = 404;
    json r2 = import_issues(not_found, "o/r", tmp.path);
    CHECK(r2["ok"] == false);
    CHECK(r2["error"].get<std::string>().find("not found") != std::string::npos);

    FakeHttpClient rate_limited;
    rate_limited.response.status = 403;
    CHECK(import_issues(rate_limited, "o/r", tmp.path)["ok"] == false);

    FakeHttpClient network_down;
    network_down.response.error = "Could not resolve host";
    json r3 = import_issues(network_down, "o/r", tmp.path);
    CHECK(r3["ok"] == false);
    CHECK(r3["error"].get<std::string>().find("network") != std::string::npos);
}

TEST_CASE("invalid repo names are rejected") {
    TempDb tmp("snap_test_5.db");
    FakeHttpClient client;
    client.response.status = 200;
    client.response.body = "[]";
    CHECK(import_issues(client, "noslash", tmp.path)["ok"] == false);
    CHECK(import_issues(client, "a/b/c", tmp.path)["ok"] == false);
    CHECK(import_issues(client, "/name", tmp.path)["ok"] == false);
    CHECK(read_issues("bad repo", tmp.path)["ok"] == false);
}
