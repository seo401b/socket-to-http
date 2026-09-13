#include "HttpSession.h"
#include <sstream>
#include <cstring>
#include <iostream>

void HttpRequest::clear(){
    method.clear();
    path.clear();
    version.clear();
    headers.clear();
    body.clear();
}

HttpSession::HttpSession() : buffer(8192) {}

std::string HttpSession::trim(std::string_view sv){
    size_t first = sv.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos) return "";
    size_t last = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(first, (last - first + 1)));
}

void HttpSession::compact() {
    if(read_idx == 0) return;
    size_t valid_len = write_idx - read_idx;
    if(valid_len > 0){
        std::memmove(buffer.data(), buffer.data()+read_idx, valid_len);
    }
    read_idx = 0;
    write_idx = valid_len;
}

void HttpSession::appendData(const char* data, size_t len) {
    if(write_idx + len > buffer.size()){
        compact();
        if(write_idx + len > buffer.size()){
            buffer.resize(buffer.size()*2);
        }
    }
    std::copy(data, data + len, buffer.begin() + write_idx);
    write_idx += len;
}

bool HttpSession::parse(HttpRequest& out_req) {
    while(true){
        std::string_view view(buffer.data() + read_idx, write_idx - read_idx);

        if(state == ParseState::REQUEST_LINE){
            size_t crlf_pos = view.find("\r\n");
            if(crlf_pos == std::string_view::npos) break;

            std::string_view line = view.substr(0, crlf_pos);
            std::stringstream ss{std::string(line)};
            ss >> request.method >> request.path >> request.version;

            read_idx += crlf_pos + 2;
            state = ParseState::HEADERS;
        }
        else if(state == ParseState::HEADERS) {
            size_t crlf_pos = view.find("\r\n");
            if(crlf_pos == std::string_view::npos) break;

            std::string_view line = view.substr(0, crlf_pos);
            if(line.empty()){
                read_idx += 2;

                auto it = request.headers.find("Content-Length");
                if(it == request.headers.end()) it = request.headers.find("content-length");

                if(it != request.headers.end()){
                    content_length = std::stoul(it->second);
                    state = (content_length > 0) ? ParseState::BODY : ParseState::COMPLETE;
                }
                else{
                    state = ParseState::COMPLETE;
                }
            }
            else {
                size_t colon_pos = line.find(':');
                if(colon_pos != std::string_view::npos){
                    std::string key = trim(line.substr(0, colon_pos+1));
                    std::string val = trim(line.substr(colon_pos+1));
                    request.headers[key] = val;
                }
                read_idx += crlf_pos + 2;
            }
        }
        else if(state == ParseState::BODY){
            if(view.size() < content_length) break;

            request.body = std::string(view.substr(0, content_length));
            read_idx += content_length;
            state = ParseState::COMPLETE;
        }
        if(state == ParseState::COMPLETE){
            out_req = request;

            request.clear();
            content_length = 0;
            state = ParseState::REQUEST_LINE;
            compact();
            return true;
        }
    }
    return false;
}