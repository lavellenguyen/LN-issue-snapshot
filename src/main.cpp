#include <iostream>
#include <string>
#include "http_client.h"
#include "snapshot.h"

static int usage() {
    std::cerr << "Usage:\n"
                 "  issue_snapshot import owner/name [--db path]\n"
                 "  issue_snapshot read   owner/name [--db path]\n";
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string cmd = argv[1];
    std::string repo = argv[2];
    std::string db = "issues.db";
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--db" && i + 1 < argc) db = argv[++i];
        else return usage();
    }

    nlohmann::json result;
    if (cmd == "import") {
        CurlHttpClient client;
        result = import_issues(client, repo, db);
    } else if (cmd == "read") {
        result = read_issues(repo, db);
    } else {
        return usage();
    }

    std::cout << result.dump(2) << "\n";
    return result.value("ok", false) ? 0 : 1;
}
