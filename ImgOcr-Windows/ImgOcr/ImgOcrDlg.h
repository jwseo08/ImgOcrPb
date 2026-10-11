
// ImgOcrDlg.h: 헤더 파일
//

#pragma once

#include <iostream>
#include <string>
#include <memory>
#include "ImgOcrEngine.h"
#include "IdCardParser.h"

// CImgOcrDlg 대화 상자
class CImgOcrDlg : public CDialogEx
{
    // 생성입니다.
  public:
    CImgOcrDlg(CWnd *pParent = nullptr); // 표준 생성자입니다.

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
    enum
    {
        IDD = IDD_IMGOCR_DIALOG
    };
#endif

  protected:
    virtual void DoDataExchange(CDataExchange *pDX); // DDX/DDV 지원입니다.

  private:
    // detector 모델 파일 경로, recognizer 모델 경로, dictionary 파일 경로, 신분증 템플릿 json 파일 경로 가져옴
    // 경로는 config.ini 파일에 설정
    void GetModelPath(std::string &detModelPathFile, std::string &recModelPathFile,
                      std::string &dicPathFile, std::wstring &templatePathFile);
    
    // 선택한 ocr 대상 이미지 로드
    void LoadAndDisplayImage(const std::string &imgPathFile, UINT nPicControlID);
    
    // ui 화면에 이미지 표시
    void DisplayImage(const cv::Mat &image, UINT nPicControlID);
    
    // ocr 기능 초기화
    bool InitializeOcr(bool showError);
    
    // 신분증 템플릿이 정의되어 있는 json 파일 로드
    bool InitializeIdCardTemplates(bool showError);
    
    // ui에 출력하는 문자열을 utf-8로 변환해서 출력
    void SetOutputUtf8(const std::string &text);

    // ocr 처리 엔진 인스턴스
    std::unique_ptr<ImgOcrEngine> m_ocr;
    
    // 텍스트 파서 인스턴스
    IdCardParser m_idCardParser;
    
    std::string m_detModelPath;    // detector 모델 파일 경로
    std::string m_recModelPath;    // recognizer 모델 파일 경로
    std::string m_dictionaryPath;  // dictionary 파일 경로
    std::wstring m_templatePath;   // 신분증 템플릿 json 파일 경로

    // 구현입니다.
  protected:
    HICON m_hIcon;

    // 생성된 메시지 맵 함수
    virtual BOOL OnInitDialog();
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    DECLARE_MESSAGE_MAP()
  public:
    afx_msg void OnBnClickedBtnImgSearch();   // ocr 대상 이미지 선택과 로드
    afx_msg void OnBnClickedBtnRun();         // ocr 작업 수행
};
