#include "OcrConfig.h"
#include "CommonUtil.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace
{
std::string UpperAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}
}

fs::path DefaultOcrConfigPath()
{
    std::error_code error;
    const auto executable = fs::read_symlink("/proc/self/exe", error);
    if (error) throw std::runtime_error("실행 파일 위치를 찾을 수 없습니다: " + error.message());
    const auto besideExecutable = executable.parent_path() / "config.ini";
    if (fs::is_regular_file(besideExecutable)) return besideExecutable;
    return (executable.parent_path() / "../share/imgocr/config.ini").lexically_normal();
}

OcrConfig LoadOcrConfig(const fs::path &file)
{
    const auto absolute = fs::absolute(file).lexically_normal();
    std::ifstream input(absolute, std::ios::binary);
    if (!input) throw std::runtime_error("설정 파일을 열 수 없습니다: " + absolute.string());

    std::map<std::string, std::string> values;
    std::string section, line;
    size_t number = 0;
    while (std::getline(input, line))
    {
        ++number;
        if (number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        line = Trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[')
        {
            if (line.back() != ']')
                throw std::runtime_error("잘못된 INI 섹션: " + absolute.string() + ":" + std::to_string(number));
            section = UpperAscii(Trim(line.substr(1, line.size() - 2)));
            continue;
        }
        if (section != "MODEL_PATH") continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            throw std::runtime_error("INI 설정에 '='가 필요합니다: " + absolute.string() + ":" + std::to_string(number));
        const auto key = UpperAscii(Trim(line.substr(0, equals)));
        auto value = Trim(line.substr(equals + 1));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);
        if (!values.emplace(key, value).second)
            throw std::runtime_error("중복 INI 설정: [MODEL_PATH] " + key);
    }
    if (input.bad()) throw std::runtime_error("설정 파일 읽기에 실패했습니다: " + absolute.string());

    const auto resolve = [&](const char *key)
    {
        const auto entry = values.find(key);
        if (entry == values.end() || entry->second.empty())
        {
            throw std::runtime_error(std::string("설정값이 필요합니다: [MODEL_PATH] ") + key);
        }
        auto resource = fs::u8path(entry->second);
        if (resource.is_relative()) resource = absolute.parent_path() / resource;
        resource = resource.lexically_normal();
        if (!fs::is_regular_file(resource))
            throw std::runtime_error(std::string("리소스 파일을 찾을 수 없습니다: ") + key + "=" + resource.string());
        return resource;
    };
    return {resolve("DET"), resolve("REC"), resolve("DIC"), resolve("TEMPLATE")};
}
