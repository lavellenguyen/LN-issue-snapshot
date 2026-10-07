#pragma once
#include <string>

struct HttpResponse {
    long status = 0;      // HTTP status code; 0 if the request never completed
    std::string body;
    std::string error;    // non-empty only on a network-level failure
};

// Abstract interface so tests can swap in a fake instead of hitting the network.
class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(const std::string& url) = 0;
};

class CurlHttpClient : public HttpClient {
public:
    HttpResponse get(const std::string& url) override;
};