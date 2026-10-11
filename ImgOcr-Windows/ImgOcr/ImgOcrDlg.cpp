
// ImgOcrDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "ImgOcr.h"
#include "ImgOcrDlg.h"
#include "afxdialogex.h"
#include <atlimage.h>
#include "CommonUtil.h"
#include "ImgProc.h"
#include "ImgOcrEngine.h"
#include "IdCardParser.h"
#include <filesystem>
#include "CommonUtil.h"

namespace fs = std::filesystem;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// 응용 프로그램 정보에 사용되는 CAboutDlg 대화 상자입니다.

class CAboutDlg : public CDialogEx
{
  public:
    CAboutDlg();

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
    enum
    {
        IDD = IDD_ABOUTBOX
    };
#endif

  protected:
    virtual void DoDataExchange(CDataExchange *pDX); // DDX/DDV 지원입니다.

    // 구현입니다.
  protected:
    DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange *pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

// CImgOcrDlg 대화 상자

CImgOcrDlg::CImgOcrDlg(CWnd *pParent /*=nullptr*/) : CDialogEx(IDD_IMGOCR_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CImgOcrDlg::DoDataExchange(CDataExchange *pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CImgOcrDlg, CDialogEx)
ON_WM_SYSCOMMAND()
ON_WM_PAINT()
ON_WM_QUERYDRAGICON()
ON_BN_CLICKED(IDC_BTN_IMG_SEARCH, &CImgOcrDlg::OnBnClickedBtnImgSearch)
ON_BN_CLICKED(IDC_BTN_RUN, &CImgOcrDlg::OnBnClickedBtnRun)
END_MESSAGE_MAP()

// CImgOcrDlg 메시지 처리기

BOOL CImgOcrDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // 시스템 메뉴에 "정보..." 메뉴 항목을 추가합니다.

    // IDM_ABOUTBOX는 시스템 명령 범위에 있어야 합니다.
    ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
    ASSERT(IDM_ABOUTBOX < 0xF000);

    CMenu *pSysMenu = GetSystemMenu(FALSE);
    if (pSysMenu != nullptr)
    {
        BOOL bNameValid;
        CString strAboutMenu;
        bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
        ASSERT(bNameValid);
        if (!strAboutMenu.IsEmpty())
        {
            pSysMenu->AppendMenu(MF_SEPARATOR);
            pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
        }
    }

    // 이 대화 상자의 아이콘을 설정합니다.  응용 프로그램의 주 창이 대화 상자가 아닐 경우에는
    //  프레임워크가 이 작업을 자동으로 수행합니다.
    SetIcon(m_hIcon, TRUE);  // 큰 아이콘을 설정합니다.
    SetIcon(m_hIcon, FALSE); // 작은 아이콘을 설정합니다.

    //-----------------------------------------------------------------

    GetModelPath(m_detModelPath, m_recModelPath, m_dictionaryPath, m_templatePath);
    InitializeIdCardTemplates(true);

    if (m_detModelPath.empty() || m_recModelPath.empty() || m_dictionaryPath.empty())
        ShowUtf8MessageBox(GetSafeHwnd(), "모델 파일이 지정되지 않았습니다.", "알림", MB_OK);

    else if (!fs::exists(m_detModelPath) || !fs::exists(m_recModelPath) || !fs::exists(m_dictionaryPath))
        ShowUtf8MessageBox(GetSafeHwnd(), "모델 파일을 찾을 수 없습니다.", "알림", MB_OK);
    else
        InitializeOcr(true);

    //-----------------------------------------------------------------

    return TRUE; // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CImgOcrDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
    if ((nID & 0xFFF0) == IDM_ABOUTBOX)
    {
        CAboutDlg dlgAbout;
        dlgAbout.DoModal();
    }
    else
    {
        CDialogEx::OnSysCommand(nID, lParam);
    }
}

// 대화 상자에 최소화 단추를 추가할 경우 아이콘을 그리려면
//  아래 코드가 필요합니다.  문서/뷰 모델을 사용하는 MFC 애플리케이션의 경우에는
//  프레임워크에서 이 작업을 자동으로 수행합니다.

void CImgOcrDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트입니다.

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // 아이콘을 그립니다.
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
    {
        CDialogEx::OnPaint();
    }
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
HCURSOR CImgOcrDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void CImgOcrDlg::OnBnClickedBtnImgSearch()
{
    std::vector<TFileFilter> vecFilter = {{_T("Image Files(*.jpg;*.jpeg;*.bmp;*.png)"), _T("*.jpg;*.jpeg;*.bmp;*.png")},
                                          {_T("JPEG Files(*.jpg;*.jpeg)"), _T("*.jpg;*.jpeg")},
                                          {_T("Bitmap Files(*.bmp)"), _T("*.bmp")},
                                          {_T("PNG Files(*.png)"), _T("*.png")},
                                          {_T("All Files(*.*)"), _T("*.*")}};

    std::string selectPathFile = OpenFileDlgSingleFile(vecFilter);
    if (!selectPathFile.empty())
    {
        GetDlgItem(IDC_EDIT_IMG_PATH)->SetWindowText(selectPathFile.c_str());
        LoadAndDisplayImage(selectPathFile, IDC_STC_IMG);
        // MessageBox(selectPathFile.c_str(), "알림", MB_OK);
    }
}

void CImgOcrDlg::OnBnClickedBtnRun()
{
    CString imagePath;
    GetDlgItem(IDC_EDIT_IMG_PATH)->GetWindowText(imagePath);

    if (imagePath.IsEmpty())
    {
        ShowUtf8MessageBox(GetSafeHwnd(), "먼저 판독할 이미지를 선택하세요.", "알림", MB_OK | MB_ICONINFORMATION);
        return;
    }
    
    if (!InitializeIdCardTemplates(true) || !InitializeOcr(true))
        return;

    CWaitCursor waitCursor;
    GetDlgItem(IDC_BTN_RUN)->EnableWindow(FALSE);
    GetDlgItem(IDC_EDIT_OUTPUT)->SetWindowText("OCR 처리 중...");

    ImgProc imageProcessor;
    cv::Mat ocrInput;
    CString preprocessingError;
    if (!imageProcessor.PrepareForOcr(imagePath, ocrInput, preprocessingError))
    {
        GetDlgItem(IDC_BTN_RUN)->EnableWindow(TRUE);
        ShowUtf8MessageBox(GetSafeHwnd(), std::string(preprocessingError.GetString()), "OCR 오류",
                           MB_OK | MB_ICONERROR);
        return;
    }

    TOcrResult result;
    if (!m_ocr->Run(ocrInput, result))
    {
        GetDlgItem(IDC_BTN_RUN)->EnableWindow(TRUE);
        SetOutputUtf8(result.errorMessage);
        ShowUtf8MessageBox(GetSafeHwnd(), "OCR 처리에 실패했습니다. 판독 결과의 오류 메시지를 확인하세요.", "OCR 오류",
                           MB_OK | MB_ICONERROR);
        return;
    }

    std::vector<TIdCardOcrTextBox> ocrTextBoxes;
    ocrTextBoxes.reserve(result.lines.size());
    for (const auto &line : result.lines)
    {
        float left = line.box[0].x;
        float top = line.box[0].y;
        float right = line.box[0].x;
        float bottom = line.box[0].y;
        for (const auto &point : line.box)
        {
            left = std::min(left, point.x);
            top = std::min(top, point.y);
            right = std::max(right, point.x);
            bottom = std::max(bottom, point.y);
        }
        ocrTextBoxes.push_back({line.text, left, top, right, bottom});
    }

    const TParsedIdCard parsed = m_idCardParser.ParseTextBoxes(ocrTextBoxes,
                                                               static_cast<float>(ocrInput.cols),
                                                               static_cast<float>(ocrInput.rows));

    std::ostringstream output;
    output << "[신분증 파싱 결과]\r\n";
    if (parsed.success)
    {
        for (const auto &field : parsed.orderedFields)
            output << field.displayName << ": " << field.value << "\r\n";
        for (const auto &warning : parsed.warnings)
            output << "주의: " << warning << "\r\n";
    }
    else
    {
        for (const auto &error : parsed.errors)
            output << "오류: " << error << "\r\n";
    }

    if (!parsed.excludedTextBoxes.empty())
    {
        output << "\r\n[제외된 OCR 텍스트]\r\n";
        for (const auto &excluded : parsed.excludedTextBoxes)
            output << excluded.text << "  [" << excluded.ruleName << "]\r\n";
    }

    output << "\r\n[OCR 원문]\r\n";
    for (const auto &line : result.lines)
        output << line.text << "  [" << static_cast<int>(line.confidence * 100.0f + 0.5f) << "%]\r\n";

    if (result.lines.empty())
        output << "인식된 텍스트가 없습니다.";

    SetOutputUtf8(output.str());
    DisplayImage(result.visualizedImage, IDC_STC_IMG);
    GetDlgItem(IDC_BTN_RUN)->EnableWindow(TRUE);
}

bool CImgOcrDlg::InitializeIdCardTemplates(bool showError)
{
    if (m_idCardParser.HasTemplates())
        return true;

    if (m_templatePath.empty())
        GetModelPath(m_detModelPath, m_recModelPath, m_dictionaryPath, m_templatePath);

    std::string error;
    if (m_templatePath.empty())
        error = "config.ini의 [MODEL_PATH] TEMPLATE에 신분증 템플릿 파일 경로를 지정하세요.";
    else if (m_idCardParser.LoadTemplatesFromJson(m_templatePath, error))
        return true;
    
    SetOutputUtf8(error);
    
    if (showError)
        ShowUtf8MessageBox(GetSafeHwnd(), error, "신분증 템플릿 오류", MB_OK | MB_ICONERROR);
    
    return false;
}

bool CImgOcrDlg::InitializeOcr(bool showError)
{
    if (m_ocr && m_ocr->IsInitialized()) return true;

    if (m_detModelPath.empty() || m_recModelPath.empty() || m_dictionaryPath.empty())
        GetModelPath(m_detModelPath, m_recModelPath, m_dictionaryPath, m_templatePath);
    
    m_ocr = std::make_unique<ImgOcrEngine>();
    
    std::string error;
    if (m_ocr->Initialize(m_detModelPath, m_recModelPath, m_dictionaryPath, error))
        return true;
    
    SetOutputUtf8(error);
    
    if (showError)
        ShowUtf8MessageBox(GetSafeHwnd(), "OCR 모델을 초기화할 수 없습니다. 판독 결과의 오류 메시지를 확인하세요.",
                           "OCR 오류", MB_OK | MB_ICONERROR);
    return false;
}

void CImgOcrDlg::SetOutputUtf8(const std::string &text)
{
    if (text.empty())
    {
        GetDlgItem(IDC_EDIT_OUTPUT)->SetWindowText("");
        return;
    }
    
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    
    if (count <= 0)
    {
        GetDlgItem(IDC_EDIT_OUTPUT)->SetWindowText(text.c_str());
        return;
    }
    
    std::wstring wide(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), count);
    ::SetWindowTextW(GetDlgItem(IDC_EDIT_OUTPUT)->GetSafeHwnd(), wide.c_str());
}

void CImgOcrDlg::DisplayImage(const cv::Mat &source, UINT controlId)
{
    if (source.empty())
        return;
    
    cv::Mat bgr;
    if (source.channels() == 1)
        cv::cvtColor(source, bgr, cv::COLOR_GRAY2BGR);
    else if (source.channels() == 4)
        cv::cvtColor(source, bgr, cv::COLOR_BGRA2BGR);
    else
        bgr = source;

    CImage image;
    if (!image.Create(bgr.cols, bgr.rows, 24)) return;
    
    const int pitch = image.GetPitch();
    BYTE *bits = static_cast<BYTE *>(image.GetBits());
    for (int y = 0; y < bgr.rows; ++y)
    {
        BYTE *destination = bits + static_cast<ptrdiff_t>(y) * pitch;
        memcpy(destination, bgr.ptr(y), static_cast<size_t>(bgr.cols) * 3);
    }

    CStatic *control = static_cast<CStatic *>(GetDlgItem(controlId));
    if (!control) return;

    CClientDC dc(control);
    CRect rect;
    control->GetClientRect(rect);
    dc.FillSolidRect(rect, RGB(0, 0, 0));
    
    const double scale =
        std::min(static_cast<double>(rect.Width()) / bgr.cols, static_cast<double>(rect.Height()) / bgr.rows);
    
    const int width = std::max(1, cvRound(bgr.cols * scale)), height = std::max(1, cvRound(bgr.rows * scale));
    SetStretchBltMode(dc.m_hDC, HALFTONE);
    image.Draw(dc.m_hDC, (rect.Width() - width) / 2, (rect.Height() - height) / 2, width, height);
}

void CImgOcrDlg::GetModelPath(std::string &detModelPathFile, std::string &recModelPathFile, std::string &dicPathFile, std::wstring &templatePathFile)
{
    std::wstring modulePath(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));

    if (length == 0 || length >= modulePath.size())
    {
        detModelPathFile.clear(); recModelPathFile.clear(); dicPathFile.clear(); templatePathFile.clear();
        return;
    }

    modulePath.resize(length);
    const fs::path iniPath = fs::path(modulePath).parent_path() / L"config.ini";
    detModelPathFile = ReadOcrResourcePath(iniPath, L"DET").string();
    recModelPathFile = ReadOcrResourcePath(iniPath, L"REC").string();
    dicPathFile = ReadOcrResourcePath(iniPath, L"DIC").string();
    templatePathFile = ReadOcrResourcePath(iniPath, L"TEMPLATE").wstring();
}

void CImgOcrDlg::LoadAndDisplayImage(const std::string &imgPathFile, UINT nPicControlID)
{
    CImage image;
    HRESULT hResult = image.Load(imgPathFile.c_str());
    if (FAILED(hResult))
    {
        ShowUtf8MessageBox(GetSafeHwnd(), "이미지 파일을 불러오지 못했습니다. 경로를 확인하세요.", "오류", MB_OK | MB_ICONERROR);
        return;
    }

    CStatic *pPicCtrl = (CStatic *)GetDlgItem(nPicControlID);
    if (pPicCtrl == nullptr) return;

    CClientDC dc(pPicCtrl);
    CRect rectControl;
    pPicCtrl->GetClientRect(&rectControl);

    dc.FillSolidRect(&rectControl, RGB(0, 0, 0));

    int imgWidth = image.GetWidth();
    int imgHeight = image.GetHeight();
    int ctrlWidth = rectControl.Width();
    int ctrlHeight = rectControl.Height();

    double imgRatio = (double)imgWidth / imgHeight;
    double ctrlRatio = (double)ctrlWidth / ctrlHeight;

    int drawWidth = 0;
    int drawHeight = 0;
    int drawX = 0;
    int drawY = 0;

    if (imgRatio > ctrlRatio)
    {
        drawWidth = ctrlWidth;
        drawHeight = (int)(ctrlWidth / imgRatio);
        drawX = 0;
        drawY = (ctrlHeight - drawHeight) / 2;
    }
    else
    {
        drawHeight = ctrlHeight;
        drawWidth = (int)(ctrlHeight * imgRatio);
        drawX = (ctrlWidth - drawWidth) / 2;
        drawY = 0;
    }

    SetStretchBltMode(dc.m_hDC, HALFTONE);
    SetBrushOrgEx(dc.m_hDC, 0, 0, NULL); 

    image.Draw(dc.m_hDC, drawX, drawY, drawWidth, drawHeight);
}
