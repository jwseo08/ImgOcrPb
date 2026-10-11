#include "pch.h"
#include "CommonUtil.h"

#include <iostream>
#include <cctype>
#include <stdio.h>
#include <shobjidl.h>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace fs = std::filesystem;

int ShowUtf8MessageBox(HWND owner, const std::string &message, const std::string &caption, UINT type)
{
    auto utf8ToWide = [](const std::string &text)
    {
        if (text.empty())
        {
            return std::wstring();
        }

        const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (length <= 0)
        {
            return std::wstring(L"문자열 변환 오류");
        }

        std::wstring converted(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, converted.data(), length);
        return converted;
    };

    const std::wstring wideMessage = utf8ToWide(message);
    const std::wstring wideCaption = utf8ToWide(caption);

    return ::MessageBoxW(owner, wideMessage.c_str(), wideCaption.c_str(), type);
}

std::string OpenFileDlgSingleFile(const std::vector<TFileFilter> &vecFilter)
{
    std::string filter = "";
    for (const auto &filterItem : vecFilter)
    {
        filter += _T(filterItem.filename);
        filter += _T("|");
        filter += _T(filterItem.pattern);
        filter += _T("|");
    }
    filter += _T("|");

    CFileDialog dlg(TRUE, NULL, NULL, 0 /*OFN_HIDEREADONLY*/, filter.c_str());
    if (dlg.DoModal() == IDOK)
    {
        return MakeAbsPath(dlg.GetPathName().GetString());
    }
    else
    {
        return std::string();
    }    
}

// 상대 경로를 절대 경로로 변경하고 경로 정리
std::string MakeAbsPath(const std::string &relativePath, bool exist /*=false*/)
{
    if (exist)
    {
        // 경로가 실제로 존재하는지 검사해야하는 경우
        try
        {
            // 파일이 존재하지 않으면 exception 발생 try catch 필요
            fs::path realPath = fs::canonical(relativePath);
            return realPath.generic_string();
        }
        catch (const fs::filesystem_error &e)
        {
            // 파일이 없거나 권한이 없을 때의 예외 처리
            std::cerr << "make canonical path fail=" << e.what() << '\n';

            // 실패 시 빈 문자열 반환
            return "";
        }
    }
    else
    {
        // 경로가 존재히지는 것과 상관없이 절대 경로 변환
        fs::path realPath = fs::absolute(relativePath).lexically_normal();
        return realPath.generic_string();
    }
}

int ReadFileToVecBuf(const std::string &filename, std::vector<unsigned char> &vBuf)
{
    FILE *fp = fopen(filename.c_str(), "rb");

    if (!fp)
    {
        perror("file opening failed");
        return -1;
    }

#ifdef _WIN32

    if (_fseeki64(fp, 0, SEEK_END) != 0)
    {
        perror("file seek failed");
        fclose(fp);
        return -2;
    }

    long long fileSize = _ftelli64(fp);

    if (fileSize < 0)
    {
        perror("file size calculation failed");
        fclose(fp);
        return -3;
    }

    if (_fseeki64(fp, 0, SEEK_SET) != 0)
    {
        perror("file seek failed");
        fclose(fp);
        return -4;
    }

#else

    if (fseeko(fp, 0, SEEK_END) != 0)
    {
        perror("file seek failed");
        fclose(fp);
        return -2;
    }

    off_t fileSize = ftello(fp);

    if (fileSize < 0)
    {
        perror("file size calculation failed");
        fclose(fp);
        return -3;
    }

    if (fseeko(fp, 0, SEEK_SET) != 0)
    {
        perror("file seek failed");
        fclose(fp);
        return -4;
    }

#endif

    if (static_cast<unsigned long long>(fileSize) > SIZE_MAX)
    {
        std::cerr << "File is too large to fit in memory." << std::endl;
        fclose(fp);
        return -5;
    }

    size_t bufSize = static_cast<size_t>(fileSize);

    if (bufSize == 0)
    {
        vBuf.clear();
        fclose(fp);
        return -6;
    }

    if (vBuf.size() != bufSize)
        vBuf.resize(bufSize);

    size_t readSize = fread(vBuf.data(), 1, bufSize, fp);

    if (readSize != bufSize)
    {
        std::cerr << "file read failed" << std::endl;
        fclose(fp);
        vBuf.clear();
        return -7;
    }

    fclose(fp);

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

std::filesystem::path ReadOcrResourcePath(const std::filesystem::path &iniPath, const wchar_t *key)
{
    const auto absoluteIni = std::filesystem::absolute(iniPath).lexically_normal();

    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    const DWORD length = ::GetPrivateProfileStringW(L"MODEL_PATH", key, L"", buffer.data(),
                                                    static_cast<DWORD>(buffer.size()), absoluteIni.c_str());

    if (length == 0) return {};

    std::filesystem::path resource(std::wstring(buffer.data(), length));
    if (resource.is_relative()) resource = absoluteIni.parent_path() / resource;

    return resource.lexically_normal();
}
