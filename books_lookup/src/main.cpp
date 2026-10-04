#include "books_api.hpp"
#include <cstdlib>
#include <iostream>

class CurlGlobal {
public:
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("curl_global_init failed");
        }
    }

    ~CurlGlobal() {
        curl_global_cleanup();
    }

    CurlGlobal(const CurlGlobal&) = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

/*
 * Google console: https://console.cloud.google.com/welcome?project=book-project-510619
 * key = look in BBEdit note: books apple/kindle
 * could be get from environment
 * all parameters (title) should be UTF8 encoded
 */
int main() {
    const char* key = std::getenv("GOOGLE_BOOKS_KEY");
    if (!key) { std::cerr << "GOOGLE_BOOKS_KEY not set\n"; return 1; }

    CurlGlobal curl_guard{};
    int rc = 0;
    try {
        BooksClient client(key);
        BookQuery q{"The Book of Psalms", "norton"};

        for (const auto& v : parse_volumes(client.search(q))) {
            std::cout << v.title << (v.subtitle.empty() ? "" : ": " + v.subtitle)
                      << " | " << v.publisher << " | " << v.year() << " | ";
            for (size_t i = 0; i < v.authors.size(); ++i)
                std::cout << (i ? ", " : "") << v.authors[i];
            std::cout << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        rc = 1;
    }
    return rc;
}
