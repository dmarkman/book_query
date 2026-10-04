#include "books_api.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string str_field(const json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

} // namespace

int Volume::year() const {
    if (published_date.size() >= 4 &&
        std::all_of(published_date.begin(), published_date.begin() + 4,
                    [](unsigned char c) { return std::isdigit(c); }))
        return std::stoi(published_date.substr(0, 4));
    return 0;
}

BooksClient::BooksClient(std::string api_key)
    : key_(std::move(api_key)), curl_(curl_easy_init()) {
    if (!curl_) throw std::runtime_error("curl_easy_init failed");
}

BooksClient::~BooksClient() { curl_easy_cleanup(curl_); }

std::string BooksClient::escape(const std::string& s) {
    char* e = curl_easy_escape(curl_, s.c_str(), static_cast<int>(s.size()));
    if (!e) throw std::runtime_error("curl_easy_escape failed");
    std::string r(e);
    curl_free(e);
    return r;
}

std::string BooksClient::build_url(const BookQuery& q) {
    std::string terms = q.title;
    if (!q.publisher.empty()) terms += " " + q.publisher;

    std::string url = "https://www.googleapis.com/books/v1/volumes?q=" + escape(terms)
                    + "&maxResults=" + std::to_string(q.max_results);
    if (!q.lang.empty())    url += "&langRestrict=" + escape(q.lang);
    if (!q.country.empty()) url += "&country=" + escape(q.country);
    url += "&key=" + escape(key_);
    return url;
}

std::string BooksClient::search(const BookQuery& q) {
    const std::string url = build_url(q);
    std::string body;

    curl_easy_reset(curl_);   // clears options, keeps the connection alive
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl_, CURLOPT_ACCEPT_ENCODING, "");   // gzip if offered
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl_, CURLOPT_USERAGENT, "book-lookup/1.0");

    CURLcode rc = curl_easy_perform(curl_);
    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("curl: ") + curl_easy_strerror(rc));

    long status = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status);
    if (status != 200) {
        std::string msg = "HTTP " + std::to_string(status);
        auto j = json::parse(body, nullptr, false);
        if (!j.is_discarded() && j.contains("error") && j["error"].is_object())
            if (auto m = str_field(j["error"], "message"); !m.empty()) msg += ": " + m;
        throw HttpError(status, msg);
    }
    return body;
}

std::vector<Volume> parse_volumes(const std::string& body) {
    std::vector<Volume> out;
    auto j = json::parse(body, nullptr, false);
    if (j.is_discarded()) throw std::runtime_error("invalid JSON from Books API");

    auto items = j.find("items");
    if (items == j.end() || !items->is_array()) return out;   // totalItems: 0

    for (const auto& item : *items) {
        auto vi_it = item.find("volumeInfo");
        if (vi_it == item.end() || !vi_it->is_object()) continue;
        const json& vi = *vi_it;

        Volume v;
        v.title          = str_field(vi, "title");
        v.subtitle       = str_field(vi, "subtitle");
        v.publisher      = str_field(vi, "publisher");
        v.published_date = str_field(vi, "publishedDate");
        v.language       = str_field(vi, "language");

        if (auto a = vi.find("authors"); a != vi.end() && a->is_array())
            for (const auto& name : *a)
                if (name.is_string()) v.authors.push_back(name.get<std::string>());

        out.push_back(std::move(v));
    }
    return out;
}
