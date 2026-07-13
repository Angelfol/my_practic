#include "multipart.hpp"

#include <cctype>
#include <stdexcept>

#include "validate.hpp" // trim

namespace apteka {

namespace {

// Значение параметра boundary из Content-Type (возможно, в кавычках).
std::string extract_boundary(const std::string& content_type) {
    auto pos = content_type.find("boundary=");
    if (pos == std::string::npos) return "";
    std::string b = content_type.substr(pos + 9);
    auto semi = b.find(';');
    if (semi != std::string::npos) b = b.substr(0, semi);
    b = trim(b);
    if (b.size() >= 2 && b.front() == '"' && b.back() == '"') {
        b = b.substr(1, b.size() - 2);
    }
    return b;
}

// Значение подпараметра вида name="..." из Content-Disposition.
std::string disposition_param(const std::string& header, const std::string& name) {
    std::string needle = name + "=\"";
    auto pos = header.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    auto end = header.find('"', pos);
    if (end == std::string::npos) return "";
    return header.substr(pos, end - pos);
}

} // namespace

MultipartData parse_multipart(const std::string& content_type,
                              const std::string& body) {
    MultipartData out;
    std::string boundary = extract_boundary(content_type);
    if (boundary.empty()) {
        throw std::runtime_error("multipart: не найден boundary в Content-Type");
    }
    const std::string delim = "--" + boundary;

    // Части разделяются последовательностями: delim CRLF ... CRLF delim ... delim--
    size_t pos = body.find(delim);
    while (pos != std::string::npos) {
        pos += delim.size();
        if (body.compare(pos, 2, "--") == 0) break; // финальный разделитель
        if (body.compare(pos, 2, "\r\n") == 0) pos += 2;

        size_t hdr_end = body.find("\r\n\r\n", pos);
        if (hdr_end == std::string::npos) break;
        std::string headers = body.substr(pos, hdr_end - pos);
        size_t data_begin = hdr_end + 4;

        size_t next = body.find("\r\n" + delim, data_begin);
        if (next == std::string::npos) break;
        std::string data = body.substr(data_begin, next - data_begin);

        // Разбор заголовков части
        std::string disposition, part_type;
        size_t hp = 0;
        while (hp < headers.size()) {
            size_t he = headers.find("\r\n", hp);
            std::string line = headers.substr(
                hp, he == std::string::npos ? std::string::npos : he - hp);
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = trim(line.substr(0, colon));
                std::string val = trim(line.substr(colon + 1));
                for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (key == "content-disposition") disposition = val;
                else if (key == "content-type") part_type = val;
            }
            if (he == std::string::npos) break;
            hp = he + 2;
        }

        std::string field = disposition_param(disposition, "name");
        std::string filename = disposition_param(disposition, "filename");
        if (!field.empty()) {
            if (filename.empty() && disposition.find("filename=") == std::string::npos) {
                out.fields[field] = data;
            } else {
                FilePart fp;
                fp.field = field;
                fp.filename = filename;
                fp.content_type = part_type;
                fp.data = std::move(data);
                out.files.push_back(std::move(fp));
            }
        }

        pos = body.find(delim, next + 2);
    }
    return out;
}

} // namespace apteka
