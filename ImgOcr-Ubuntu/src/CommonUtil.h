#pragma once

#include <filesystem>
#include <string>
#include <vector>

// 파일 데이터를 버퍼로 읽기. 성공하면 0, 실패하면 음수 반환.
int ReadFileToVecBuf(const std::filesystem::path &filename, std::vector<unsigned char> &vBuf);

// 문자열 앞뒤의 ASCII 공백 제거.
std::string Trim(const std::string &value);

// 문자열 전체의 ASCII 공백 제거.
std::string RemoveAsciiWhiteSpace(const std::string &value);
