#include "validate.hpp"

#include <cctype>

namespace apteka {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool valid_email(const std::string& s) {
    if (s.size() < 5 || s.size() > 190) return false; // a@b.c минимум
    auto at = s.find('@');
    if (at == std::string::npos || at == 0) return false;
    if (s.find('@', at + 1) != std::string::npos) return false; // второй '@'
    std::string local = s.substr(0, at);
    std::string domain = s.substr(at + 1);
    if (domain.size() < 3) return false;
    auto dot = domain.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= domain.size()) return false;
    if (domain.front() == '.' || domain.back() == '.') return false;
    if (domain.find("..") != std::string::npos) return false;
    for (unsigned char c : s) {
        if (c <= ' ' || c == 0x7f) return false; // пробелы и управляющие
    }
    for (unsigned char c : domain) {
        if (!(std::isalnum(c) || c == '-' || c == '.')) return false;
    }
    (void)local;
    return true;
}

static bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

bool valid_date(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    std::string ys = s.substr(0, 4), ms = s.substr(5, 2), ds = s.substr(8, 2);
    if (!all_digits(ys) || !all_digits(ms) || !all_digits(ds)) return false;
    int y = std::stoi(ys), m = std::stoi(ms), d = std::stoi(ds);
    if (y < 1900 || y > 2100 || m < 1 || m > 12 || d < 1) return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_d = days[m - 1];
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (m == 2 && leap) max_d = 29;
    return d <= max_d;
}

bool valid_positive_int(const std::string& s) {
    if (s.empty() || s.size() > 9 || !all_digits(s)) return false;
    return std::stol(s) > 0;
}

} // namespace apteka
