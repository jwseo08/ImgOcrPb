#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

// 텍스트 영역 1개의 ocr 결과
// 텍스트는 utf-8 문자열
struct TOcrTextLine
{
    std::array<cv::Point2f, 4> box{}; // 텍스트 박스 좌표 - 좌표 순서는 좌상단, 우상단, 우하단, 좌하단
    std::string text;                 // 텍스트 박스의 ocr 문자열
    float confidence = 0.0f;          // 문자을 구성하는 문자의 출력 점수 평균 - ctc 디코딩에서 선택된 문자들의 평균 출력 점수
};

// 이미지 전체의 ocr 결과
// success = false면 errorMessage에 실패 원인 기록
struct TOcrResult
{
    bool success = false;            // ocr 작업 성공 여부
    std::string errorMessage;        // 에러 메시지
    std::vector<TOcrTextLine> lines; // ocr 텍스트 박스 목록
    cv::Mat visualizedImage;         // 입력 이미지에 텍스트 영역을 표시한 이미지
};


// PaddleOCR의 검출기와 인식기 ONNX 모델을 직접 실행하는 OCR 엔진 클래스

// ocr 대상 이미지 로드
// 이미지 데이터를 검출기에 입력하기 위한 전처리
// 텍스트 검출을 위한 onnx 추론
// 텍스트 박스 검출을 위한 db 후처리
// 검출 결과를 인식기에 입력하기 위한 전처리
// 텍스트 인식을 위한 onnx 추론
// 인식 결과에 대한 ctc 디코딩
// 텍스트 결과 출력

class ImgOcrEngine
{
  public:
    ImgOcrEngine();
    ~ImgOcrEngine();

    // 기능 초기화
    // 검출기, 인식기 onnx 세션 준비, 문자 사전 준비
    // 검출기 - detector, 인식기 - recognizer, 문자 사전 - dictionary
    bool Initialize(const std::string &detectorModelPath, const std::string &recognizerModelPath,
                    const std::string &dictionaryPath, std::string &errorMessage);

    // 이미지 1개를 대상으로 텍스트 검출과 인식 수행
    bool Run(const cv::Mat &image, TOcrResult &result);

    // 초기화 여부 반환
    bool IsInitialized()
    {
        return m_initialized;
    }

  private:
    Ort::Env m_environment;               // onnx runtime 전역 실행 환경
    Ort::SessionOptions m_sessionOptions; // detector, recognizer 공유 옵션 - 최적화 설정

    std::unique_ptr<Ort::Session> m_detectorSession;   // 검출기 세션
    std::unique_ptr<Ort::Session> m_recognizerSession; // 인식기 세션

    std::string m_detectorInputName;     // detect 모델 입력 텐서 이름
    std::string m_detectorOutputName;    // detect 모델 출력 텐서 이름
    std::string m_recognizerInputName;   // recognizer 모델 입력 텐서 이름
    std::string m_recognizerOutputName;  // recognizer 모델 출력 텐서 이름

    // 문자 사전 - ctc blank를 제외한 utf-8 문자 목록
    // use_space_char = true 에 해당하는 공백은 마지막 항목으로 포함
    // ctc blank는 출력 인덱스 0으로 별도 처리
    std::vector<std::string> m_dictionary;

    // 초기화 여부 플래그
    bool m_initialized = false;

  private:
    // 텍스트 박스 별칭 사용 명시
    using TextBox = std::array<cv::Point2f, 4>;

    // 문자 사전 로드 - 확장자 기준으로 일반 텍스트 형식과 yaml 형식 구분
    bool LoadDictionary(const std::string &path, std::string &errorMessage);

    // yaml 형식 문자 사전 로드
    bool LoadDictionaryFromYaml(const std::string &path, std::string &errorMessage);

    // 입력 이미지에 대해서 텍스트 검출 작업, db 후처리로 텍스트 박스 목록 생성 - 박스는 원본 이미지 좌표 기준
    bool DetectText(const cv::Mat &image, std::vector<TextBox> &boxes, std::string &errorMessage);

    // 한 개의 텍스트 박스 이미지에 대해서 텍스트 인식 작업, ctc 디코딩으로 실제 문자열과 출력 점수 평균 반환 
    bool RecognizeText(const cv::Mat &crop, std::string &text, float &confidence, std::string &errorMessage);

    // 텍스트 박스 좌표 정렬 - 좌상단부터 시계 방향으로 정렬
    static TextBox OrderBox(const cv::Point2f points[4]);

    // 텍스트 박스 좌표를 기반으로 recognizer 입력용 crop 이미지 생성
    static cv::Mat CropTextImage(const cv::Mat &image, const TextBox &box);

    // 검출된 텍스트 박스들을 정렬 - 위에서 아래로, 같은 줄에서는 왼쪽에서 오른쪽 순서로 정렬
    static void SortBoxes(std::vector<TextBox> &boxes);
};
