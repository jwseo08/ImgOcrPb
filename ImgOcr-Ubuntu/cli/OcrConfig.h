#pragma once

#include <filesystem>

struct OcrConfig
{
    std::filesystem::path detector;
    std::filesystem::path recognizer;
    std::filesystem::path dictionary;
    std::filesystem::path templates;
};

// Values are UTF-8; relative resource paths are resolved against the INI file.
OcrConfig LoadOcrConfig(const std::filesystem::path &file);
std::filesystem::path DefaultOcrConfigPath();
