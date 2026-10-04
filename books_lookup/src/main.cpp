#include "books_api.hpp"
#include <cstdlib>
#include <fstream>
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
  * Load KEY=VALUE pairs from a .env file into the process environment.
  * Lines that are blank or start with '#' are ignored. An optional
  * leading "export " is stripped, as are surrounding single/double
  * quotes around the value. Keys already present in the environment
  * are left untouched, so real env vars win over the file.
  */
 static void load_dotenv(const std::string& path) {
     std::ifstream file{path};
     if (!file) {
         return;  // no .env file is fine; fall back to the real environment
     }

     std::string line{};
     while (std::getline(file, line)) {
         // trim leading whitespace
         size_t begin = line.find_first_not_of(" \t\r\n");
         if (begin == std::string::npos) {
             continue;
         }
         line.erase(0, begin);

         if (line.empty() || line[0] == '#') {
             continue;
         }

         if (line.rfind("export ", 0) == 0) {
             line.erase(0, 7);
         }

         size_t eq = line.find('=');
         if (eq == std::string::npos) {
             continue;
         }

         std::string key = line.substr(0, eq);
         std::string value = line.substr(eq + 1);

         // trim trailing whitespace from key
         size_t key_end = key.find_last_not_of(" \t");
         if (key_end == std::string::npos) {
             continue;
         }
         key.erase(key_end + 1);

         // trim surrounding whitespace from value
         size_t val_begin = value.find_first_not_of(" \t\r\n");
         if (val_begin == std::string::npos) {
             value.clear();
         } else {
             size_t val_end = value.find_last_not_of(" \t\r\n");
             value = value.substr(val_begin, val_end - val_begin + 1);
         }

         // strip one layer of matching surrounding quotes
         if (value.size() >= 2 &&
             (value.front() == '"' || value.front() == '\'') &&
             value.back() == value.front()) {
             value = value.substr(1, value.size() - 2);
         }

         // 0 = do not overwrite an existing environment variable
         setenv(key.c_str(), value.c_str(), 0);
     }
 }


/*
 * Google console: https://console.cloud.google.com/welcome?project=book-project-510619
 * key = look in BBEdit note: books apple/kindle
 * could be get from environment
 * all parameters (title) should be UTF8 encoded
 */
int main() {
    load_dotenv(".env");
    
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
