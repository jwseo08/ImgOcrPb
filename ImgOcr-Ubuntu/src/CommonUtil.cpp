#include "CommonUtil.h"

#include <cctype>
#include <fstream>
#include <limits>

int ReadFileToVecBuf(const std::filesystem::path &filename, std::vector<unsigned char> &vBuf)
{
    vBuf.clear();
    std::ifstream input(filename, std::ios::binary | std::ios::ate);
    if (!input) return -1;

    const auto size = input.tellg();
    if (size < 0) return -3;
    if (size == 0) return -6;
    const auto count = static_cast<unsigned long long>(size);
    if (count > vBuf.max_size() || count > static_cast<unsigned long long>(
            (std::numeric_limits<std::streamsize>::max)())) return -5;

    input.seekg(0, std::ios::beg);
    if (!input) return -4;
    vBuf.resize(static_cast<size_t>(count));
    if (!input.read(reinterpret_cast<char *>(vBuf.data()), static_cast<std::streamsize>(count)))
    {
        vBuf.clear();
        return -7;
    }
    return 0;
}

std::string Trim(const std::string &value)
{
    // 문자열 앞쪽에 있는 공백 찾음 - 공백 아닌 문자를 만날 때까지 진행
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }

    // 뒤쪽 공백 찾음 - 역방향으로 공백 아닌 문자를 만날 때까지 진행
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }

    // 앞뒤 공백이 제거된 문자열 반환
    return value.substr(begin, end - begin);
}

std::string RemoveAsciiWhiteSpace(const std::string &value)
{
    std::string result;
    result.reserve(value.size());
    for (unsigned char ch : value)
    {
        if (!std::isspace(ch))
            result.push_back(static_cast<char>(ch));
    }
    return result;
}
