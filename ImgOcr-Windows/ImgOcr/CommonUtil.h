#ifndef COMMON_UTIL_H
#define COMMON_UTIL_H

#include <Windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

struct TFileFilter
{
    std::string filename;
    std::string pattern;
};

// UTF-8 문자열을 UTF-16으로 변환해서 메시지 박스에 표시
int ShowUtf8MessageBox(HWND owner, const std::string &message, const std::string &caption, UINT type = MB_OK);

// 파일 선택 대화상자 표시
std::string OpenFileDlgSingleFile(const std::vector<TFileFilter> &vecFilter);

// 상대 경로를 절대 경로로 변경, 경로 정리
std::string MakeAbsPath(const std::string &relativePath, bool exist = false);

// 파일 데이터를 std::vector<unsigned char> 타입의 버퍼로 로드
int ReadFileToVecBuf(const std::string &filename, std::vector<unsigned char> &vBuf);

// 문자열 앞뒤에 있는 공백제거 - 내부 공백은 유지
std::string Trim(const std::string &value);

// 문자열에서 공백 제거 - ' ', '\t', '\n', '\v', '\f', '\r' 제거
// 제거되는 공백 문자는 일반 공백, 수평 탭, 줄바꿈 LF, 수직 탭, 폼 피드, 캐리지 리턴 CR 
std::string RemoveAsciiWhiteSpace(const std::string &value);

// 모델/사전/템플릿 경로는 같은 ini 파일에서 읽음
// 상대 경로는 ini 파일 위치 기준
std::filesystem::path ReadOcrResourcePath(const std::filesystem::path &iniPath, const wchar_t *key);


#endif // common_util_h
