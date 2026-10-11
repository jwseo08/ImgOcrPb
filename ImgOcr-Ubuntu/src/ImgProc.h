#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <filesystem>
#include <string>

// 이미지 처리 옵션
struct TImgProcOption
{
    bool bSuppressOcrBackground = true; // OCR에서 밝은 배경무늬를 억제한다.
    int nOcrInkPreserveLevel = 60;      // 이 밝기 이하의 진한 획은 유지한다.
    int nOcrBackgroundWhiteLevel = 180; // 이 밝기 이상의 배경은 흰색으로 만든다.

    int nMaxWidth = 1600;  // 최대 가로 크기
    int nMaxHeight = 1600; // 최대 세로 크기

    int nBilateralDiameter = 7;         // 양방향 필터 이웃 지름
    double dBilateralSigmaColor = 50.0; // 양방향 필터 색 차이 범위
    double dBilateralSigmaSpace = 50.0; // 양방향 필터 공간 거리 범위

    double dCannyThreshold1 = 50.0;      // canny 하위 임계값
    double dCannyThreshold2 = 150.0;     // canny 상위 임계값
    double dMinDocumentAreaRatio = 0.15; // 문서 최소 면적 비율 - 전체 영상 기준

};

// 이미지 처리 클래스
// 원근 보정, 그림자 제거, 선명화, 이진화
class ImgProc
{
  public:
    ImgProc() = default;
    ~ImgProc() = default;

    // ocr 대상 이미지 준비
    // 이미지 파일 로드, BGR 3채널 형식으로 변환, 노이즈 감소, 원근 보정
    bool PrepareForOcr(const std::filesystem::path &imagePath, cv::Mat &matOcrInput, std::string &errorMessage,
                       const TImgProcOption &tOption = TImgProcOption());

  private:
    // 명암 보정으로 배경 처리 - 배경 무늬 감소
    bool SuppressOcrBackground(const cv::Mat &matSrc, cv::Mat &matDst,
                               const TImgProcOption &tOption = TImgProcOption());

    // 이미지 크기 조정, bgr 3채널 변환
    bool ResizeAndConvertColor(const cv::Mat &matSrc, cv::Mat &matDst, const TImgProcOption &tOption);

    // 양방향 필터 적용 - 경계선 유지, 노이즈 제거
    bool RemoveNoise(const cv::Mat &matSrc, cv::Mat &matDst, const TImgProcOption &tOption);

    // 문서 경계선 검출
    bool DetectDocumentContour(const cv::Mat &matSrc, cv::Mat &matCanny, std::vector<cv::Point2f> &vecCorners,
                               const TImgProcOption &tOption);

    // 원근 보정 - 문서 꼭지점을 직사각형으로 변환
    bool CorrectPerspective(const cv::Mat &matSrc, const std::vector<cv::Point2f> &vecCorners, cv::Mat &matDst);
      
    // 이미지 파일 로드
    bool LoadImageFile(const std::filesystem::path &imagePath, cv::Mat &matImage);

    // 꼭지점 정렬 - 좌상, 우상, 우하, 좌하 순서
    std::vector<cv::Point2f> OrderCorners(const std::vector<cv::Point> &vecCorners);
};
