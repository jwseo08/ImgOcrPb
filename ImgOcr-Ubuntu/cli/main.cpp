#include "OcrConfig.h"
#include "ImgOcrEngine.h"
#include "ImgProc.h"
#include "IdCardParser.h"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace
{
constexpr const char *Help = R"(사용법: imgocr --input IMAGE [--output JSON] [--save-debug-image IMAGE]

  --input PATH             입력 이미지 (필수)
  --output PATH            JSON 결과를 추가로 저장
  --save-debug-image PATH  OCR 영역이 표시된 처리 이미지를 저장

신분증 필드와 OCR 원문은 항상 터미널에 출력합니다.
설정은 실행 파일 옆 config.ini, 또는 실행 파일 기준
../share/imgocr/config.ini에서 찾습니다. 출력 폴더는 미리 만들어야 합니다.

종료 코드: 0 성공, 1 리소스/처리/저장 오류, 2 잘못된 인자,
3 OCR 성공 후 신분증 필드 추출 실패 (OCR 원문은 출력).
)";

struct UsageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Options
{
    fs::path input, output, debugImage;
};

Options ParseOptions(int argc, char **argv)
{
    Options options;
    std::set<std::string> seen;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (!seen.insert(arg).second) throw UsageError("중복 옵션: " + arg);
        if (arg == "--input" || arg == "--output" || arg == "--save-debug-image")
        {
            if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0 ||
                std::string(argv[i + 1]).empty()) throw UsageError("옵션 값이 필요합니다: " + arg);
                
            const std::string value = argv[++i];
            
            if (arg == "--input") options.input = fs::u8path(value);
            else if (arg == "--output") options.output = fs::u8path(value);
            else options.debugImage = fs::u8path(value);
        }
        else throw UsageError("알 수 없는 옵션: " + arg);
    }
    if (options.input.empty()) throw UsageError("--input IMAGE가 필요합니다.");

    return options;
}

bool SameFile(const fs::path &first, const fs::path &second)
{
    std::error_code error;
    if (fs::equivalent(first, second, error)) return true;

    return fs::weakly_canonical(first) == fs::weakly_canonical(second);
}

void ValidateOutputs(const Options &options, const OcrConfig &config, const fs::path &configPath)
{
    const std::vector<fs::path> inputs = {options.input, configPath, config.detector,
                                       config.recognizer, config.dictionary, config.templates};
    std::vector<fs::path> outputs;
    for (const auto &file : {options.output, options.debugImage})
    {
        if (file.empty()) continue;

        for (const auto &input : inputs)
            if (!input.empty() && SameFile(file, input))
                throw UsageError("출력 경로가 입력/리소스 파일과 같습니다: " + file.string());

        for (const auto &output : outputs)
            if (SameFile(file, output)) throw UsageError("출력 경로가 서로 같습니다: " + file.string());

        const auto parent = fs::absolute(file).parent_path();

        if (!fs::is_directory(parent))
            throw std::runtime_error("출력 폴더가 없습니다: " + parent.string());

        if (fs::is_directory(file)) throw UsageError("출력 경로는 파일이어야 합니다: " + file.string());
        
        outputs.push_back(file);
    }
}

Json ParsedJson(const TParsedIdCard &parsed)
{
    Json fields = Json::array(), excluded = Json::array();
    for (const auto &field : parsed.orderedFields)
        fields.push_back({{"key", field.key}, {"display_name", field.displayName}, {"value", field.value}});

    for (const auto &box : parsed.excludedTextBoxes)
        excluded.push_back({{"text", box.text}, {"rule", box.ruleName}});

    return {{"success", parsed.success}, {"template_id", parsed.templateId},
            {"document_type", parsed.documentType}, {"fields", fields},
            {"errors", parsed.errors}, {"warnings", parsed.warnings},
            {"excluded_text_boxes", excluded}};
}

void SaveImage(const fs::path &file, const cv::Mat &image)
{
    if (!file.empty() && !cv::imwrite(file.string(), image))
        throw std::runtime_error("이미지 저장에 실패했습니다: " + file.string());
}

Json Process(const Options &options, const OcrConfig &config)
{
    Json result = {{"schema_version", 1}, {"input", fs::absolute(options.input).lexically_normal().string()},
                   {"success", false}, {"image", nullptr}, {"parsed", nullptr},
                   {"ocr", {{"success", false}, {"error", ""}, {"lines", Json::array()}}}};

    if (!fs::is_regular_file(options.input))
        throw std::runtime_error("입력 이미지가 없습니다: " + options.input.string());

    IdCardParser parser;
    std::string error;
    if (!parser.LoadTemplatesFromJson(config.templates, error))
        throw std::runtime_error(error);

    ImgOcrEngine engine;
    if (!engine.Initialize(config.detector.string(), config.recognizer.string(), config.dictionary.string(), error))
        throw std::runtime_error(error);

    ImgProc processor;
    TImgProcOption preprocessing;
    cv::Mat input;
    if (!processor.PrepareForOcr(options.input, input, error, preprocessing))
    {
        result["ocr"]["error"] = error;
        return result;
    }

    result["image"] = {{"width", input.cols}, {"height", input.rows},
                       {"coordinate_space", "preprocessed_image"}};

    TOcrResult ocr;
    if (!engine.Run(input, ocr))
    {
        result["ocr"]["error"] = ocr.errorMessage;
        return result;
    }

    result["ocr"]["success"] = true;
    std::vector<TIdCardOcrTextBox> boxes;
    for (const auto &line : ocr.lines)
    {
        Json corners = Json::array();
        float left = line.box[0].x, top = line.box[0].y, right = left, bottom = top;
        for (const auto &point : line.box)
        {
            corners.push_back({point.x, point.y});
            left = std::min(left, point.x); top = std::min(top, point.y);
            right = std::max(right, point.x); bottom = std::max(bottom, point.y);
        }
        result["ocr"]["lines"].push_back({{"text", line.text}, {"confidence", line.confidence}, {"box", corners}});
        boxes.push_back({line.text, left, top, right, bottom});
    }

    SaveImage(options.debugImage, ocr.visualizedImage);

    const auto parsed = parser.ParseTextBoxes(boxes, static_cast<float>(input.cols), static_cast<float>(input.rows));
    result["parsed"] = ParsedJson(parsed);
    result["success"] = parsed.success;

    return result;
}

std::string FormatText(const Json &result)
{
    std::ostringstream text;
    if (!result["parsed"].is_null())
    {
        text << "[신분증 파싱 결과]\n";
        for (const auto &field : result["parsed"]["fields"])
            text << field["display_name"].get<std::string>() << ": " << field["value"].get<std::string>() << '\n';

        for (const auto &error : result["parsed"]["errors"]) text << "오류: " << error.get<std::string>() << '\n';

        for (const auto &warning : result["parsed"]["warnings"]) text << "주의: " << warning.get<std::string>() << '\n';

        if (!result["parsed"]["excluded_text_boxes"].empty())
        {
            text << "\n[제외된 OCR 텍스트]\n";
            for (const auto &box : result["parsed"]["excluded_text_boxes"])
                text << box["text"].get<std::string>() << " [" << box["rule"].get<std::string>() << "]\n";
        }

        text << '\n';
    }

    text << "[OCR 원문]\n";
    
    if (!result["ocr"]["success"].get<bool>()) text << "오류: " << result["ocr"]["error"].get<std::string>() << '\n';
    else if (result["ocr"]["lines"].empty()) text << "인식된 텍스트가 없습니다.\n";

    for (const auto &line : result["ocr"]["lines"])
        text << line["text"].get<std::string>() << " [" << std::fixed << std::setprecision(1)
             << line["confidence"].get<float>() * 100 << "%]\n";

    return text.str();
}

void WriteResult(const Options &options, const Json &result)
{
    std::cout << FormatText(result);
    std::cout.flush();
    if (!std::cout) throw std::runtime_error("표준 출력 쓰기에 실패했습니다.");

    if (!options.output.empty())
    {
        std::ofstream file(options.output, std::ios::binary | std::ios::trunc);
        file << result.dump(2) << '\n';
        file.close();
        if (!file) throw std::runtime_error("결과 파일 쓰기에 실패했습니다: " + options.output.string());
    }
}
}

//---------------------------------------------------------------------

int main(int argc, char **argv)
{
    try
    {
        auto options = ParseOptions(argc, argv);

        const auto configPath = DefaultOcrConfigPath();
        const auto config = LoadOcrConfig(configPath);
        ValidateOutputs(options, config, configPath);
        const auto result = Process(options, config);
        WriteResult(options, result);

        if (!result["ocr"]["success"].get<bool>())
        {
            std::cerr << "imgocr: " << result["ocr"]["error"].get<std::string>() << '\n';
            return 1;
        }

        if (!result["success"].get<bool>())
        {
            std::cerr << "imgocr: OCR은 성공했지만 신분증 파싱에 실패했습니다. 터미널의 파싱 오류를 확인하세요.\n";
            return 3;
        }
        
        return 0;
    }
    catch (const UsageError &error)
    {
        std::cerr << "imgocr: " << error.what() << '\n' << Help;
        return 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << "imgocr: " << error.what() << '\n';
        return 1;
    }
}
