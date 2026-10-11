#include "ImgOcrEngine.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace
{
// 검출 모델의 DB 후처리 경계값 - det.yml의 PostProcess 설정과 동일하게 설정
constexpr float DET_THRESH = 0.20f;    // 텍스트 확률 맵의 이진화 경계값 - 경계값으로 텍스트 후보와 배경 나눔
constexpr float BOX_THRESH = 0.45f;    // 텍스트 박스 선정을 위한 경계값 - 박스 영역에 속한 텍스트 후보의 확률값 평균으로 박스 선정 
constexpr float UNCLIP_RATIO = 1.40f;

constexpr int DET_MAX_SIDE = 960;      // 검출기 입력 이미지 긴 변을 960 이하로 제한 - 모델 네트워크 다운샘플링 배수에 맞춰서 32의 배수로 지정
constexpr int MAX_CANDIDATES = 3000;   // 검출된 텍스트 후보의 경계선의 최대 크기 - 최대 크기보다 크면 크기 조정
constexpr int REC_HEIGHT = 48;         // 인식기 입력 이미지 높이는 48로 고정
constexpr int REC_BASE_WIDTH = 320;    // 인식기 입력 이미지 너비 최소 크기 - 문자열 비율에 따라 320~3200 범위에서 동적으로 사용
constexpr int REC_MAX_WIDTH = 3200;    // 인식기 입력 이미지 너비 최대 크기

// value 값 이상인 값 중에서 divisor로 나누어 떨어지는 가장 작은 값을 반환
// 동적 입력 모델의 입력 크기를 모델 네트워크 stride의 배수에 맞추기 위해서 사용
// (ex) stride가 32일 때 입력 크기가 641이면 672로, 640이면 640으로 맞춤
int RoundUp(int value, int divisor)
{
    return std::max(divisor, ((value + divisor - 1) / divisor) * divisor);
}

// 두 점 사이의 직선거리 계산
float Dist(const cv::Point2f &a, const cv::Point2f &b)
{
    return static_cast<float>(cv::norm(a - b));
}

// detector 입력 텐서 생성을 위한 데이터 구성 - 텐서 생성을 위한 이미지 전처리
// 이미지 정규화, HWC uint8 -> CHW float32 변환
std::vector<float> DetectorTensor(const cv::Mat &image)
{
    // det.yml의 NormalizeImage 설정 적용
    // 이미지 채널 순서는 PaddleOCR 설정에 따라서 BGR 순서 - opencv mat 순서와 동일
    const float mean[] = {0.485f, 0.456f, 0.406f};
    const float invStd[] = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};

    // opencv HWC uint8 배열 -> onnx [1, 3, H, W] float32 CHW 연속 버퍼로 변환
    const size_t plane = static_cast<size_t>(image.rows) * image.cols;
    std::vector<float> data(plane * 3);
    for (int y = 0; y < image.rows; ++y)
    {
        const cv::Vec3b *row = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < image.cols; ++x)
        {
            const size_t offset = static_cast<size_t>(y) * image.cols + x;
            for (int c = 0; c < 3; ++c)
            {
                data[c * plane + offset] = (row[x][c] / 255.0f - mean[c]) * invStd[c];
            }
        }
    }

    return data;
}

// recognizer 입력 텐서 생성을 위한 전처리
// 입력 이미지 정규화, HWC -> CHW 변환, 지정된 입력 너비까지 오른쪽 영역을 -1.0으로 패딩
// paddle ocr recognizer 입력 이미지 정규화는 픽셀값 / 127.5 - 1.0
// 정규화 결과 범위는 [-1, 1]
std::vector<float> RecognizerTensor(const cv::Mat &image, int width)
{
    size_t plane = static_cast<size_t>(REC_HEIGHT) * width;

    // 정규화 결과 버퍼를 -1.0으로 초기화 - 정규화 값과 동일하게 맞춘 검은색
    std::vector<float> data(plane * 3, -1.0f);
    
    for (int y = 0; y < image.rows; ++y)
    {
        const cv::Vec3b *row = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < image.cols; ++x)
        {
            const size_t offset = static_cast<size_t>(y) * width + x;
            for (int c = 0; c < 3; ++c)
            {
                data[c * plane + offset] = row[x][c] / 127.5f - 1.0f;
            }
        }
    }
    return data;
}

// detect된 텍스트 후보 영역에 대해서 실제 텍스트 영역인지 확률 점수 계산
float BoxScore(const cv::Mat &probability, const std::vector<cv::Point> &contour)
{
    // detect된 텍스트 후보의 경계선을 감싸는 사각형 생성 - 마스크 생성을 위한 사각형
    // 마스크 생성을 위한 사각형은 텍스트 후보 경계선을 모두 포함하고 확률 맵을 벗어나지 않도록 조정
    const cv::Rect bounds = cv::boundingRect(contour) & cv::Rect(0, 0, probability.cols, probability.rows);
    if (bounds.empty()) return 0.0f;
    
    // 텍스트 후보 경계선 내부 영역을 마스크에 표시하기 위해서 
    // 텍스트 후보 경계선 좌표를 위에서 생성한 사각형 영역 기준으로 변환
    std::vector<cv::Point> local;
    for (const auto &p : contour)
    {
        local.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }

    // 사각형, 검은색으로 마스크를 생성하고 텍스트 윤곽선 내부 영역에 해당하는 부분을 흰색으로 채움
    cv::Mat mask(bounds.height, bounds.width, CV_8U, cv::Scalar(0));
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{local}, cv::Scalar(255));

    // 텍스트 윤곽선 내부 영역이 표시된 마스크를 이용해서 텍스트 확률 평균 계산 - confidence score
    // probability map에서 contour 내부의 확률만 선택해서 평균 계산
    return static_cast<float>(cv::mean(probability(bounds), mask)[0]);
}
} // namespace

ImgOcrEngine::ImgOcrEngine() : m_environment(ORT_LOGGING_LEVEL_WARNING, "ImgOcr") // onnx runtime 로그 레벨을 warning 레벨로 설정
{
    // onnx runtime 옵션 설정
    // 모델 그래프 최적화 설정 - 최적화 모두 사용
    m_sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // onnx runtime 사용 스레드 수량 설정 - 사용가능 스레드 수량의 절반 사용
    // 최소 한 개 보장, UI 응답 여유 남김
    m_sessionOptions.SetIntraOpNumThreads(std::max(1u, std::thread::hardware_concurrency() / 2));
}

ImgOcrEngine::~ImgOcrEngine()
{

}

bool ImgOcrEngine::Initialize(const std::string &detectorModelPath, const std::string &recognizerModelPath,
                            const std::string &dictionaryPath, std::string &errorMessage)
{
    errorMessage.clear();
    m_initialized = false;
    
    try
    {
        // 모델 경로 타입에 맞도록 std::filesystem::path로 변환 - 윈도우용 onnx runtime은 wchar_t 모델 경로 사용
        const std::filesystem::path detPath(detectorModelPath);   // detector 모델 파일 경로
        const std::filesystem::path recPath(recognizerModelPath); // recognizer 모델 파일 경로

        // dectector, recognizer 세션 생성
        m_detectorSession = std::make_unique<Ort::Session>(m_environment, detPath.c_str(), m_sessionOptions);
        m_recognizerSession = std::make_unique<Ort::Session>(m_environment, recPath.c_str(), m_sessionOptions);
        
        // 모델 메타데이터에서 입출력 이름 조회
        Ort::AllocatorWithDefaultOptions allocator;
        auto di = m_detectorSession->GetInputNameAllocated(0, allocator);
        auto dout = m_detectorSession->GetOutputNameAllocated(0, allocator);
        auto ri = m_recognizerSession->GetInputNameAllocated(0, allocator);
        auto rout = m_recognizerSession->GetOutputNameAllocated(0, allocator);

        // 입출력 이름 설정
        m_detectorInputName = di.get();
        m_detectorOutputName = dout.get();
        m_recognizerInputName = ri.get();
        m_recognizerOutputName = rout.get();

        if (!LoadDictionary(dictionaryPath, errorMessage)) 
        {
            m_detectorSession.reset();
            m_recognizerSession.reset();

            return false;
        }
        
        m_initialized = true;
        return true;
    }
    catch (const Ort::Exception &e)
    {
        errorMessage = std::string("ONNX Runtime 초기화 오류: ") + e.what();
    }
    catch (const std::exception &e)
    {
        errorMessage = std::string("OCR 초기화 오류: ") + e.what();
    }
    
    m_detectorSession.reset();
    m_recognizerSession.reset();
    
    return false;
}

bool ImgOcrEngine::LoadDictionary(const std::string &path, std::string &errorMessage)
{
    m_dictionary.clear();

    // 파일 확장자 기준으로 일반 텍스트 파일과 yaml 파일 구분
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    // yaml 파일인 경우 분기
    if (extension == ".yml" || extension == ".yaml")
    {
        return LoadDictionaryFromYaml(path, errorMessage);
    }

    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file)
    {
        errorMessage = "OCR 사전을 열 수 없습니다: " + path;
        return false;
    }

    // 일반 텍스트 피일인 경우 라인 단위로 로드
    std::string line;
    while (std::getline(file, line))
    {
        // CRLF의 CR과 UTF-8 BOM 제거
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (m_dictionary.empty() && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef &&
            static_cast<unsigned char>(line[1]) == 0xbb && static_cast<unsigned char>(line[2]) == 0xbf)
        {
            line.erase(0, 3);
        }

        m_dictionary.push_back(line);
    }
        
    // paddle ocr recognizer의 use_space_char=true 설정에 맞춰서 마지막에 space 추가
    m_dictionary.emplace_back(" ");
    return true;
}

bool ImgOcrEngine::LoadDictionaryFromYaml(const std::string &path, std::string &errorMessage)
{
    m_dictionary.clear();
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file)
    {
        errorMessage = "OCR YAML 사전을 열 수 없습니다: " + path;
        return false;
    }

    bool reading = false;
    std::string line;

    // paddle ocr yaml 파일에서 postprocess.character_dict 항목 로드
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (!reading)
        {
            if (line.find("character_dict:") != std::string::npos)
            {
                reading = true;
            }
            continue;
        }

        if (line.rfind("  - ", 0) != 0)
        {
            break;
        }

        std::string value = line.substr(4);

        // 작은 따옴표 처리 - '' 는 '를 의미함
        // 큰 따옴표 처리 - 큰따옴표로 둘러싸인 값에서 바깥 따옴표 제거 - 실제 문자만 저장
        if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
        {
            value = value.substr(1, value.size() - 2);
            size_t position = 0;

            while ((position = value.find("''", position)) != std::string::npos)
            {
                value.replace(position, 2, "'");
                ++position;
            }
        }
        else if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        {
            value = value.substr(1, value.size() - 2);
        }

        m_dictionary.push_back(value);
    }

    if (m_dictionary.empty())
    {
        errorMessage = "YAML 사전에서 character_dict를 읽지 못했습니다: " + path;
        return false;
    }

    // use_space_char=true에 맞춰서 space 추가
    m_dictionary.emplace_back(" ");
    return true;
}

bool ImgOcrEngine::Run(const cv::Mat &image, TOcrResult &result)
{
    // 결과 초기화
    result = TOcrResult();
    
    if (!m_initialized)
    {
        result.errorMessage = "OCR 엔진이 초기화되지 않았습니다.";
        return false;
    }
    
    if (image.empty())
    {
        result.errorMessage = "OCR 입력 이미지가 비어 있습니다.";
        return false;
    }
    
    try
    {
        // 전체 이미지에서 텍스트 영역 검출 - 텍스트 박스 생성
        std::vector<TextBox> boxes;
        if (!DetectText(image, boxes, result.errorMessage))
        {
            return false;
        }
        
        // 검출된 텍스트 영역에 대한 입력 이미지 크롭과 문자 인식
        // 인식에 성공한 영역을 별도의 결과 이미지에 녹색 사각형으로 표시
        result.visualizedImage = image.clone();
        for (const auto &box : boxes)
        {
            // 전체 이미지에서 텍스트 박스 별로 이미지를 잘라냄 - 잘라내면서 원근 보정
            // 텍스트 박스 기반으로 recognizer에 입력되는 각각의 이미지 생성
            TOcrTextLine line;
            line.box = box;
            cv::Mat crop = CropTextImage(image, box);
            
            if (crop.empty()) continue;
            
            // 각각의 크롭 이미지에 대해서 텍스트 인식
            if (!RecognizeText(crop, line.text, line.confidence, result.errorMessage)) 
            {
                return false;
            }

            if (line.text.empty()) continue;

            // 인식된 텍스트 저장
            result.lines.push_back(line);

            // 인식된 텍스트 박스를 녹색으로 표시
            std::vector<cv::Point> polygon;
            for (const auto &p : box)
            {
                polygon.emplace_back(cvRound(p.x), cvRound(p.y));
            }
            cv::polylines(result.visualizedImage, polygon, true, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        }

        result.success = true;
        return true;
    }
    catch (const Ort::Exception &e)
    {
        result.errorMessage = std::string("ONNX 추론 오류: ") + e.what();
    }
    catch (const cv::Exception &e)
    {
        result.errorMessage = std::string("OpenCV OCR 오류: ") + e.what();
    }
    catch (const std::exception &e)
    {
        result.errorMessage = std::string("OCR 오류: ") + e.what();
    }
    return false;
}

// 텍스트 영역 검출
bool ImgOcrEngine::DetectText(const cv::Mat &image, std::vector<TextBox> &boxes, std::string &errorMessage)
{
    boxes.clear();

    // detector 입력 전처리를 위해 입력 이미지 형식 통일 - 3채널 BGR 형식
    cv::Mat bgr;
    if (image.channels() == 1) 
{
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
    }
    else if (image.channels() == 4)
    {
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
    }
    else if (image.channels() == 3)
    {
        bgr = image;
    }
    else
    {
        errorMessage = "지원하지 않는 이미지 채널입니다.";
        return false;
    }

    // 입력 이미지를 긴 변 기준으로 가로세로 비율을 유지하고 각 변을 32의 배수로 조정해서 리사이즈
    // 조정 과정에서 발생하는 미세한 비율 차이는 마지막 좌표 복원에서 sx, sy를 각각 적용해서 원본 좌표계에 맞춤
    // 긴 변이 DET_MAX_SIDE 보다 크면 축소하고 작거나 같으면 확대 안함
    const float scale = std::min(1.0f, static_cast<float>(DET_MAX_SIDE) / std::max(bgr.cols, bgr.rows));
    const int width = RoundUp(cvRound(bgr.cols * scale), 32), height = RoundUp(cvRound(bgr.rows * scale), 32);

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(width, height), 0, 0, scale < 1 ? cv::INTER_AREA : cv::INTER_LINEAR);
    
    // 리사이즈된 이미지로 입력 텐서 생성 - 텐서 형식은 [batch, channel, height, width]
    std::vector<float> data = DetectorTensor(resized);  // 입력 텐서 생성을 위한 이미지 전처리
    const std::array<int64_t, 4> inputShape{1, 3, height, width};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input = Ort::Value::CreateTensor<float>(mem, data.data(), data.size(), inputShape.data(), inputShape.size());
    
    const char *inNames[]{m_detectorInputName.c_str()};
    const char *outNames[]{m_detectorOutputName.c_str()};
    
    // 텍스트 검출 추론 실행
    auto outputs = m_detectorSession->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
    if (outputs.empty() || !outputs[0].IsTensor())
    {
        errorMessage = "검출기 출력 텐서가 없습니다.";
        return false;
    }
    
    // detector 출력 텐서 형태 검사 - db 검출기의 출력은 [batch, channel, height, width] probability map
    const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4 || shape[0] != 1 || shape[1] < 1 || shape[2] <= 0 || shape[3] <= 0)
    {
        errorMessage = "검출기 출력 텐서 형식이 올바르지 않습니다.";
        return false;
    }

    // 출력 텐서 데이터를 cv::Mat 형태의 probability map 으로 매핑 - 텍스트 확률 지도 생성
    int mh = static_cast<int>(shape[2]);
    int mw = static_cast<int>(shape[3]);
    cv::Mat probability(mh, mw, CV_32F, const_cast<float *>(outputs[0].GetTensorData<float>()));
    
    // 확률이 DET_THRESH 이상인 픽셀만 이진화 - 텍스트 후보 경계선을 찾기 위한 이진화
    cv::Mat binary;
    cv::threshold(probability, binary, DET_THRESH, 255, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8U);

    // 이진화된 확률 맵에서 텍스트 후보 경계선들을 검출하고 목록 생성
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    if (contours.size() > MAX_CANDIDATES) contours.resize(MAX_CANDIDATES);
    
    // probability map 좌표를 원본 이미지 좌표로 복원하기 위한 가로세로 scale 계산
    float sx = static_cast<float>(bgr.cols) / mw;
    float sy = static_cast<float>(bgr.rows) / mh;
    
    // 텍스트 후보 경계선을 검사해서 유효한 텍스트 박스 목록 생성
    for (const auto &contour : contours)
    {
        // 너무 작은 후보 영역은 제외
        // 내부 영역의 텍스트 확률 평균이 임계값보다 낮은 후보 영역은 제외 
        if (contour.size() < 3 || 
            std::abs(cv::contourArea(contour)) < 4 || 
            BoxScore(probability, contour) < BOX_THRESH)
        {
            continue;
        }

        // 후보 경계선을 감싸는 사각형 박스 좌표 계산
        cv::RotatedRect rect = cv::minAreaRect(contour);
        
        // 사각형 박스가 유효한지 검사
        if (std::min(rect.size.width, rect.size.height) < 3)
        {
            continue;
        }

        // 글자 윤곽선 영역 * unclip_ratio / perimeter만큼 사각형 확장
        // db 검출 결과는 텍스트 내부의 중심 영역 위주로 형성되므로 실제 문자 영역과 주변 여백을 충분히 포함하도록 확장
        // unclip_ratio 는 1.4

        // 텍스트 후보 경계선 둘레 길이를 측정하고 유효하면 확장
        const double perimeter = cv::arcLength(contour, true); 
                
        float expand = 0.0f;
        if (perimeter > 0.0f) 
        {
            expand = (float)(std::abs(cv::contourArea(contour)) * UNCLIP_RATIO / perimeter);
        }
        else 
        {
            expand = 0;
        }
                
        rect.size.width += 2 * expand;
        rect.size.height += 2 * expand;
        cv::Point2f raw[4];
        rect.points(raw);
        TextBox box = OrderBox(raw);

        // probability map 좌표를 원본 입력 이미지 좌표로 복원
        // sx, sy로 좌표 차이를 조정하고 이미지 경계를 벗어나지 않도록 제한
        for (auto &p : box)
        {
            p.x = std::clamp(p.x * sx, 0.0f, static_cast<float>(bgr.cols - 1));
            p.y = std::clamp(p.y * sy, 0.0f, static_cast<float>(bgr.rows - 1));
        }

        // 조정된 텍스트 박스의 가로세로 크기가 일정 크기 이상이면 텍스트 박스 목록에 추가
        // recognizer로 전달되는 유효한 텍스트 박스 목록
        if (Dist(box[0], box[1]) >= 3 && Dist(box[0], box[3]) >= 3)
        {
            boxes.push_back(box);
        }
    }
    
    // 텍스트 박스 순서 정렬
    // 인식 작업에 넘기기 전에 인식 순서와 결과 문자열이 일반적인 읽기 순서가 되도록 정렬
    SortBoxes(boxes);
    return true;
}

bool ImgOcrEngine::RecognizeText(const cv::Mat &crop, std::string &text, float &confidence, std::string &errorMessage)
{
    text.clear();
    confidence = 0;

    // 인식기 입력 전처리를 위해서 입력 이미지 형식 통일 - 3채널 BGR로 변환
    cv::Mat bgr;
    if (crop.channels() == 1)
    {
        cv::cvtColor(crop, bgr, cv::COLOR_GRAY2BGR);
    }
    else if (crop.channels() == 4)
    {
        cv::cvtColor(crop, bgr, cv::COLOR_BGRA2BGR);
    }
    else if (crop.channels() == 3)
    {
        bgr = crop;
    }
    else
    {
        errorMessage = "지원하지 않는 이미지 채널입니다.";
        return false;
    }

    // 입력 이미지 세로 길이는 48로 고정 - REC_HEIGHT 48
    // 입력 이미지 가로 길이는 가로세로 비율을 맞춰서 실제 글자 너비로 계산 -  모델 입력 가로 길이는 최소 320, 최대 3200
    // 남는 오른쪽 영역은 RecognizerTensor()가 -1.0으로 패딩
    const float ratio = static_cast<float>(bgr.cols) / std::max(1, bgr.rows);
    const int actualWidth = std::max(1, static_cast<int>(std::ceil(REC_HEIGHT * ratio)));
    const int width = std::clamp(RoundUp(std::max(REC_BASE_WIDTH, actualWidth), 8), REC_BASE_WIDTH, REC_MAX_WIDTH);

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(std::min(width, actualWidth), REC_HEIGHT));

    // 입력 텐서 생성을 위한 이미지 전처리
    std::vector<float> data = RecognizerTensor(resized, width);
    
    // recognizer 입력 텐서 생성 - 텐서 형태는 [batch, channel, height, width]
    const std::array<int64_t, 4> inputShape{1, 3, REC_HEIGHT, width};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input = Ort::Value::CreateTensor<float>(mem, data.data(), data.size(), inputShape.data(), inputShape.size());
    
    const char *inNames[]{m_recognizerInputName.c_str()};
    const char *outNames[]{m_recognizerOutputName.c_str()};
    
    // 텍스트 인식 추론 실행
    auto outputs = m_recognizerSession->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
    if (outputs.empty() || !outputs[0].IsTensor())
    {
        errorMessage = "인식기 출력 텐서가 없습니다.";
        return false;
    }

    // 출력 텐서 형태 검사 - recognizer 출력 형태는 [batch, time steps, classes]
    const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3 || 
        shape[0] != 1 || 
        shape[1] <= 0 || 
        shape[2] <= 1)
    {
        errorMessage = "인식기 출력 텐서 형식이 올바르지 않습니다.";
        return false;
    }

    const int steps = static_cast<int>(shape[1]);
    const int classes = static_cast<int>(shape[2]);
    
    // 문자 사전 크기 검사
    // 모델 출력 클래스 수와 문자 사전 크기 비교 - 다르면 다른 버전의 사전을 연결한 것이므로 오류 반환
    const int expectedClasses = static_cast<int>(m_dictionary.size()) + 1; //  CTC blank가 index 0를 사용하므로 +1 추가 
    if (classes != expectedClasses)
    {
        errorMessage = "인식기 클래스 수(" + std::to_string(classes) + ")와 OCR 사전/공백/CTC blank 수(" +
                       std::to_string(expectedClasses) + ")가 일치하지 않습니다.";
        return false;
    }

    // 출력 텐서 데이터를 실제 문자로 디코딩 - greedy CTC 디코딩 방식
    // 각 time step에서 확률이 가장 높은 클래스 선택
    // blank(0) 제거
    // 연속해서 반복된 동일 클래스를 한 문자로 합침
    // 선택된 실제 문자들의 출력 점수 평균을 해당 텍스트 영역의 신뢰도로 사용
    
    const float *output = outputs[0].GetTensorData<float>();
    int previous = -1;
    int count = 0;
    float total = 0;
    
    for (int t = 0; t < steps; ++t)
    {
        const float *row = output + static_cast<size_t>(t) * classes;
        const int index = static_cast<int>(std::max_element(row, row + classes) - row);
        if (index != 0 && index != previous)
        {
            const size_t dictionaryIndex = static_cast<size_t>(index - 1);
            if (dictionaryIndex >= m_dictionary.size())
            {
                errorMessage = "인식기 클래스 수와 OCR 사전이 일치하지 않습니다.";
                return false;
            }
            text += m_dictionary[dictionaryIndex];
            total += row[index];
            ++count;
        }
        previous = index;
    }

    // 인식된 각 문자의 confidence 평균을 전체 문자열 confidence로 사용
    if (count)
        confidence = total / count;
    else
        confidence = 0;

    return true;
}

ImgOcrEngine::TextBox ImgOcrEngine::OrderBox(const cv::Point2f p[4])
{
    // x + y가 가장 작은 점이 좌상단, 가장 큰 점이 우하단
    // y - x가 가장 작은 점은 우상단, 가장 큰 점은 좌하단
    TextBox out;
    
    int tl = 0; 
    int tr = 0; 
    int br = 0; 
    int bl = 0;
    
    float minSum = FLT_MAX; 
    float maxSum = -FLT_MAX; 
    float minDiff = FLT_MAX; 
    float maxDiff = -FLT_MAX;
   
    for (int i = 0; i < 4; ++i)
    {
        const float sum = p[i].x + p[i].y, diff = p[i].y - p[i].x;
        if (sum < minSum)
        {
            minSum = sum;
            tl = i;
        }
        if (sum > maxSum)
        {
            maxSum = sum;
            br = i;
        }
        if (diff < minDiff)
        {
            minDiff = diff;
            tr = i;
        }
        if (diff > maxDiff)
        {
            maxDiff = diff;
            bl = i;
        }
    }
    out = {p[tl], p[tr], p[br], p[bl]};
    return out;
}

cv::Mat ImgOcrEngine::CropTextImage(const cv::Mat &image, const TextBox &box)
{
    // 텍스트 박스의 상하좌우 변 길이를 기준으로 크롭 이미지의 가로세로 크기 계산
    const int width = std::max(1, cvRound(std::max(Dist(box[0], box[1]), Dist(box[3], box[2]))));
    const int height = std::max(1, cvRound(std::max(Dist(box[0], box[3]), Dist(box[1], box[2]))));
    const cv::Point2f dst[4]{{0, 0},
                             {static_cast<float>(width - 1), 0},
                             {static_cast<float>(width - 1), static_cast<float>(height - 1)},
                             {0, static_cast<float>(height - 1)}};

    // 텍스트 박스 좌표에 기반해서 원근 보정을 적용하고 박스에 해당하는 이미지 영역 잘라내기
    // 원근 보정 시 경계 밖 샘플은 가장자리 픽셀 복제
    cv::Mat crop;
    cv::warpPerspective(image, crop, cv::getPerspectiveTransform(box.data(), dst), cv::Size(width, height),
                        cv::INTER_CUBIC, cv::BORDER_REPLICATE);
    
    // 세로 방향으로 긴 텍스트 영역은 recognizer가 가로 방향으로 인식할 수 있도록 90도 회전
    if (crop.rows >= crop.cols * 1.5)
    {
        cv::transpose(crop, crop);
        cv::flip(crop, crop, 0);
    }
    return crop;
}

void ImgOcrEngine::SortBoxes(std::vector<TextBox> &boxes)
{
    // 텍스트 박스의 y 좌표 차이가 글자 높이의 절반보다 크면 다른 줄 판단 - 위쪽을 전방으로 이동
    // 같은 줄로 판단되면 x 좌표가 작은 왼쪽 박스를 전방으로 이동
    std::sort(boxes.begin(), boxes.end(),
              [](const TextBox &a, const TextBox &b)
              {
                  const float tolerance = std::max(Dist(a[0], a[3]), Dist(b[0], b[3])) * 0.5f;
                  return std::abs(a[0].y - b[0].y) > tolerance ? a[0].y < b[0].y : a[0].x < b[0].x;
              });
}
