#include "pch.h"
#include "IdCardParser.h"

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
#include "third_party/nlohmann/json.hpp"

#include "IdCardTemplateJson.h"
#include "CommonUtil.h"

// 파싱 작업을 위한 helper, sub 함수 네임스페이스
namespace
{

// ocr 텍스트에서 종류 표시 타이틀 검사  - '주민등록증', '자동차운전면허증', '복지카드' ...
// 신분증 별 템플릿의 타이틀과 비교해서 신분증 종류 찾음
bool MatchesTitle(const std::string &line, const TIdCardTemplate &documentTemplate)
{
    // 타이틀 문자열에서 공백 제거 - 오류방지
    // 제거하는 공백 문자는 일반 공백, 수평 탭, 줄바꿈 LF, 수직 탭, 폼 피드, 캐리지 리턴 CR 
    const std::string normalizedLine = RemoveAsciiWhiteSpace(line);

    // 신분증 템플릿의 타이틀 이름과 비교
    for (const auto &keyword : documentTemplate.titleKeywords)
    {
        if (normalizedLine.find(RemoveAsciiWhiteSpace(keyword)) != std::string::npos)
            return true;
    }

    return false;
}

// 여러 개의 문자열을 하나로 합침
std::string JoinLines(const std::vector<std::string> &lines, size_t begin, size_t end, const std::string &separator)
{
    std::ostringstream joined;
    for (size_t index = begin; index < end; ++index)
    {
        if (index > begin)
            joined << separator;
        joined << lines[index];
    }
    return joined.str();
}

// 위치로 선택한 필드 값의 앞부분에 알려진 라벨이 있으면 라벨과 구분 문자 제거해서 실제 값 반환
// 라벨은 "성명", "기간", "갱신기간" 처럼 신분증에 인쇄되어 있는 항목 텍스트
// 라벨을 비교할 때 공백 무시 - "기간", "기 간", "기   간"을 같은 라벨로 처리
std::string RemoveLeadingLabel(const std::string &value, const std::vector<std::string> &labelAliases)
{
    // 비교할 문자열의 앞뒤 공백 제거
    const std::string trimValue = Trim(value);

    for (const std::string &label : labelAliases)
    {
        // 비교할 라벨의 공백 제거
        const std::string normalizedLabel = RemoveAsciiWhiteSpace(label);

        size_t valueIndex = 0;
        size_t labelIndex = 0;

        // 입력 문자열에서 공백은 건너뛰고 문자를 순회하면서 라벨과 비교
        while (valueIndex < trimValue.size() && labelIndex < normalizedLabel.size())
        {
            unsigned char ch = static_cast<unsigned char>(trimValue[valueIndex]);

            // 입력 문자열 내부의 공백은 무시
            if (std::isspace(ch))
            {
                ++valueIndex;
                continue;
            }

            // 라벨과 다른 문자가 나오면 현재 라벨은 불일치로 판단
            if (trimValue[valueIndex] != normalizedLabel[labelIndex])
            {
                // 라벨 목록의 다른 라벨과 비교하기 위해서 빠져나감
                break;
            }

            ++valueIndex;
            ++labelIndex;
        }

        // 입력 문자열에서 라벨 부분이 일치하면 뒤에 이어지는 구분 문자를 제거하고 실제 값 반환
        // 입력 문자열과 라벨의 일치 여부는 라벨 문자열을 기준으로 판단
        // 입력 문자열에 ":", "~", '：' 같은 구분 문자가 붙어있어도 라벨 문자열과 일치하면 일치로 판단
        if (labelIndex == normalizedLabel.size())
        {
            // 입력 문자열에서 공백, ":", "~", '：' 제거 - 구분자 제거
            while (valueIndex < trimValue.size())
            {
                unsigned char ch = static_cast<unsigned char>(trimValue[valueIndex]);

                // 공백 제거
                if (std::isspace(ch))
                {
                    ++valueIndex;
                    continue;
                }

                // ":", "~" 제거
                if (trimValue[valueIndex] == ':' || trimValue[valueIndex] == '~')
                {
                    ++valueIndex;
                    continue;
                }

                // utf-8 전각 콜론 '：' 제거 
                if (trimValue.compare(valueIndex, 3, "\xEF\xBC\x9A") == 0)
                {
                    valueIndex += 3;
                    continue;
                }

                // 제거할 문자가 아니면 실제 값의 시작
                break;
            }

            // 정리가 끝나면 정리된 문자열 반환
            return Trim(trimValue.substr(valueIndex));
        }
    }

    // 라벨 목록의 모든 라벨과 일치하지 않으면 앞뒤 공백을 제거한 입력 문자열 반환
    return trimValue;
}

// 템플릿에 정의된 필드에 해당하는 텍스트에서 실제 문자열을 가져옴 - 라벨 제거한 문자열 반환
// 날짜나 등록번호처럼 정규식 패턴이 지정되어 있으면 패턴에 맞는 값 추출
std::string ExtractFieldValue(const std::string &rawValue, const TIdCardFieldTemplate &field)
{
    // ocr 문자열이 있을 때만 고정 값을 적용하므로, 누락된 필드를 고정 값으로 채우지는 않음
    if (!rawValue.empty() && !field.fixedValue.empty())
        return field.fixedValue;

    // 라벨 제거
    const std::string withoutLabel = RemoveLeadingLabel(rawValue, field.labelAliases);

    // 정규식 검사 패턴이 없으면 라벨 제거한 문자열 반환
    if (field.valuePattern.empty()) return withoutLabel;

    // 정규식으로 필드 값을 검사하고 추출
    std::smatch match;
    if (std::regex_search(withoutLabel, match, std::regex(field.valuePattern)) && match.size() > 1)
    {
        // 패턴이 일치하면 첫번째 캡처 그룹의 앞뒤 공백을 제거하고 반환
        // 불필요한 ocr 문자열을 제외하기 위해서 첫번째 캡처 그룹 사용 
        return Trim(match[1].str());
    }
    else
    {
        // 패턴이 맞지 않으면 라벨 제거한 문자열 반환
        return withoutLabel;
    }
}

// 등록번호, 날짜, 한글 이름 필드값에 대해서 형식 검사 - 추출된 필드값을 대상으로 형식 검사
bool IsValidField(const std::string &value, EIdCardFieldValidation validation)
{
    // 공백 제거 - 오류 방지
    std::string textVal = RemoveAsciiWhiteSpace(value);

    switch (validation)
    {
    // 주민등록번호 형식 검사 - 숫자 6자리 '-' 숫자 7자리
    case EIdCardFieldValidation::ResidentRegistrationNumber:
        return std::regex_match(textVal, std::regex(R"(\d{6}-?\d{7})"));

    // 운전면허등록번호 형식 검사 - 숫자 2자리 '-' 숫자 2자리 '-' 숫자 6자리 '-' 숫자 2자리
    case EIdCardFieldValidation::DriverLicenseNumber:
        return std::regex_match(textVal, std::regex(R"(\d{2}-\d{2}-\d{6}-\d{2})"));
    
    // 날짜 형식 검사
    case EIdCardFieldValidation::Date:
    {
        std::smatch match;
        if (!std::regex_match(textVal, match, std::regex(R"((\d{4})[.\-/](\d{1,2})[.\-/](\d{1,2})\.?)"))) 
        {
            return false;
        }
        
        int year=std::stoi(match[1]); 
        int month=std::stoi(match[2]); 
        int day=std::stoi(match[3]);
        
        if (year < 1 || 
            month < 1 || 
            month > 12 || 
            day < 1) 
        {
            return false;
        }
        
        int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
        
        // 윤년 규칙 - 4년마다 윤년이지만 100의 배수는 제외하고 400의 배수는 다시 포함
        bool leap = false;
        if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) leap = true;
        
        int extraDay = 0;
        if (month == 2 && leap) extraDay = 1;

        if (day <= days[month - 1] + extraDay) return true;
        else return false;
    }

    // 한국 이름 형식 검사
    case EIdCardFieldValidation::KoreanName:
    {
        // std::regex의 바이트 기반 한글 범위 대신 utf-8 음절 검사.
        size_t count=0;
        for (size_t i=0; i<textVal.size(); i+=3)
        {
            if (i+2>=textVal.size()) return false;
            const auto a=static_cast<unsigned char>(textVal[i]);
            const auto b=static_cast<unsigned char>(textVal[i+1]);
            const auto c=static_cast<unsigned char>(textVal[i+2]);

            if ((a&0xF0)!=0xE0 || (b&0xC0)!=0x80 || (c&0xC0)!=0x80) return false;

            // utf-8 3바이트를 유니코드 코드 포인트로 복원해서 완성형 한글 음절 범위 확인
            const unsigned code=((a&15)<<12)|((b&63)<<6)|(c&63);
            if (code<0xAC00 || code>0xD7A3) return false;
            ++count;
        }
        return count>=2 && count<=10;
    }

    default:
        return true;
    }
}

// 기존 논리 행과 검사 대상 텍스트 박스의 세로 위치가 겹치는지 검사하고 겹치면 비율 반환
float VerticalOverlapRatio(const TLogicalRow &row, const TIdCardOcrTextBox &box)
{
    // 겹치는 길이 계산
    const float overlap = (std::max)(0.0f, (std::min)(row.bottom, box.bottom) - (std::max)(row.top, box.top));
    
    // 겹침을 판단하기 위한 기준으로 논리 행의 세로 길이와 텍스트 박스의 세로 길이 중에서 작은 쪽을 선택
    // 세로 길이가 작은 요소를 기준으로 비율을 구해서 겹침 여부를 판단 - 작은 쪽이 큰 쪽에 포함되는 비율 구함
    const float smallerHeight = (std::min)(row.bottom - row.top, box.bottom - box.top);

    // 겹치는 부분이 있으면 겹치는 비율 계산해서 반환 - 세로 길이가 작은 쪽 기준
    if (smallerHeight > 0.0f)
        return (overlap / smallerHeight);
    else
        return 0.0f;
}

// ocr detector 텍스트 박스가 유효한지 검사
bool IsUsableBox(const TIdCardOcrTextBox &box)
{
    if (!Trim(box.text).empty() &&     // ocr 텍스트 박스에 있는 문자 수량 검사 - 공백을 제외한 문자 수
        std::isfinite(box.left) &&     // ocr 텍스트 박스 좌표 검사 - 유한수
        std::isfinite(box.top) &&      
        std::isfinite(box.right) &&
        std::isfinite(box.bottom) &&
        box.right > box.left &&        // ocr 텍스트 박스 좌표 정렬 검사 - 상하좌우 순서
        box.bottom > box.top)
    {
        return true;
    }
    else
    {
        return false;
    }
}

// 템플릿 0~1 행의 비율 영역과 ocr 박스가 겹치는 면적을 ocr 박스 전체 면적으로 나눈 비율 계산
double BoxOverlap(const TIdCardOcrTextBox &box, const TRelativeRect &rect, double imageWidth, double imageHeight)
{
    // 템플릿의 상대 좌표를 OCR 입력 이미지 기준 픽셀 좌표로 변환
    const double l = rect.left * imageWidth, t = rect.top * imageHeight;
    const double r = (rect.left + rect.width) * imageWidth, b = (rect.top + rect.height) * imageHeight;
    const double w = (std::max)(0.0, (std::min)(double(box.right), r) - (std::max)(double(box.left), l));
    const double h = (std::max)(0.0, (std::min)(double(box.bottom), b) - (std::max)(double(box.top), t));
 
    double rt = w * h / ((double(box.right) - box.left) * (double(box.bottom) - box.top)); 
    
    return rt;
}

// 패턴 비교용 문자열 준비 - 설정에 따라서 공백 제거
std::string NormalizeMatchText(const std::string &text, EIdCardTextNormalization mode)
{
    // 모든 공백 제거
    if (mode == EIdCardTextNormalization::RemoveWhitespace)
        return RemoveAsciiWhiteSpace(text);

    // 앞뒤 공백은 제거하고 내부 공백은 유지
    else
        return Trim(text);
}

// ocr 텍스트 박스를 필드값 추출에서 제외할지 검사
// 겹침 비율 조건과 텍스트 조건을 만족하면 필드값 추출 제외 - 신분증의 도장이나 2차원 바코드 영역 제외에 사용
bool IsExcludedBox(const TIdCardOcrTextBox &box, const TIdCardTemplate &documentTemplate,
                   double imageWidth, double imageHeight, std::string &ruleName)
{
    // 템플릿에 정의된 제외 영역 목록에 대해서 검사
    for (size_t i = 0; i < documentTemplate.excludeRegions.size(); ++i)
    {
        // 제외 영역 좌표는 전체 이미지에 대한 비율로 정의되어 있음 - TRelativeRect로 변환
        const auto &region = documentTemplate.excludeRegions[i];
        const TRelativeRect rect{region.left, region.top, region.width, region.height};

        // 텍스트 박스와 제외 영역 겹침 비율 검사
        // 검사 대상 텍스트 박스가 제외 영역과 겹치는 비율을 계산하고 경계값을 사용해서 제외 여부 판단
        // 경계값 비교에 오차 여유 적용 - 부동소수점 계산 오차 보완
        if (BoxOverlap(box, rect, imageWidth, imageHeight) + 1e-9 < region.minOverlapRatio)
        {
            // 겹침 비율이 임계값 보다 작으면 목록의 다른 제외 영역으로 검사
            continue;
        }

        // 텍스트 조건 검사 - 제외 영역에 텍스트 조건이 지정되어 있으면 검사
        // 텍스트 박스의 전체 문자열이 템플릿 json 파일에 정의된 제외 텍스트 정규식과 일치하는지 검사
        if (!region.textPattern.empty())
        {
            std::string resultText = NormalizeMatchText(box.text, region.textNormz);

            if (!std::regex_match(resultText, std::regex(region.textPattern)))
            {
                // 정규식과 일치하지 않으면 목록의 다른 제외 영역으로 검사
                continue;
            }
        }

        // 겹침 비율 검사와 텍스트 조건 검사에서 해당 텍스트 박스가 파싱 제외로 판단되면 제외 영역 index 기록
        if (region.name.empty())
            ruleName = "excludeRegions[" + std::to_string(i) + "]";
        else
            ruleName = region.name;

        // 검사 결과 반환 - 검사한 텍스트 박스는 파싱 제외로 판정
        return true;
    }

    // 검사 결과 반환 - 검사한 텍스트 박스는 파싱에서 제외하지 않음으로 판정
    return false;
}

// ocr detector 텍스트 박스를 기반으로 파싱에 사용하는 논리 행 목록 생성
// 유효한 ocr detector 텍스트 박스를 세로 방향 겹침 비율 판단으로 논리 행으로 묶고 
// 논리 행은 위에서 아래, 행 내부 박스는 왼쪽에서 오른쪽 순서로 정렬
std::vector<TLogicalRow> BuildLogicalRows(const std::vector<TIdCardOcrTextBox> &ocrTextBoxes)
{
    // 유효한 텍스트 박스 목록
    // ParseTextBoxes에서 텍스트와 좌표를 검증한 박스만 전달되므로 내부에서는 재검사하지 않음
    // 원본 박스 순서를 유지하기 위해 정렬할 복사본을 만드는 것임
    std::vector<TIdCardOcrTextBox> boxes = ocrTextBoxes;

    // 유효한 텍스트 박스 목록을 박스 위치에 따라서 정렬
    std::sort(boxes.begin(), boxes.end(),
              [](const TIdCardOcrTextBox &left, const TIdCardOcrTextBox &right)
              {
                  // 상단에 있는 박스 우선으로 정렬
                  if (left.top != right.top) return left.top < right.top;
                  
                  // top 좌표가 같으면 왼쪽에 있는 박스 우선으로 정렬
                  return left.left < right.left;
              });

    // 세로 겹침 기준으로 ocr 텍스트 박스들을 묶은 논리 행 목록 - 판단 범위에 있는 여러 개의 논리 텍스트 박스로 구성
    std::vector<TLogicalRow> rows;

    // 논리 행 y 좌표와 텍스트 박스 y 좌표를 검사해서 기준 범위 안에 있는 박스들을 논리 행으로 묶음
    for (const auto &box : boxes)
    {
        auto bestRow = rows.end();
        float bestOverlap = 0.0f;
        for (auto row = rows.begin(); row != rows.end(); ++row)
        {
            // 논리 행과 텍스트 박스의 세로 위치가 겹치는지 검사
            const float overlap = VerticalOverlapRatio(*row, box);
            
            // 논리 행 중에서 겹치는 비율이 45% 이상이고 겹침 비율이 더 큰 행을 찾음
            // 최종 선정된 논리 행을 해당 텍스트 박스가 속한 논리 행으로 판단
            if (overlap >= 0.45f && overlap > bestOverlap)
            {
                bestOverlap = overlap;
                bestRow = row;
            }
        }

        if (bestRow == rows.end())
        {
            // 겹침 기준을 만족하는 기존 행이 없으면 현재의 텍스트 박스 하나로 새 논리 행 생성
            // 논리 행에 처음 속하는 텍스트 박스로 논리 행의 top과 bottom 좌표 설정
            rows.push_back({box.top, box.bottom, {box}});
        }
        else
        {
            // 겹침 기준을 만족하는 행이 있으면 논리 행에 텍스트 박스 삽입
            // 후속 텍스트 박스가 삽입되면 논리 행의 top과 bottom은 삽입된 박스의 상하좌표와 비교해서
            // 기존 행과 새 박스를 모두 포함하도록 세로 범위 갱신
            bestRow->top = (std::min)(bestRow->top, box.top);
            bestRow->bottom = (std::max)(bestRow->bottom, box.bottom);
            bestRow->boxes.push_back(box);
        }
    }

    // 논리 행을 수직 위치 기준으로 이미지 상단에서 이미지 하단 순서로 정렬 
    std::sort(rows.begin(), rows.end(),
              [](const TLogicalRow &left, const TLogicalRow &right) { return left.top < right.top; });

    // 논리 행에 속해있는 텍스트 박스를 수평 위치 기준으로 이미지 좌측에서 우측 순서로 정렬 
    for (auto &row : rows)
        std::sort(row.boxes.begin(),row.boxes.end(),
            [](const TIdCardOcrTextBox &a,const TIdCardOcrTextBox &b){return a.left<b.left;});
    
    // 정렬된 논리 행 반환
    return rows;
}

// 논리 행에 속한 텍스트 박스의 문자열을 모두 모아서 반환
std::string RowText(const TLogicalRow &row)
{
    std::string text;
    for (const auto &box : row.boxes)
    {
        if (!text.empty()) text += ' ';
        text += Trim(box.text);
    }
    return text;
}

// ocr 텍스트 박스로 구성된 논리 행 생성, 각각의 논리 행에 속해있는 텍스트를 라인 별로 모아서 반환 
std::vector<std::string> BuildLogicalLines(const std::vector<TIdCardOcrTextBox> &boxes)
{
    std::vector<std::string> lines;
    for (const auto &row : BuildLogicalRows(boxes)) lines.push_back(RowText(row));
    return lines;
}

} // namespace


//-------------------------------------------------------------------------

IdCardParser::IdCardParser()
{
}

IdCardParser::~IdCardParser()
{
}

// 신분증 필드 추출에 사용할 json 템플릿 로드
// 전체 템플릿 검사 후 기존 목록 교체 - 실패 원인은 error로 반환
bool IdCardParser::LoadTemplatesFromJson(const std::wstring &path, std::string &error)
{
    // 이전 호출에서 남은 오류 메시지 초기화
    error.clear();

    try
    {
        // 한글이 포함된 경로를 처리하기 위해서 filesystem::path로 파일 열기
        std::ifstream input(std::filesystem::path(path), std::ios::binary);
        
        if (!input)
            TemplateJson::SetErrorMsg("file", "템플릿 파일을 열 수 없습니다.");
        
        // utf-8 json 읽기 - 구문 오류나 SetErrorMsg에서 발생한 예외는 아래 catch에서 처리
        TemplateJson::Json root = TemplateJson::Json::parse(input);
        if (input.bad())
            TemplateJson::SetErrorMsg("file", "템플릿 파일을 읽을 수 없습니다.");
        
        // json 최상위 객체 검사 - 잘못된 구조 사용 방지
        if (!root.is_object())
            TemplateJson::SetErrorMsg("root", "객체가 필요합니다.");

        // 공통 필드 설정과 기본값을 펼친 후 모든 템플릿을 같은 방식으로 검사
        TemplateJson::Required(root, "templates", "root");
        root = TemplateJson::ExpandCompactTemplates(std::move(root));

        // 공통 필드 설정을 펼친 최상위 객체의 속성 이름 검사
        TemplateJson::CheckObject(root, "root", {"templates"});

        const auto &templates = TemplateJson::Required(root, "templates", "root");
        if (!templates.is_array() || templates.empty())
            TemplateJson::SetErrorMsg("templates", "템플릿이 하나 이상 있는 배열이 필요합니다.");
        
        // 로딩 중 오류가 발생해도 기존 설정을 유지하기 위해서 임시 목록에 저장
        std::vector<TIdCardTemplate> loaded;
        std::set<std::string> ids;
        for (size_t i = 0; i < templates.size(); ++i)
        {
            // 오류 위치를 표시할 배열 경로 전달 - 필드 설정과 필드 간 참조 관계 검사
            const std::string context = "templates[" + std::to_string(i) + "]";
            auto documentTemplate = TemplateJson::CreateTemplate(templates[i], context);
        
            // 템플릿 id 중복 검사 - 파싱 결과에서 신분증 종류 구분에 사용
            if (!ids.insert(documentTemplate.id).second)
                TemplateJson::SetErrorMsg(context + ".id", "중복 템플릿 ID: " + documentTemplate.id);
            
            loaded.push_back(std::move(documentTemplate));
        }
        
        // 모든 템플릿의 변환과 검사가 성공하면 사용 중인 템플릿 목록 교체
        m_templates.swap(loaded);
        return true;
    }
    catch (const std::exception &exception)
    {
        // 실패 원인을 확인할 수 있도록 파일 경로와 오류 내용 반환
        error = "신분증 템플릿 로딩 실패 [" + std::filesystem::path(path).u8string() + "]: " + exception.what();
        return false;
    }
}

// index번째 필드 추출 - context.values[index]에 값과 위치 저장
// 참조하는 기준 필드 먼저 처리 - 오류와 경고는 context.result에 기록
void IdCardParser::ResolveField(size_t index, TResolveContext &context)
{
    auto &ctx = context;

    // 이미 처리를 시작한 필드는 다시 처리하지 않음
    if (ctx.resolved[index])
        return;

    // 같은 필드를 다시 추출하지 않도록 처리 시작 상태 기록 - 중복 처리 방지
    // resolved는 처리 시작 여부 - 실제 추출 성공 여부는 value.found로 구분
    ctx.resolved[index] = true;

    // 같은 인덱스의 필드 설정과 추출 결과를 참조로 연결
    const auto &field = ctx.documentTemplate.fields[index];
    auto &value = ctx.values[index];

    // 값을 찾지 못하면 필수 필드만 오류 기록 - 선택 필드는 누락 허용
    const auto missing = [&]()
    {
        if (field.required)
            ctx.result.errors.push_back(field.displayName + " 필드의 후보를 찾지 못했습니다.");
    };

    // 참조 키 field.key에 대응하는 필드 인덱스를 찾음
    // 참조 키 존재 여부와 순환 참조는 템플릿 로딩 때 검사 완료
    const auto fieldIndex = [&](const std::string &key)
    {
        return static_cast<size_t>(std::find_if(ctx.documentTemplate.fields.begin(), ctx.documentTemplate.fields.end(),
                                                [&](const TIdCardFieldTemplate &item) { return item.key == key; }) -
                                   ctx.documentTemplate.fields.begin());
    };

    // 날짜의 선후 비교를 위해 검증된 날짜를 YYYYMMDD 정수로 변환
    // 2026.10.09 -> 20261009처럼 변환 - 호출 전에 날짜 유효성 검사 필요
    const auto dateNumber = [](const std::string &text)
    {
        std::smatch m;
        const auto normalized = RemoveAsciiWhiteSpace(text);

        std::regex_match(normalized, m, std::regex(R"((\d{4})[.\-/](\d{1,2})[.\-/](\d{1,2})\.?)"));

        return std::stoi(m[1]) * 10000 + std::stoi(m[2]) * 100 + std::stoi(m[3]);
    };

    // 위치 지정 필드는 제목 위치를 보정한 논리 행 인덱스로 추출
    if (field.position != EIdCardFieldPosition::Candidates)
    {
        // 실제 제목이 예상 위치보다 앞이면 음수 보정이 필요하므로 이 경로에서는 추출하지 않음
        if (ctx.titleIndex < ctx.documentTemplate.expectedTitleLineIndex)
        {
            missing();
            return;
        }

        // 정상 제목 위치와 실제 위치의 차이만큼 앞쪽 잡음 행을 건너뜀
        const size_t offset = ctx.titleIndex - ctx.documentTemplate.expectedTitleLineIndex;

        // 추출 범위는 [begin, end) - 시작 행 포함, 끝 행 제외
        // 범위를 빈 상태로 초기화해서 위치 조건에 맞는 행이 없으면 누락 처리
        size_t begin = ctx.rows.size();
        size_t end = ctx.rows.size();

        if (field.position == EIdCardFieldPosition::FromStart && field.lineIndex < ctx.rows.size() - offset)
        {
            // FromStart: 앞쪽 잡음 행을 건너뛴 시작점에서 lineIndex번째 행을 선택(0부터 시작)
            begin = offset + field.lineIndex;
            end = begin + 1;
        }
        else if (field.position == EIdCardFieldPosition::FromEnd && field.lineIndex < ctx.rows.size() - offset)
        {
            // FromEnd: lineIndex가 0이면 마지막 행, 1이면 끝에서 두 번째 행
            begin = ctx.rows.size() - 1 - field.lineIndex;
            end = begin + 1;
        }
        else if (field.position == EIdCardFieldPosition::Range && field.trailingLineCount <= ctx.rows.size() - offset)
        {
            // Range: 보정된 시작 위치부터 마지막 trailingLineCount개 행을 제외한 범위를 선택
            // begin >= end이면 선택되는 행이 없어 누락 처리됨
            begin = (std::min)(offset + field.lineIndex, ctx.rows.size());
            end = ctx.rows.size() - field.trailingLineCount;
        }

        std::vector<std::string> texts;

        for (size_t i = begin; i < end; ++i)
            texts.push_back(RowText(ctx.rows[i]));

        // 선택한 행을 separator로 연결한 뒤 실제 필드 값으로 변환
        // 원문과 fixedValue가 있으면 고정 값을 우선 사용하고 그 외에는 라벨 제거 후 정규식 추출
        // 정규식이 불일치하면 라벨을 제거한 문자열이 반환되므로 형식 검사는 별도로 수행
        value.text = ExtractFieldValue(JoinLines(texts, 0, texts.size(), field.separator), field);

        if (!value.text.empty())
        {
            // 위치 기반 추출은 형식이 달라도 값을 보존하고 불일치는 경고로 기록
            value.found = true;

            // 선택한 첫 행과 마지막 행의 중심 y 좌표를 픽셀 단위로 저장
            // 다른 필드의 afterField/beforeField 검색 경계로 사용
            value.firstY = (ctx.rows[begin].top + ctx.rows[begin].bottom) / 2.;

            value.lastY = (ctx.rows[end - 1].top + ctx.rows[end - 1].bottom) / 2.;

            if (!IsValidField(value.text, field.validation))
            {
                ctx.result.warnings.push_back(field.displayName + " 형식을 확인해 주세요: " + value.text);
            }
        }
        else
        {
            missing();
        }

        return;
    }

    // 후보 검색 필드는 위치와 문자열 조건을 통과한 후보를 수집한 뒤 선택
    const auto &selection = field.selection;

    // 위치 기준이 없으면 세로 검색 범위를 제한하지 않음
    double lower = -(std::numeric_limits<double>::infinity)();
    double upper = (std::numeric_limits<double>::infinity)();

    // 검색에 필요한 기준 값을 가져오기 위해서 템플릿 순서와 관계없이 참조 필드 먼저 추출
    // notBeforeField는 날짜 비교에 사용할 기준 값 추출 - 검색 위치는 제한하지 않음
    for (const auto &key : {selection.afterField, selection.beforeField, selection.notBeforeField})
    {
        if (key.empty())
            continue;

        const size_t dependency = fieldIndex(key);

        // 참조하는 기준 필드를 재귀적으로 먼저 처리
        ResolveField(dependency, ctx);

        // 기준 값이 없으면 현재 필드 추출 중단 - 잘못된 후보 선택 방지
        // 필수 필드는 기준 필드 누락 오류 기록 - 선택 필드는 현재 필드 값 생략
        if (!ctx.values[dependency].found)
        {
            if (field.required)
            {
                ctx.result.errors.push_back(field.displayName + " 검색 기준 필드가 없습니다: " + key);
            }

            return;
        }

        // afterField는 마지막 행 아래, beforeField는 첫 행 위를 검색
        if (key == selection.afterField)
            lower = ctx.values[dependency].lastY;

        if (key == selection.beforeField)
            upper = ctx.values[dependency].firstY;
    }

    // 검색 영역과 위치 조건에 해당하는 텍스트 박스 추출
    std::vector<TIdCardOcrTextBox> eligible;

    for (const auto &box : ctx.boxes)
    {
        const double y = (double(box.top) + box.bottom) / 2.;

        // 이미지 아래쪽으로 y가 증가하므로 lower < 중심 y < upper인 박스만 사용
        // 경계와 중심 y가 같은 박스는 제외하고 박스 전체가 범위 안에 있을 필요는 없음
        if (y <= lower || y >= upper)
            continue;

        // 검색 영역이 지정된 경우에만 박스 면적의 최소 겹침 비율을 검사
        // 겹침 비율은 겹친 면적 / ocr 박스 전체 면적 - 1e-9는 부동소수점 계산 오차 보완
        if (selection.hasSearchRegion &&
            BoxOverlap(box, selection.searchRegion, ctx.imageWidth, ctx.imageHeight) + 1e-9 < selection.minOverlapRatio)
        {
            continue;
        }

        eligible.push_back(box);
    }

    // 후보 값과 원본 행의 중심 y 좌표를 함께 보관해서 후속 필드의 위치 기준으로 사용
    struct TCandidate
    {
        std::string value;
        double y = 0;
    };

    std::vector<TCandidate> candidates;

    const std::regex valueRegex(field.valuePattern);
    const std::regex textRegex(selection.textPattern);

    // 검색 범위 밖의 텍스트가 섞이지 않도록 통과한 박스만 다시 논리 행으로 묶음
    // 후보 순서는 위에서 아래이며, 같은 행에서는 왼쪽에서 오른쪽으로 검색
    for (const auto &row : BuildLogicalRows(eligible))
    {
        const auto raw = RowText(row);

        // 공백을 제거한 원문에서 기준 문구 검사
        // anchorTexts 중 하나라도 같은 행 원문에 포함되면 통과함(ascii 공백 무시)
        // 주변 행은 검사하지 않으며, 기준 문구가 하나도 없는 행은 제외
        if (!selection.anchorTexts.empty() &&
            std::none_of(
                selection.anchorTexts.begin(), selection.anchorTexts.end(), [&](const std::string &anchor)
                { return RemoveAsciiWhiteSpace(raw).find(RemoveAsciiWhiteSpace(anchor)) != std::string::npos; }))
        {
            continue;
        }

        // 라벨 제거와 정규화 후 정규식 검사
        // none은 앞뒤 공백만 제거하고 removewhitespace는 내부 ascii 공백도 제거함
        // 정리한 문자열을 행 전체 패턴 검사와 후보 값 추출에 함께 사용
        const auto text = NormalizeMatchText(RemoveLeadingLabel(raw, field.labelAliases), selection.textNormalization);

        // textPattern은 정리한 행 전체를 검사하는 조건 - 부분 일치는 제외
        if (!selection.textPattern.empty() && !std::regex_match(text, textRegex))
        {
            continue;
        }

        const double y = (double(row.top) + row.bottom) / 2.;

        // 유효한 후보 값만 목록에 추가
        // 빈 값이나 필드 형식에 맞지 않는 값을 제외하기 위한 후보 추가 함수임
        // 같은 문자열이 여러 번 발견되어도 별도 후보로 유지해서 개수 검사에 반영
        const auto add = [&](const std::string &found)
        {
            const auto extracted = Trim(found);

            if (!extracted.empty() && IsValidField(extracted, field.validation))
            {
                // ocr 추출 값이 형식 검사를 통과한 경우에만 fixedValue로 대체
                candidates.push_back({field.fixedValue.empty() ? extracted : field.fixedValue, y});
            }
        };

        // valuePattern은 허용된 행에서 실제 후보 값을 추출하기 위한 정규식임
        if (field.valuePattern.empty())
        {
            // 추출 패턴이 없으면 정리된 행 전체를 후보 하나로 사용
            add(text);
        }
        else
        {
            // 첫 번째 캡처 그룹을 후보로 추가
            // 한 행에 여러 일치 항목이 있으면 각 항목의 첫 번째 캡처 그룹을 별도 후보로 추가
            // 패턴이 불일치하면 해당 행을 후보에서 제외
            for (std::sregex_iterator it(text.begin(), text.end(), valueRegex), end; it != end; ++it)
            {
                add((*it)[1].str());
            }
        }
    }

    // 후보 수 검증
    // expectedMatchCount가 0이면 개수 미지정 - 양수이면 수집한 후보 수와 일치해야 함
    // Unique는 같은 값의 중복을 포함하여 후보가 둘 이상이면 오류
    // 후보 수 조건 불일치는 required가 false인 선택 필드에서도 오류로 처리
    if ((selection.expectedMatchCount && candidates.size() != selection.expectedMatchCount) ||
        (selection.mode == EIdCardCandidateSelectMode::Unique && candidates.size() > 1))
    {
        ctx.result.errors.push_back(field.displayName +
                                    " 후보 수가 선택 조건과 일치하지 않습니다: " + std::to_string(candidates.size()));

        return;
    }

    // 후보 수 조건을 통과했지만 후보가 없으면 필수 여부에 따라 누락 처리
    if (candidates.empty())
    {
        missing();
        return;
    }

    // 선택 방식에 따라 후보 범위 결정
    // Unique: [0, 1), All: [0, 후보 수), Occurrence: [matchIndex, matchIndex + 1).
    // matchIndex는 0부터 시작하며, Occurrence의 예상 개수와 인덱스는 로딩 시 검증됨
    const size_t begin = selection.mode == EIdCardCandidateSelectMode::Occurrence ? selection.matchIndex : 0;

    const size_t end = selection.mode == EIdCardCandidateSelectMode::All ? candidates.size() : begin + 1;

    if (begin >= candidates.size())
    {
        missing();
        return;
    }

    // 선택한 후보를 검색 순서대로 separator로 연결해서 최종 필드 값을 만듬
    for (size_t i = begin; i < end; ++i)
    {
        if (i > begin)
            value.text += field.separator;

        value.text += candidates[i].value;
    }

    // 후속 필드의 위치 검색을 위해 선택된 첫 후보와 마지막 후보의 중심 y 좌표를 저장
    value.firstY = candidates[begin].y;
    value.lastY = candidates[end - 1].y;

    // 후보 연결이나 fixedValue 적용으로 형식이 달라질 수 있으므로 최종 값을 다시 검사
    // 후보 방식에서 최종 형식이 맞지 않으면 오류 처리 - 해당 값은 추출 실패로 판단
    if (!IsValidField(value.text, field.validation))
    {
        ctx.result.errors.push_back(field.displayName + " 선택 결과의 형식이 잘못되었습니다.");

        return;
    }

    // 기준 날짜보다 선택 날짜가 이전인지 검사
    // 두 값이 모두 유효한 날짜인지 확인한 뒤 비교하고 같은 날짜는 허용
    // 앞선 검사에서 실패하면 ||의 뒤쪽 비교를 생략하여 잘못된 날짜의 숫자 변환을 방지함
    if (!selection.notBeforeField.empty())
    {
        const auto &earlier = ctx.values[fieldIndex(selection.notBeforeField)];

        if (!IsValidField(earlier.text, EIdCardFieldValidation::Date) ||
            !IsValidField(value.text, EIdCardFieldValidation::Date) ||
            dateNumber(value.text) < dateNumber(earlier.text))
        {
            ctx.result.errors.push_back(field.displayName + " 값이 기준 날짜보다 이르거나 날짜 형식이 잘못되었습니다.");

            return;
        }
    }

    // 후보 선택과 최종 검증을 모두 통과한 값만 결과 및 다른 필드의 참조 값으로 사용
    value.found = true;
}

// 위치 지정 필드만 있는 템플릿의 논리 행 텍스트를 필드 값으로 변환
// 앞쪽 잡음 행을 제거한 뒤 위치로 추출하고 형식이 달라도 값은 보존하고 경고를 남김
TParsedIdCard IdCardParser::Parse(const std::vector<std::string> &ocrLines, const TIdCardTemplate &documentTemplate)
{
    // 파싱 결과 - 오류, 경고 발생 시 errors, warnings에 기록
    TParsedIdCard result;
    if (m_templates.empty())
    {
        result.errors.push_back("신분증 템플릿이 로드되지 않았습니다.");
        return result;
    }

    // ocr 인식 결과인 raw 텍스트의 앞뒤 공백 제거, 내용 없는 라인은 파싱 대상에서 제외
    std::vector<std::string> lines;
    lines.reserve(ocrLines.size());
    for (const auto &line : ocrLines)
    {
        const std::string trimmed = Trim(line);
        if (!trimmed.empty())
            lines.push_back(trimmed);
    }

    // 파싱 대상 텍스트가 유효한지 검사
    if (lines.empty())
    {
        result.errors.push_back("파싱할 OCR 텍스트가 없습니다.");
        return result;
    }

    // 이미 선택된 템플릿의 제목을 필터링된 행에서 다시 찾아 위치 보정 기준을 확보
    const auto *matched = &documentTemplate;
    const auto titleLine = std::find_if(lines.begin(), lines.end(),
                                        [matched](const std::string &line) { return MatchesTitle(line, *matched); });
    if (titleLine == lines.end())
    {
        result.errors.push_back("선택한 템플릿의 제목을 찾지 못했습니다. 제외 영역 설정을 확인하세요.");
        return result;
    }
    const size_t titleLineIndex = static_cast<size_t>(std::distance(lines.begin(), titleLine));

    // 파싱에 사용하는 템플릿 아이디와 신분증 종류를 결과에 저장
    result.templateId = matched->id;
    result.documentType = matched->displayName;

    // 제목 앞 행이 예상보다 부족하면 위치 기준을 맞출 수 없어 오류로 종료
    // 예상보다 앞쪽 행이 많으면 초과분만 잡음으로 제거 - 면허종류 같은 정상 선행 행은 보존
    if (titleLineIndex < matched->expectedTitleLineIndex)
    {
        result.errors.push_back(matched->displayName + " 제목 앞의 필수 라인이 인식되지 않았습니다.");
        return result;
    }

    const size_t leadingNoiseLineCount = titleLineIndex - matched->expectedTitleLineIndex;
    if (leadingNoiseLineCount > 0)
        lines.erase(lines.begin(), lines.begin() + leadingNoiseLineCount);

    // 잡음 제거 후 남은 행 수가 템플릿의 최소 구성에 맞는지 검사
    if (lines.size() < matched->minimumLineCount)
    {
        result.errors.push_back("인식된 줄 수가 " + matched->displayName + "의 최소 구성보다 적습니다.");
        return result;
    }

    // 템플릿에 정의된 순서대로 각 필드의 값 추출
    for (const auto &field : matched->fields)
    {
        std::string value;
        if (field.position == EIdCardFieldPosition::FromStart)
        {
            // 문서의 첫 라인을 기준으로 고정된 위치에서 값을 가져옴
            if (field.lineIndex < lines.size())
                value = lines[field.lineIndex];
        }
        else if (field.position == EIdCardFieldPosition::FromEnd)
        {
            // 문서의 마지막 라인을 기준으로 역방향의 고정된 위치에서 값을 가져옴
            if (field.lineIndex < lines.size())
                value = lines[lines.size() - 1 - field.lineIndex];
        }
        else
        {
            // 가변 길이 필드는 시작 위치부터 문서 끝의 고정 필드들을 제외한 범위를 하나로 합침
            const size_t end = lines.size() >= field.trailingLineCount ? lines.size() - field.trailingLineCount : 0;
            if (field.lineIndex < end)
                value = JoinLines(lines, field.lineIndex, end, field.separator);
        }

        // 원문이 있으면 fixedValue를 우선 적용 - 그 외에는 라벨 제거와 정규식 추출 수행
        value = ExtractFieldValue(value, field);

        // 필수 필드가 비어 있으면 오류로 기록하고 다음 필드의 파싱 계속 진행
        if (value.empty())
        {
            if (field.required)
                result.errors.push_back(field.displayName + " 필드를 찾지 못했습니다.");
            continue;
        }

        // 찾은 필드를 템플릿 표시 순서대로 결과에 저장
        result.orderedFields.push_back({field.key, field.displayName, value});

        // validation에 지정된 형식으로 검사하며, 불일치는 추출 값을 유지한 채 경고로 기록
        if (!IsValidField(value, field.validation))
            result.warnings.push_back(field.displayName + " 형식을 확인해 주세요: " + value);
    }

    // 오류가 없으면 파싱 성공으로 처리 - 경고만 있는 경우도 성공으로 판단
    result.success = result.errors.empty();
    return result;
}

// 좌표와 필드 간 참조를 사용해서 후보 검색 - 위치 지정 필드도 함께 처리
TParsedIdCard IdCardParser::ParseCandidates(const std::vector<TIdCardOcrTextBox> &boxes,
                                            const TIdCardTemplate &documentTemplate, float imageWidth,
                                            float imageHeight)
{
    // 신분증 파싱 결과
    TParsedIdCard result;
    result.templateId = documentTemplate.id;
    result.documentType = documentTemplate.displayName;

    // 제외 규칙 적용 후 남은 박스를 논리 행으로 묶어 제목과 필드 위치의 기준을 만듦
    const auto rows = BuildLogicalRows(boxes);

    const auto title = std::find_if(rows.begin(), rows.end(), [&](const TLogicalRow &row)
                                    { return MatchesTitle(RowText(row), documentTemplate); });

    // 제외 규칙으로 제목이 사라진 경우 잘못된 위치 추출을 막기 위해 종료
    if (title == rows.end())
    {
        result.errors.push_back("선택한 템플릿의 제목을 찾지 못했습니다. 제외 규칙을 확인하세요.");

        return result;
    }

    // 제외 규칙 적용 후 전체 논리 행 수 검사 - 앞쪽 잡음 행을 빼기 전의 행 수 기준
    if (rows.size() < documentTemplate.minimumLineCount)
    {
        result.errors.push_back("인식된 줄 수가 " + documentTemplate.displayName + "의 최소 구성보다 적습니다.");

        return result;
    }

    // 입력 박스, 논리 행, 제목 위치와 결과 연결 - 필드별 값과 처리 상태 초기화
    TResolveContext context(boxes, rows, documentTemplate, imageWidth, imageHeight,
                            static_cast<size_t>(title - rows.begin()), result);

    // 모든 필드 해석 - 참조하는 기준 필드는 재귀적으로 먼저 처리
    for (size_t i = 0; i < context.values.size(); ++i)
    {
        ResolveField(i, context);
    }

    // 참조 때문에 추출 순서가 달라도 성공한 필드는 템플릿의 표시 순서대로 저장
    for (size_t i = 0; i < context.values.size(); ++i)
    {
        if (context.values[i].found)
        {
            result.orderedFields.push_back(
                {documentTemplate.fields[i].key, documentTemplate.fields[i].displayName, context.values[i].text});
        }
    }

    // 오류가 없으면 파싱 성공 - 위치 지정 필드의 형식 경고는 실패로 처리하지 않음
    result.success = result.errors.empty();

    return result;
}

// 외부 OCR 박스 입력을 받아서 유효성 검사, 제목 판별, 제외 규칙 적용, 필드 추출
TParsedIdCard IdCardParser::ParseTextBoxes(const std::vector<TIdCardOcrTextBox> &ocrTextBoxes, float imageWidth, float imageHeight)
{
    TParsedIdCard result;
    if (m_templates.empty())
    {
        result.errors.push_back("신분증 템플릿이 로드되지 않았습니다.");
        return result;
    }

    // 빈 텍스트, 유한하지 않은 좌표, 너비나 높이가 0 이하인 박스를 파싱 대상에서 제외
    std::vector<TIdCardOcrTextBox> validBoxes;
    for (const auto &box : ocrTextBoxes)
    {
        if (IsUsableBox(box)) validBoxes.push_back(box);
    }

    if (validBoxes.empty())
    {
        result.errors.push_back("파싱할 OCR 텍스트가 없습니다.");
        return result;
    }

    // 제외 규칙을 고르기 위해 필터링 전 유효 박스의 제목으로 템플릿을 먼저 선택
    // 단일 박스로 판별되지 않으면 논리 행으로 합쳐 재검사, 먼저 일치한 템플릿 사용
    auto selected = std::find_if(m_templates.begin(), m_templates.end(),
        [&validBoxes](const TIdCardTemplate &item)
        {
            return std::any_of(validBoxes.begin(), validBoxes.end(),
                [&item](const TIdCardOcrTextBox &box) { return MatchesTitle(box.text, item); });
        });

    if (selected == m_templates.end())
    {
        const auto titleLines = BuildLogicalLines(validBoxes);
        selected = std::find_if(m_templates.begin(), m_templates.end(),
            [&titleLines](const TIdCardTemplate &item)
            {
                return std::any_of(titleLines.begin(), titleLines.end(),
                    [&item](const std::string &line) { return MatchesTitle(line, item); });
            });
    }

    if (selected == m_templates.end())
    {
        result.errors.push_back("지원하는 신분증 템플릿을 찾지 못했습니다.");
        return result;
    }

    // 후보 방식이 하나라도 있으면 위치 지정 필드도 함께 ParseCandidates에서 처리
    bool hasCandidates =
        std::any_of(selected->fields.begin(), selected->fields.end(), 
            [](const TIdCardFieldTemplate &field)
                    { return field.position == EIdCardFieldPosition::Candidates; });

    // 영역 규칙이 있거나 후보 방식이면 ocr 입력 이미지 크기가 유한한 양수인지 검사
    // 크기는 박스 좌표와 같은 이미지 기준 - 상대 좌표 영역을 픽셀 좌표로 변환할 때 사용
    if ((!selected->excludeRegions.empty() || hasCandidates) &&
        (!std::isfinite(imageWidth) || !std::isfinite(imageHeight) || imageWidth <= 0 || imageHeight <= 0))
    {
        result.templateId = selected->id;
        result.documentType = selected->displayName;
        result.errors.push_back("영역 및 후보 규칙 적용에는 OCR 입력 이미지의 유효한 너비와 높이가 필요합니다.");
        return result;
    }

    std::vector<TIdCardOcrTextBox> filtered;
    
    // 제외한 박스의 원문과 규칙명을 따로 보관 - 결과에서 제외 이유를 확인할 수 있게 함
    std::vector<TParsedIdCard::TExcludedTextBox> excluded;
   
    // 영역 겹침과 지정된 텍스트 조건을 만족한 박스를 제외하고 첫 일치 규칙명 기록
    for (const auto &box : validBoxes)
    {
        std::string ruleName;
        if (IsExcludedBox(box, *selected, imageWidth, imageHeight, ruleName))
            excluded.push_back({box.text, ruleName});
        else
            filtered.push_back(box);
    }

    // 제외된 텍스트가 행 위치나 후보에 섞이지 않도록 필터링 후 논리 행을 구성
    // 앞에서 선택한 템플릿을 그대로 전달 - 필터링 후 신분증 종류가 바뀌지 않게 함
    if (hasCandidates)
    {
        result = ParseCandidates(filtered, *selected, imageWidth, imageHeight);
    }
    else
    {
        result = Parse(BuildLogicalLines(filtered), *selected);
    }

    // 필드 추출 결과에 제외 진단 정보를 함께 반환 - 추출 실패 시에도 제외 내역은 남음
    result.excludedTextBoxCount = excluded.size();
    result.excludedTextBoxes = std::move(excluded);

    return result;
}
