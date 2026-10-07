#pragma once
#include <string>
#include "http_client.h"
#include "json.hpp"

// Fetch open issues (not PRs) for "owner/name" and upsert them into SQLite.
// Returns {"ok":true,"repo":...,"count":N,"issues":[...]} or {"ok":false,"error":"..."}.
nlohmann::json import_issues(HttpClient& client, const std::string& repo,
                             const std::string& db_path);

// Read saved issues for a repo straight from SQLite (no network).
nlohmann::json read_issues(const std::string& repo, const std::string& db_path);
