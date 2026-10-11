#ifndef PARSE_DEF_H
#define PARSE_DEF_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <functional>
#include <stdexcept>

// 템플릿에서 필드가 위치하는 형태 - 필드 위치 기준과 형태
enum class EIdCardFieldPosition
{
    FromStart, // 문서 첫 줄 기준 고정 위치 - 제목 위치를 보정한 시작점 기준
    FromEnd,   // 문서 마지막 줄 기준 고정 위치
    Range,     // 시작 위치부터 문서 끝의 고정 줄들을 제외한 가변 범위 - 문서 끝의 지정한 행 수를 제외한 범위
    Candidates // 영역, 형식, 다른 필드와의 관계로 필드 후보를 찾고 필드 값 선택
};

// 텍스트에 대한 형식 검사 형태
enum class EIdCardFieldValidation
{
    None,                       // 형식 검사 없음
    ResidentRegistrationNumber, // 주민번호 형식으로 검사 - 000000-0000000
    DriverLicenseNumber,        // 운전면허 번호 형식으로 검사 - 00-00-000000-00
    Date,                       // 날짜 형식으로 검사
    KoreanName                  // 한국어 이름 형식으로 검사
};

// 이미지 크기에 대한 상대좌표 - 이미지 크기를 기준, 값은 0 ~ 1
struct TRelativeRect
{
    double left = 0, top = 0, width = 1, height = 1;
};

// 필드 후보로 찾은 문자열들을 필드 값으로 선택하는 방법
enum class EIdCardCandidateSelectMode
{
    Unique,     // 후보로 찾은 문자열이 1개일 때만 선택
    All,        // 모든 후보 문자열들을 이어 붙여서 선택
    Occurrence  // 후보 문자열들 중에서 index로 지정한 문자열 1개만 선택
};

// 텍스트에 포함된 공백 제거 여부 명시
enum class EIdCardTextNormalization
{
    None,             // 텍스트 앞뒤 공백 제거, 내부 공백 유지
    RemoveWhitespace  // 모든 ascii 공백 제거
};

// 필드 후보 문자열 검색 조건과 선택 방법 지정
// afterField, beforeField, searchRegion으로 검색할 박스 제한
// 텍스트 박스를 행으로 묶고 anchorTexts 검사
// 라벨을 제거
// textNormalization로 공백 제거 여부 적용
// textPattern 검사
// 값 추출
// 필드 형식에 맞는 후보 수집
// expectedMatchCount 검사
// mode, matchIndex로 후보 중에서 문자열 선택
// notBeforeField로 최종 날짜의 선후 관계 검사
struct TIdCardFieldSelection
{
    bool hasSearchRegion = false; // 검색 영역 제한 사용 여부
    TRelativeRect searchRegion;   // 이미지 기준 0~1 상대 좌표의 검색 영역
    double minOverlapRatio = 0.5; // ocr 박스 면적 중 검색 영역과 겹쳐야 하는 최소 비율

    std::string textPattern; // 라벨 제거와 정규화 후 행 전체에 적용할 정규식
    EIdCardTextNormalization textNormalization = EIdCardTextNormalization::None; // 공백 제거 여부

    std::vector<std::string> anchorTexts; // 행 원문에 하나 이상 포함되어야 할 문구, 공백 무시
    std::string afterField;               // 참조 필드의 마지막 행 아래에서 검색
    std::string beforeField;              // 참조 필드의 첫 행 위에서 검색
    std::string notBeforeField;           // 최종 날짜가 참조 필드의 날짜보다 이르면 오류

    EIdCardCandidateSelectMode mode = EIdCardCandidateSelectMode::Unique; // 후보 중에서 필드값 선택 방식
    size_t matchIndex = 0;         // Occurrence에서 선택할 후보 인덱스, 0부터 시작
    size_t expectedMatchCount = 0; // 예상 후보 수, 0은 미지정, Occurrence에서는 필수
};

// 신분증 템플릿에서 필드 1개의 추출과 검사 설정
struct TIdCardFieldTemplate
{
    std::string key;         // 필드 구분을 위한 키 - (ex) "resident_number"
    std::string displayName; // 화면에 표시할 필드명 - (ex) "주민등록번호"
   
    // 필드 추출 방식 - 설정 방식에 따라서 lineIndex에 따른 위치가 달라짐
    // FromStart - 제목 위치를 보정한 시작점부터 lineIndex번째 행 추출
    // FromEnd - 마지막 행부터 역순으로 lineIndex번째 행 추출
    // Range - 보정된 시작점의 lineIndex부터 끝의 trailingLineCount개 행을 제외한 범위 추출
    // Candidates - selection 조건을 통과한 후보에서 값 선택
    EIdCardFieldPosition position = EIdCardFieldPosition::FromStart;
    
    size_t lineIndex = 0;         // position 설정에 따라서 진행되는 행의 인덱스 - candidate에서는 사용하지 않음
    size_t trailingLineCount = 0; // position 형식이 range 일 때 range 범위에서 제외할 문서 끝의 줄 수
    
    bool required = true;         // 필수 필드 여부 - true일 때 값이 없으면 오류, false면 누락 허용
    std::string separator = "\n"; // 여러 줄 필드를 합칠 구분자

    // 필드의 텍스트 내용이 형식을 검사할 필요가 있는지 명시 - 주민번호, 날짜와 같은 항목은 형식 검사
    EIdCardFieldValidation validation = EIdCardFieldValidation::None;

    std::string fixedValue;                // ocr 텍스트를 대체하는 고정 값
    std::vector<std::string> labelAliases; // 텍스트 앞에 포함될 수 있는 라벨 후보
    std::string valuePattern;              // 텍스트만 추출할 정규식. 첫 번째 캡처 그룹을 사용
    TIdCardFieldSelection selection;       // position이 candidates로 설정된 경우 사용 - 실패 시 행 인덱스로 대체하지 않음
};

// ocr 텍스트 박스 하나의 문자열과 픽셀 좌표
// 박스 좌표는 같은 라인에 위치한 박스들을 한 줄로 묶는 데 사용
struct TIdCardOcrTextBox
{
    std::string text;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// 파싱 제외 영역 - (ex) 도장 영역
// 보정된 ocr 입력 이미지 기준의 0~1 비율 좌표.
struct TIdCardExcludeRegion
{
    std::string name;     // 영역 이름 - 구분을 위해서 추가
    double left = 0.0;    // 제외 영역 - 입력 이미지에 대한 비율로 표시
    double top = 0.0;
    double width = 0.0;
    double height = 0.0;
    double minOverlapRatio = 0.5; // 텍스트 박스와 제외 영역의 겹치는 면적 비율 경계값 - 제외할 텍스트 박스 검사에 사용
    std::string textPattern;      // 텍스트 패턴 검사 조건 - 제외할 텍스트 박스 검사에 사용
    EIdCardTextNormalization textNormz = EIdCardTextNormalization::None; // 공백 제거 방식
};

struct TIdCardTemplate
{
    std::string id;                         // 템플릿 구분 아이디
    std::string displayName;                // 화면에 표시되는 신분증 종류
    std::vector<std::string> titleKeywords; // 템플릿을 판별할 제목 문자열
    size_t expectedTitleLineIndex = 0;      // 레이아웃에서 제목이 위치하는 라인 인덱스 - (ex) "주민등록증", "자동차운전면허증" 텍스트의 위치
    size_t minimumLineCount = 0;            // 신분증 내용을 구성하는 최소 라인 수
    std::vector<TIdCardFieldTemplate> fields;         // 내용을 구성하는 필드 목록
    std::vector<TIdCardExcludeRegion> excludeRegions; // 파싱 제외 영역 목록
};

// 신분증 파싱 결과 - 신분증 전체의 파싱 결과
struct TParsedIdCard
{
    // 신분증의 각 항목 - 필드로 지칭
    struct TField
    {
        std::string key;         // 필드 구분자
        std::string displayName; // 화면 표시용 필드 구분자
        std::string value;       // 내용 - 파싱된 실제 텍스트
    };

    bool success = false;              // 작업 성공실패 여부
    std::string templateId;            // 신분증 템플릿 구분 아이디
    std::string documentType;          // 신분증 종류
    std::vector<TField> orderedFields; // 파싱된 필드 목록 - 템플릿에 정의된 표시 순서 보존
    std::vector<std::string> errors;   // 오류 메시지
    std::vector<std::string> warnings; // 경고 메시지
    size_t excludedTextBoxCount = 0;   // 제외 영역으로 제거된 유효 ocr 박스 수
   
    // 파싱 제외 영역에 해당하는 텍스트 박스
    struct TExcludedTextBox
    {
        std::string text;     // 제외된 박스의 텍스트
        std::string ruleName; // 식별자
    };
    
    // 제외 영역 텍스트 목록
    std::vector<TExcludedTextBox> excludedTextBoxes;
};

// 다수의 텍스트 박스로 구성된 논리 행
// 행의 y 좌표, 행을 구성하는 텍스트 박스 목록
struct TLogicalRow
{
    float top = 0.0f;
    float bottom = 0.0f;
    std::vector<TIdCardOcrTextBox> boxes;
};

// 신분증의 필드들을 추출하는 동안 필요한 입력 데이터와 처리 상태
// documentTemplate.fields[i] - i번째 필드의 설정
// values[i] - i번째 필드의 추출 결과
// resolved[i] - i번째 필드의 처리 여부
struct TResolveContext
{
    // 추출된 필드 값과 위치 정보 - 필드 하나의 추출 결과값
    struct TValue
    {
        std::string text;    // 결과 텍스트
        double firstY = 0;   // 선택된 첫번째 행 혹은 후보의 중심 y 좌표
        double lastY = 0;    // 선택된 마지막 행 혹은 후보의 중심 y 좌표
        bool found = false;  // 사용할 필드 값을 찾았는지 여부
    };

    const std::vector<TIdCardOcrTextBox> &boxes;  // 파싱에 사용할 ocr 텍스트 박스 목록
    const std::vector<TLogicalRow> &rows;         // 같은 라인에 있는 텍스트 박스를 묶은 논리 행 
    const TIdCardTemplate &documentTemplate;      // 신분증 템플릿 - 필드별 추출 위치, 검색 조건, 검사 방식

    float imageWidth;      // ocr 입력 이미지 width
    float imageHeight;     // ocr 입력 이미지 height
    size_t titleIndex;     // 실제로 발견한 신분증 제목의 논리 행 인덱스 - 신분증 제목은 "주민등록증", "자동차운전면허증"...

    TParsedIdCard &result; // 파싱 결과

    std::vector<TValue> values; // 각각의 필드 추출 결과 목록 - 필드별로 추출한 값과 위치를 저장하는 중간 결과
    std::vector<bool> resolved; // 해당 필드에 대한 추출 처리 시작 여부 - 중복 처리 방지
        
    // 템플릿, 결과 객체를 참조로 연결
    // 이미지 크기와 제목 인덱스 저장
    // 템플릿의 필드 수만큼 values, resolved 생성
    TResolveContext(const std::vector<TIdCardOcrTextBox> &boxes, 
                    const std::vector<TLogicalRow> &rows,
                    const TIdCardTemplate &documentTemplate, 
                    float imageWidth, 
                    float imageHeight, 
                    size_t titleIndex,
                    TParsedIdCard &result)
        : boxes(boxes), 
          rows(rows), 
          documentTemplate(documentTemplate), 
          imageWidth(imageWidth),
          imageHeight(imageHeight), 
          titleIndex(titleIndex), 
          result(result), 
          values(documentTemplate.fields.size()),
          resolved(documentTemplate.fields.size(), false)
    {
    }
};

#endif // parse_def_h
