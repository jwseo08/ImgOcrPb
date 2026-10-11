#include "pch.h"
#include "ImgProc.h"

#include <iostream>
#include <algorithm>
#include <cmath>
#include <limits>

#include "CommonUtil.h"

bool ImgProc::PrepareForOcr(const CString &csImageFileName, cv::Mat &matOcrInput, CString &csErrorMessage,
                             const TImgProcOption &tOption)
{
    matOcrInput.release();
    csErrorMessage.Empty();
    try
    {
        // 전처리 내용 별로 cv::Mat 유지 - 이전 처리 단계 내용 유지

        cv::Mat input;          // 입력된 원본 이미지 
        cv::Mat resized;        // 크기 조정 이미지
        cv::Mat geometryInput;  // geometry 보정 이미지
        cv::Mat perspective;    // 원근 보정 이미지
        cv::Mat canny;          // canny 엣지 검출 이미지
        std::vector<cv::Point2f> corners;   // 검출된 엣지 목록

        bool rt = false;

        // 입력 이미지 로드
        rt = LoadImageFile(csImageFileName, input);
        if (!rt)
        {
            csErrorMessage = _T("OCR 이미지를 불러올 수 없습니다.");
            return false;
        }

        // 크기 조정, 다른 형식인 경우 BGR 형식으로 통일
        rt = ResizeAndConvertColor(input, resized, tOption);
        if (!rt)
        {
            csErrorMessage = _T("OCR 이미지 전처리에 실패 - ResizeAndConvertColor");
            return false;
        }

        // 양방향 필터로 노이즈 감소시킴
        rt = RemoveNoise(resized, geometryInput, tOption);
        if (!rt)
        {
            csErrorMessage = _T("OCR 이미지 전처리에 실패 - RemoveNoise");
            return false;
        }
                        
        // 보정 옵션 설정
        TImgProcOption geometryOption = tOption;
        geometryOption.dMinDocumentAreaRatio = (std::max)(0.5, tOption.dMinDocumentAreaRatio);
        
        // 신분증 윤곽선이 검출되면 원근 보정
        rt = DetectDocumentContour(geometryInput, canny, corners, geometryOption);
        if (rt)
        {
            rt = CorrectPerspective(resized, corners, perspective);
        }
        
        // 신분증 윤곽선을 찾지 못하거나 원근 보정이 실패하면
        // 이전 처리 단계의 resized 이미지를 다음 단계로 넘김
        if (!rt) perspective = resized;
        
        // 경계값을 이용한 명암 조정으로 배경 무늬 감소 처리
        rt = SuppressOcrBackground(perspective, matOcrInput, tOption);
        if (!rt)
        {
            csErrorMessage = _T("OCR 배경 억제에 실패했습니다. 명암 기준값을 확인하세요.");
            return false;
        }
        return true;
    }
    catch (const cv::Exception &e)
    {
        csErrorMessage.Format(_T("OpenCV 오류: %S"), e.what());
    }
    catch (const std::exception &e)
    {
        csErrorMessage.Format(_T("OCR 전처리 오류: %S"), e.what());
    }
    return false;
}

bool ImgProc::ResizeAndConvertColor(const cv::Mat &matSrc, cv::Mat &matDst, const TImgProcOption &tOption)
{
    if (matSrc.empty() || tOption.nMaxWidth <= 0 || tOption.nMaxHeight <= 0)
        return false;

    // 채널 수가 다른 이미지를 BGR 3채널로 변환
    cv::Mat matBgr;
    switch (matSrc.channels())
    {
    case 1: // 1채널 grayscale 이미지
        cv::cvtColor(matSrc, matBgr, cv::COLOR_GRAY2BGR);
        break;
    case 3:
        matBgr = matSrc;
        break;
    case 4: // 4채널 알파채널 포함 이미지
        cv::cvtColor(matSrc, matBgr, cv::COLOR_BGRA2BGR);
        break;
    default:
        return false;
    }

    // 최대 크기보다 크면 크기 조정 - 가로세로 비율 유지
    const double dScaleX = static_cast<double>(tOption.nMaxWidth) / matBgr.cols;
    const double dScaleY = static_cast<double>(tOption.nMaxHeight) / matBgr.rows;
    const double dScale = std::min(1.0, std::min(dScaleX, dScaleY));

    if (dScale < 1.0)
    {
        // 최대 크기보다 큰 경우 리사이즈
        cv::resize(matBgr, matDst, cv::Size(), dScale, dScale, cv::INTER_AREA);
    }
    else
    {
        // 최대 크기보다 작은 경우
        matDst = matBgr.clone();
    }

    if (!matDst.empty())
        return true;
    else
        return false;
}

bool ImgProc::RemoveNoise(const cv::Mat &matSrc, cv::Mat &matDst, const TImgProcOption &tOption)
{
    if (matSrc.empty())
        return false;

    // 양방향 필터 적용 - 윤곽선을 최대한 보존하면서 노이즈 감소 시킴
    const int nDiameter = std::max(1, tOption.nBilateralDiameter);
    cv::bilateralFilter(matSrc, matDst, nDiameter, std::max(1.0, tOption.dBilateralSigmaColor),
                        std::max(1.0, tOption.dBilateralSigmaSpace));

    if (!matDst.empty())
        return true;
    else
        return false;
}

bool ImgProc::DetectDocumentContour(const cv::Mat &matSrc, cv::Mat &matCanny, std::vector<cv::Point2f> &vecCorners,
                                     const TImgProcOption &tOption)
{
    // 결과값 초기화
    vecCorners.clear();
    matCanny.release();

    if (matSrc.empty())
        return false;

    // grayscale 변환 - canny 경계선 검출을 위한 전처리
    cv::Mat matGray;
    if (matSrc.channels() == 3)
        cv::cvtColor(matSrc, matGray, cv::COLOR_BGR2GRAY);
    else
        matGray = matSrc;

    // blur 적용 - canny 경계선 검출을 위한 전처리, 미세 노이즈 제거
    cv::GaussianBlur(matGray, matGray, cv::Size(5, 5), 0.0);

    // canny 경계선 검출 - 밝기 변화가 큰 문서 외곽선, 글자 경계선 검출
    cv::Canny(matGray, matCanny, std::max(0.0, tOption.dCannyThreshold1),
              std::max(tOption.dCannyThreshold1 + 1.0, tOption.dCannyThreshold2));

    // 끊어진 경계선을 닫힘 연산으로 이어줌 - 문서 외곽선 검출을 위한 보정
    const cv::Mat matKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(matCanny, matCanny, cv::MORPH_CLOSE, matKernel, cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> vecContours;
    cv::findContours(matCanny.clone(), vecContours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    // 너무 작은 영역이 문서로 오검출되지 않도록 영역 크기 최소값 지정
    const double dImageArea = static_cast<double>(matSrc.cols) * matSrc.rows;
    const double dMinArea = dImageArea * std::clamp(tOption.dMinDocumentAreaRatio, 0.01, 0.95);

    // 조건을 만족하는 사각형 영역 중에서 가장 크기가 큰 것을 문서 경계선으로 지정
    double dBestArea = 0.0;
    std::vector<cv::Point> vecBestCorners;

    for (const auto &vecContour : vecContours)
    {
        const double dArea = std::fabs(cv::contourArea(vecContour));
        if (dArea < dMinArea || dArea <= dBestArea)
            continue;

        // 꼭지점이 4개인지 검사 - 정밀도는 윤곽선 둘레 2%
        const double dPerimeter = cv::arcLength(vecContour, true);
        std::vector<cv::Point> vecApprox;
        cv::approxPolyDP(vecContour, vecApprox, dPerimeter * 0.02, true);

        // 꼭지점이 4개이고 볼록 사각형인 경우 문서 후보로 지정
        if (vecApprox.size() == 4 && cv::isContourConvex(vecApprox))
        {
            dBestArea = dArea;
            vecBestCorners = std::move(vecApprox);
        }
    }

    if (vecBestCorners.size() != 4)
        return false;

    // 찾은 꼭지점을 순서대로 정렬 - 좌상, 우상, 우하, 좌하 순서
    vecCorners = OrderCorners(vecBestCorners);

    if (vecCorners.size() == 4) 
        return true;
    else
        return false;
}

bool ImgProc::CorrectPerspective(const cv::Mat &matSrc, const std::vector<cv::Point2f> &vecCorners, cv::Mat &matDst)
{
    if (matSrc.empty() || vecCorners.size() != 4)
        return false;

    // 서로 마주 보는 두 변 중 긴 길이를 보정 결과 사각형의 너비와 높이로 사용
    const double dTopWidth = cv::norm(vecCorners[1] - vecCorners[0]);
    const double dBottomWidth = cv::norm(vecCorners[2] - vecCorners[3]);
    const double dLeftHeight = cv::norm(vecCorners[3] - vecCorners[0]);
    const double dRightHeight = cv::norm(vecCorners[2] - vecCorners[1]);

    const int nWidth = static_cast<int>(std::round(std::max(dTopWidth, dBottomWidth)));
    const int nHeight = static_cast<int>(std::round(std::max(dLeftHeight, dRightHeight)));
    if (nWidth < 2 || nHeight < 2) return false;

    // 검출된 네 점에 대응하는 보정된 직사각형 좌표 설정 - 보정 목표 좌표
    const std::vector<cv::Point2f> vecDestination = {
        cv::Point2f(0.0f, 0.0f), cv::Point2f(static_cast<float>(nWidth - 1), 0.0f),
        cv::Point2f(static_cast<float>(nWidth - 1), static_cast<float>(nHeight - 1)),
        cv::Point2f(0.0f, static_cast<float>(nHeight - 1))};

    // 보정 목표 좌표로 변환하기 위한 변환 행렬 계산 - 원근 변환
    const cv::Mat matTransform = cv::getPerspectiveTransform(vecCorners, vecDestination);

    // 워핑 적용 - 기울어진 이미지 보정
    cv::warpPerspective(matSrc, matDst, matTransform, cv::Size(nWidth, nHeight), cv::INTER_CUBIC, cv::BORDER_REPLICATE);

    if (!matDst.empty()) return true;
    else return false;
}

bool ImgProc::LoadImageFile(const CString &csImageFileName, cv::Mat &matImage)
{
    matImage.release();
    
    if (csImageFileName.IsEmpty()) return false;

    std::vector<uchar> vecData;
    int rt = ReadFileToVecBuf(csImageFileName.GetString(), vecData);
    if (rt == 0)
    {
        matImage = cv::imdecode(vecData, cv::IMREAD_UNCHANGED);
        if (matImage.empty())
        {
            std::cerr << "image decode fail \n";
            return false;
        }
    }
    else
    {
        std::cerr << "image load fail=" << rt << "\n";
        return false;
    }

    return true;
}

std::vector<cv::Point2f> ImgProc::OrderCorners(const std::vector<cv::Point> &vecCorners)
{
    if (vecCorners.size() != 4)
        return {};

    // x + y 는 좌상 우하 구분에 사용
    // y - x 는 우상 좌하 구분에 사용
    std::vector<cv::Point2f> vecOrdered(4);
    double dMinSum = std::numeric_limits<double>::max();
    double dMaxSum = std::numeric_limits<double>::lowest();
    double dMinDiff = std::numeric_limits<double>::max();
    double dMaxDiff = std::numeric_limits<double>::lowest();

    for (const cv::Point &pt : vecCorners)
    {
        const double dSum = static_cast<double>(pt.x) + pt.y;
        const double dDiff = static_cast<double>(pt.y) - pt.x;

        if (dSum < dMinSum)
        {
            dMinSum = dSum;
            vecOrdered[0] = cv::Point2f(static_cast<float>(pt.x), static_cast<float>(pt.y));
        }
        if (dDiff < dMinDiff)
        {
            dMinDiff = dDiff;
            vecOrdered[1] = cv::Point2f(static_cast<float>(pt.x), static_cast<float>(pt.y));
        }
        if (dSum > dMaxSum)
        {
            dMaxSum = dSum;
            vecOrdered[2] = cv::Point2f(static_cast<float>(pt.x), static_cast<float>(pt.y));
        }
        if (dDiff > dMaxDiff)
        {
            dMaxDiff = dDiff;
            vecOrdered[3] = cv::Point2f(static_cast<float>(pt.x), static_cast<float>(pt.y));
        }
    }

    return vecOrdered;
}

bool ImgProc::SuppressOcrBackground(const cv::Mat &matSrc, cv::Mat &matDst, const TImgProcOption &tOption)
{
    matDst.release();
    
    // 이미지 데이터, 픽셀 depth 검사
    if (matSrc.empty() || matSrc.depth() != CV_8U) return false;

    // 채널 수 검사 - 1, 3, 4 채널만 처리
    if (matSrc.channels() != 1 && matSrc.channels() != 3 && matSrc.channels() != 4) return false;

    // 옵션에서 배경 처리 안함으로 설정되어 있으면 BGR 변환만 처리
    if (!tOption.bSuppressOcrBackground)
    {
        if (matSrc.channels() == 1) cv::cvtColor(matSrc, matDst, cv::COLOR_GRAY2BGR);
        else if (matSrc.channels() == 4) cv::cvtColor(matSrc, matDst, cv::COLOR_BGRA2BGR);
        else matDst = matSrc.clone();
        return true;
    }

    // gray 컬러를 이용해서 이진화 없이 문자 획의 경계 보존
    // 진한 글자는 유지하고 중간 밝기 값을 연속적으로 올림
    
    const int ink = tOption.nOcrInkPreserveLevel;        // 보존 경계값 - 보존 경계값 이하의 밝기는 보존 - 기본값 60
    const int white = tOption.nOcrBackgroundWhiteLevel;  // 제거 경계값 - 제거 경계값 이상의 밝기는 흰색으로 변경 - 기본값 180
    if (ink < 0 || white > 255 || ink >= white) return false;
    
    // 입력 이미지를 gray color로 변환 - 밝기 값 조정으로 배경 제거를 위해서
    cv::Mat gray;
    if (matSrc.channels() == 1) gray = matSrc;
    else if (matSrc.channels() == 4) cv::cvtColor(matSrc, gray, cv::COLOR_BGRA2GRAY);
    else cv::cvtColor(matSrc, gray, cv::COLOR_BGR2GRAY);

    // lookup 테이블 작성
    cv::Mat lut(1, 256, CV_8U);
    for (int i = 0; i < 256; ++i)
    {
        if (i <= ink)
        {
            // 보존 경계값 이하인 경우 - 보존
            lut.at<uchar>(i) = static_cast<uchar>(i);
        }
        else
        {
            // ink 와 white 범위 사이에 있는 중간값을 ink ~ 255 범위로 확대
            double value = ink + (i - ink) * (255.0 - ink) / (white - ink);

            // 확대된 값을 0 ~ 255 범위로 제한
            lut.at<uchar>(i) = cv::saturate_cast<uchar>(value);
        }
    }

    // lookup 테이블 적용
    cv::Mat suppressed;
    cv::LUT(gray, lut, suppressed);

    // 다음 단계 작업을 위해서 BGR로 변환
    cv::cvtColor(suppressed, matDst, cv::COLOR_GRAY2BGR);

    return true;
}