#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    void clear();
};

enum class ParseState {
    REQUEST_LINE,
    HEADERS,
    BODY,
    COMPLETE
};

class HttpSession {
private:
    std::vector<char> buffer;
    size_t read_idx{0};
    size_t write_idx{0};

    ParseState state{ParseState::REQUEST_LINE};
    HttpRequest request;
    size_t content_length{0};

    static std::string trim(std::string_view sv);
    void compact();

public:
    HttpSession();
    void appendData(const char* data, size_t len);
    bool parse(HttpRequest& out_req);
};