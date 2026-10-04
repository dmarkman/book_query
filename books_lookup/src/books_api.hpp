#pragma once
#include <curl/curl.h>
#include <stdexcept>
#include <string>
#include <vector>

struct BookQuery {
    std::string title;
    std::string publisher;   // main word only, e.g. "norton"; optional
    std::string lang;        // "en", "ru", "uk"; optional
    std::string country;     // e.g. "US"; optional
    int max_results = 10;
};

struct Volume {
    std::string title;
    std::string subtitle;
    std::vector<std::string> authors;
    std::string publisher;
    std::string published_date;   // "2007", "2009-09-22", ...
    std::string language;
    int year() const;             // 0 if unknown
};

struct HttpError : std::runtime_error {
    long status;
    HttpError(long s, const std::string& msg) : std::runtime_error(msg), status(s) {}
};

class BooksClient {
public:
    explicit BooksClient(std::string api_key);
    ~BooksClient();
    BooksClient(const BooksClient&) = delete;
    BooksClient& operator=(const BooksClient&) = delete;

    std::string search(const BookQuery& q);   // returns raw JSON body

private:
    std::string escape(const std::string& s);
    std::string build_url(const BookQuery& q);

    std::string key_;
    CURL* curl_;
};

std::vector<Volume> parse_volumes(const std::string& body);
