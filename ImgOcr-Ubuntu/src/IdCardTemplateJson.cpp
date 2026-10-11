#include "IdCardTemplateJson.h"
#include "CommonUtil.h"

// json 으로 구성된 신분증 템플릿 설정 읽기, 검증, 템플릿 변환에 사용하는 네임스페이스
namespace TemplateJson
{
// 등록번호, 날짜 필드에 대해서 형식 검사
bool IsValidField(const std::string &value, EIdCardFieldValidation validation)
{
    // 공백 제거 - 오류 방지
    std::string textVal = RemoveAsciiWhiteSpace(value);

    switch (validation)
    {
    // 주민등록번호 형식 검사 - 숫자 6자리 '-' 숫자 7자리
    case EIdCardFieldValidation::ResidentRegistrationNumber:
        return std::regex_match(textVal, std::regex(R"(\d{6}-?\d{7})"));

    // 운전면허등록번호 형식 검사 - 2자리-2자리-6자리-2자리
    case EIdCardFieldValidation::DriverLicenseNumber:
        return std::regex_match(textVal, std::regex(R"(\d{2}-\d{2}-\d{6}-\d{2})"));

    // 날짜 형식 검사
    case EIdCardFieldValidation::Date:
    {
        std::smatch match;
        
        if (!std::regex_match(textVal, match, std::regex(R"((\d{4})[.\-/](\d{1,2})[.\-/](\d{1,2})\.?)")))
            return false;
        
        const int year = std::stoi(match[1]), month = std::stoi(match[2]), day = std::stoi(match[3]);
        
        if (year < 1 || month < 1 || month > 12 || day < 1) return false;
        
        const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        
        // 4년마다 윤년이지만 100의 배수는 제외하고 400의 배수는 다시 포함하는 달력 규칙
        const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        return day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
    }

    // 한국 이름 형식 검사
    case EIdCardFieldValidation::KoreanName:
    {
        // std::regex의 바이트 기반 한글 범위 대신 UTF-8 음절을 직접 검사
        size_t count = 0;
        for (size_t i = 0; i < textVal.size(); i += 3)
        {
            if (i + 2 >= textVal.size()) return false;

            const auto a = static_cast<unsigned char>(textVal[i]);
            const auto b = static_cast<unsigned char>(textVal[i + 1]);
            const auto c = static_cast<unsigned char>(textVal[i + 2]);
            
            if ((a & 0xF0) != 0xE0 || 
                (b & 0xC0) != 0x80 || 
                (c & 0xC0) != 0x80)
                return false;
            
            // UTF-8 3바이트를 유니코드 코드 포인트로 복원하고 완성형 한글 음절 범위 확인
            const unsigned code = ((a & 15) << 12) | ((b & 63) << 6) | (c & 63);
            
            if (code < 0xAC00 || code > 0xD7A3) return false;
            
            ++count;
        }
        return count >= 2 && count <= 10;
    }

    default:
        return true;
    }
}

// 오류 메시지 구성
[[noreturn]] void SetErrorMsg(const std::string &path, const std::string &message)
{
    throw std::runtime_error(path + ": " + message);
}

// json 객체가 유효한지 검사, 허용된 속성명 검사
void CheckObject(const Json &object, const std::string &path, std::initializer_list<const char *> allowedKeys)
{
    if (!object.is_object())
        SetErrorMsg(path, "객체가 필요합니다.");

    for (auto item = object.begin(); item != object.end(); ++item)
    {
        bool rt = std::none_of(allowedKeys.begin(), allowedKeys.end(),
                               [&item](const char *key) { return item.key() == key; });

        if (rt)
            SetErrorMsg(path + "." + item.key(), "지원하지 않는 속성입니다.");
    }
}

// 로드된 json 객체에서 key에 해당하는 속성의 값을 가져옴 - 누락 시 json 경로를 포함한 예외 발생
const Json &Required(const Json &object, const char *key, const std::string &path)
{
    const auto item = object.find(key);

    if (item == object.end())
        SetErrorMsg(path + "." + key, "해당 속성이 없습니다.");

    return *item;
}

// 가져온 json 값이 문자열이 맞는지, 공백을 허용하는지 검사하고 조건에 맞으면 반환
std::string ReadString(const Json &value, const std::string &path, bool allowEmpty = false)
{
    if (!value.is_string())
        SetErrorMsg(path, "문자열이 필요합니다.");

    const std::string text = value.get<std::string>();

    if (!allowEmpty && Trim(text).empty())
        SetErrorMsg(path, "빈 문자열은 허용하지 않습니다.");

    return text;
}

// 가져온 json 값이 0 보다 큰 정수이고 size_t 범위에 있는지 검사하고 조건에 맞으면 반환
size_t ReadIndex(const Json &value, const std::string &path)
{
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
        SetErrorMsg(path, "0 이상의 정수가 필요합니다.");

    const uint64_t number = value.get<uint64_t>();

    if (number > (std::numeric_limits<size_t>::max)())
        SetErrorMsg(path, "정수가 허용 범위를 초과했습니다.");

    return static_cast<size_t>(number);
}

// 가져온 json 값이 배열인지 검사하고 맞으면 문자열 목록으로 구성해서 반환
std::vector<std::string> ReadStrings(const Json &value, const std::string &path)
{
    if (!value.is_array())
        SetErrorMsg(path, "문자열 배열이 필요합니다.");

    std::vector<std::string> strings;
    for (size_t i = 0; i < value.size(); ++i)
        strings.push_back(ReadString(value[i], path + "[" + std::to_string(i) + "]"));

    return strings;
}

// 가져온 json 값에 대해서 숫자, NaN, 무한대인지 검사하고 조건에 맞으면 반환
// 좌표나 비율 계산에 사용할 숫자를 가져올 때 사용
double ReadNumber(const Json &value, const std::string &path)
{
    if (!value.is_number())
        SetErrorMsg(path, "숫자가 필요합니다.");
    const double number = value.get<double>();
    if (!std::isfinite(number))
        SetErrorMsg(path, "유한한 숫자가 필요합니다.");
    return number;
}

// 텍스트 박스 유효성 검사 - 좌표, 크기 검사
void ValidateRect(const TRelativeRect &rect, const std::string &path)
{
    if (!std::isfinite(rect.left) || !std::isfinite(rect.top) ||

        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||

        rect.left < 0 || rect.top < 0 || rect.width <= 0 || rect.height <= 0 ||

        rect.left + rect.width > 1.0 + 1e-9 || rect.top + rect.height > 1.0 + 1e-9)
    {
        SetErrorMsg(path, "이미지 안의 양수 크기 0~1 비율 사각형이 필요합니다.");
    }
}

// json 파일에 있는 정규식 문법 검사 - 값 추출 용도에 사용하는 정규식에 대해서는 캡처 그룹이 있는지 검사
// 날짜, 등록번호 등의 형식 검사와 값 추출용 정규식에 사용
void ValidatePattern(const std::string &pattern, const std::string &path, bool capture = false)
{
    if (pattern.empty())
        return;

    try
    {
        const std::regex compiled(pattern);

        if (capture && compiled.mark_count() == 0)
        {
            SetErrorMsg(path, "값 추출을 위한 캡처 그룹이 필요합니다.");
        }
    }
    catch (const std::regex_error &e)
    {
        SetErrorMsg(path, std::string("잘못된 정규식: ") + e.what());
    }
}

// ocr 결과 문자열에 적용할 정규화 방식의 유효성 검사
// 현재 정규화 없음에 해당하는 none과 공백 제거에 해당하는 RemoveWhitespace만 있음
void ValidateNormalization(EIdCardTextNormalization mode, const std::string &path)
{
    if (mode != EIdCardTextNormalization::None && mode != EIdCardTextNormalization::RemoveWhitespace)
        SetErrorMsg(path, "지원하지 않는 문자열 정규화 방식입니다.");
}

// left, top, width, height 순서의 배열을 읽고 이미지 안에 있는 상대 좌표 영역인지 검사
TRelativeRect ReadRect(const Json &value, const std::string &path)
{
    if (!value.is_array() || value.size() != 4)
        SetErrorMsg(path, "[left, top, width, height] 배열이 필요합니다.");

    TRelativeRect result{ReadNumber(value[0], path + "[0]"), ReadNumber(value[1], path + "[1]"),
                       ReadNumber(value[2], path + "[2]"), ReadNumber(value[3], path + "[3]")};

    ValidateRect(result, path);
    return result;
}

// json의 정규화 이름을 열거형으로 변환하고 알 수 없는 이름은 오류로 처리
EIdCardTextNormalization ReadNormalization(const Json &value, const std::string &path)
{
    const auto text = ReadString(value, path);
   
    if (text == "None")
        return EIdCardTextNormalization::None;
    
    if (text == "RemoveWhitespace")
        return EIdCardTextNormalization::RemoveWhitespace;
    
    SetErrorMsg(path, "None 또는 RemoveWhitespace가 필요합니다.");
}

// 생략된 속성은 구조체 기본값 유지 - 지정된 값만 기존 타입 검사로 읽음
template <typename T, typename Reader, typename... Args>
void ReadOptional(const Json &object, const char *key, const std::string &path, T &target, Reader reader, Args... args)
{
    const auto item = object.find(key);
    if (item != object.end())
        target = reader(*item, path + "." + key, args...);
}

// 후보 검색 영역, 문자열 조건, 기준 필드와 선택 방식 읽음 - 생략된 선택 속성은 기본값 사용
TIdCardFieldSelection ReadSelection(const Json &object, const std::string &path)
{
    CheckObject(object, path,
                {"searchRegion", "minOverlapRatio", "textPattern", "matchText", "anchorTexts", "afterField",
                 "beforeField", "notBeforeField", "mode", "matchIndex", "expectedMatchCount"});

    TIdCardFieldSelection selection;

    if (object.contains("searchRegion"))
    {
        selection.hasSearchRegion = true;
        selection.searchRegion = ReadRect(object.at("searchRegion"), path + ".searchRegion");
    }

    ReadOptional(object, "minOverlapRatio", path, selection.minOverlapRatio, ReadNumber);

    ReadOptional(object, "textPattern", path, selection.textPattern, ReadString, true);

    ReadOptional(object, "matchText", path, selection.textNormalization, ReadNormalization);

    ReadOptional(object, "anchorTexts", path, selection.anchorTexts, ReadStrings);

    ReadOptional(object, "afterField", path, selection.afterField, ReadString, false);

    ReadOptional(object, "beforeField", path, selection.beforeField, ReadString, false);

    ReadOptional(object, "notBeforeField", path, selection.notBeforeField, ReadString, false);

    // Unique는 후보 하나, All은 모든 후보, Occurrence는 지정된 순번의 후보를 선택
    if (object.contains("mode"))
    {
        const auto mode = ReadString(object.at("mode"), path + ".mode");

        if (mode == "Unique")
            selection.mode = EIdCardCandidateSelectMode::Unique;
        else if (mode == "All")
            selection.mode = EIdCardCandidateSelectMode::All;
        else if (mode == "Occurrence")
            selection.mode = EIdCardCandidateSelectMode::Occurrence;
        else
            SetErrorMsg(path + ".mode", "Unique, All 또는 Occurrence가 필요합니다.");
    }

    ReadOptional(object, "matchIndex", path, selection.matchIndex, ReadIndex);

    ReadOptional(object, "expectedMatchCount", path, selection.expectedMatchCount, ReadIndex);

    return selection;
}

// 템플릿 전체의 필수 정보와 제목 위치 검사
void ValidateTemplateBasics(const TIdCardTemplate &documentTemplate, const std::string &path)
{
    if (Trim(documentTemplate.id).empty())
        SetErrorMsg(path + ".id", "템플릿 ID가 비어 있습니다.");

    if (Trim(documentTemplate.displayName).empty())
        SetErrorMsg(path + ".displayName", "표시 이름이 비어 있습니다.");

    if (documentTemplate.titleKeywords.empty())
        SetErrorMsg(path + ".titleKeywords", "제목 키워드가 필요합니다.");

    for (const auto &keyword : documentTemplate.titleKeywords)
        if (Trim(keyword).empty())
            SetErrorMsg(path + ".titleKeywords", "빈 제목 키워드는 허용하지 않습니다.");

    if (documentTemplate.minimumLineCount == 0 ||
        documentTemplate.expectedTitleLineIndex >= documentTemplate.minimumLineCount)
        SetErrorMsg(path + ".expectedTitleLineIndex", "제목 위치는 최소 줄 수 범위 안이어야 합니다.");

    if (documentTemplate.fields.empty())
        SetErrorMsg(path + ".fields", "필드가 하나 이상 필요합니다.");
}

// 제외 영역 하나의 좌표, 정규식, 문자열 정규화 방식, 겹침 기준 검사
void ValidateExcludeRegion(const TIdCardExcludeRegion &region, const std::string &regionPath)
{
    if (!std::isfinite(region.left) || !std::isfinite(region.top) || !std::isfinite(region.width) ||
        !std::isfinite(region.height) || region.left < 0 || region.top < 0 || region.width <= 0 || region.height <= 0 ||
        region.left + region.width > 1.0 + 1e-9 || region.top + region.height > 1.0 + 1e-9)
    {
        SetErrorMsg(regionPath + ".rect", "이미지 안에 있는 양수 크기의 0~1 비율 사각형이 필요합니다.");
    }

    ValidatePattern(region.textPattern, regionPath + ".textPattern");
    ValidateNormalization(region.textNormz, regionPath + ".matchText");

    if (!std::isfinite(region.minOverlapRatio) || region.minOverlapRatio <= 0 || region.minOverlapRatio > 1)
    {
        SetErrorMsg(regionPath + ".minOverlapRatio", "0보다 크고 1 이하인 겹침 비율이 필요합니다.");
    }
}

// candidates 필드에서 후보를 찾고 선택할 때 사용하는 설정 검사
void ValidateCandidateSelection(const TIdCardFieldSelection &selection, const std::string &fieldPath)
{
    // 검색 영역, 겹침 기준, 텍스트 조건이 유효한지 검사
    if (selection.hasSearchRegion)
        ValidateRect(selection.searchRegion, fieldPath + ".selection.searchRegion");

    if (!std::isfinite(selection.minOverlapRatio) || selection.minOverlapRatio <= 0 || selection.minOverlapRatio > 1)
        SetErrorMsg(fieldPath + ".selection.minOverlapRatio", "0보다 크고 1 이하인 값이 필요합니다.");

    ValidatePattern(selection.textPattern, fieldPath + ".selection.textPattern");
    ValidateNormalization(selection.textNormalization, fieldPath + ".selection.matchText");

    for (const auto &anchor : selection.anchorTexts)
    {
        if (Trim(anchor).empty())
            SetErrorMsg(fieldPath + ".selection.anchorTexts", "빈 문구는 허용하지 않습니다.");
    }

    // occurrence는 후보 수와 인덱스를 지정하고 다른 방식은 인덱스 0만 허용
    if (selection.mode == EIdCardCandidateSelectMode::Occurrence)
    {
        if (selection.expectedMatchCount == 0 || selection.matchIndex >= selection.expectedMatchCount)
            SetErrorMsg(fieldPath + ".selection.matchIndex", "Occurrence는 예상 후보 수와 그 범위 내 인덱스가 필요합니다.");
    }
    else if (selection.mode != EIdCardCandidateSelectMode::Unique && selection.mode != EIdCardCandidateSelectMode::All)
    {
        SetErrorMsg(fieldPath + ".selection.mode", "지원하지 않는 선택 방식입니다.");
    }
    else if (selection.matchIndex != 0 ||
             (selection.mode == EIdCardCandidateSelectMode::Unique && selection.expectedMatchCount > 1))
    {
        SetErrorMsg(fieldPath + ".selection.matchIndex", "후보 수/인덱스가 선택 방식과 일치하지 않습니다.");
    }
}

// 필드 하나의 기본 정보와 추출 조건 검사 - 키 중복 검사용 목록은 모든 필드가 공유
void ValidateField(const TIdCardFieldTemplate &field, size_t minimumLineCount, std::set<std::string> &keys,
                   const std::string &fieldPath)
{
    // keys는 앞에서 검사한 필드의 키를 보관 - 중복 찾음
    if (Trim(field.key).empty() || !keys.insert(field.key).second)
        SetErrorMsg(fieldPath + ".key", "필드 키가 비어 있거나 중복됩니다.");

    if (Trim(field.displayName).empty())
        SetErrorMsg(fieldPath + ".displayName", "표시 이름이 비어 있습니다.");

    // 위치 지정 방식마다 허용되는 행 인덱스와 제외 줄 수 검사
    switch (field.position)
    {
    case EIdCardFieldPosition::FromStart:
    case EIdCardFieldPosition::FromEnd:
        if (field.trailingLineCount != 0)
            SetErrorMsg(fieldPath + ".trailingLineCount", "Range 필드에서만 사용할 수 있습니다.");
        break;
    case EIdCardFieldPosition::Range:
        break;
    case EIdCardFieldPosition::Candidates:
        if (field.trailingLineCount != 0 || field.lineIndex != 0)
            SetErrorMsg(fieldPath + ".lineIndex", "Candidates에서는 행 인덱스를 사용하지 않습니다.");
        break;
    default:
        SetErrorMsg(fieldPath + ".position", "지원하지 않는 위치 지정 방식입니다.");
    }
    if (field.position != EIdCardFieldPosition::Candidates && field.required &&
        (field.lineIndex >= minimumLineCount || (field.position == EIdCardFieldPosition::Range &&
                                                 field.trailingLineCount >= minimumLineCount - field.lineIndex)))
        SetErrorMsg(fieldPath + ".lineIndex", "최소 줄 수에서 필수 필드를 추출할 수 없습니다.");

    // 형식 검사 종류, 라벨, 값 추출 패턴, 고정 값 형식 검사
    switch (field.validation)
    {
    case EIdCardFieldValidation::None:
    case EIdCardFieldValidation::ResidentRegistrationNumber:
    case EIdCardFieldValidation::DriverLicenseNumber:
    case EIdCardFieldValidation::Date:
    case EIdCardFieldValidation::KoreanName:
        break;
    default:
        SetErrorMsg(fieldPath + ".validation", "지원하지 않는 형식 검사입니다.");
    }

    for (const auto &label : field.labelAliases)
        if (Trim(label).empty())
            SetErrorMsg(fieldPath + ".labelAliases", "빈 라벨은 허용하지 않습니다.");

    ValidatePattern(field.valuePattern, fieldPath + ".valuePattern", true);

    if (!field.fixedValue.empty() && !IsValidField(field.fixedValue, field.validation))
        SetErrorMsg(fieldPath + ".fixedValue", "고정 값이 필드 형식과 일치하지 않습니다.");

    // 후보 선택 방식의 설정은 별도 함수에서 검사
    if (field.position == EIdCardFieldPosition::Candidates)
        ValidateCandidateSelection(field.selection, fieldPath);
}

// 깊이 우선 탐색에서 각 필드의 검사 진행 상태 구분
enum class EFieldDependencyState
{
    NotVisited, // 아직 검사하지 않은 필드
    Visiting,   // 현재 참조 경로에서 검사 중인 필드
    Completed   // 이 필드와 참조한 필드의 검사가 모두 끝난 상태
};

// 현재 필드가 참조하는 필드를 따라가면서 없는 참조, 날짜 조건, 순환 참조 검사
void VisitFieldDependency(size_t index, const TIdCardTemplate &documentTemplate,
                          std::vector<EFieldDependencyState> &states, const std::string &path)
{
    // 검사 완료된 필드는 다시 검사하지 않음
    if (states[index] == EFieldDependencyState::Completed) return;

    const std::string fieldPath = path + ".fields[" + std::to_string(index) + "]";
    
    // 현재 검사 경로에 있는 필드를 다시 방문하면 참조가 순환됨 - 오류 메시지 생성
    if (states[index] == EFieldDependencyState::Visiting)
        SetErrorMsg(fieldPath + ".selection", "필드 참조가 순환합니다.");

    states[index] = EFieldDependencyState::Visiting;
    const auto &field = documentTemplate.fields[index];
    if (field.position == EIdCardFieldPosition::Candidates)
    {
        // 위치 기준과 날짜 비교 기준을 원래 순서대로 검사 - 빈 참조는 사용하지 않음
        for (const auto &key :
             {field.selection.afterField, field.selection.beforeField, field.selection.notBeforeField})
        {
            if (key.empty())
                continue;

            const auto found = std::find_if(documentTemplate.fields.begin(), documentTemplate.fields.end(),
                                            [&key](const TIdCardFieldTemplate &item) { return item.key == key; });
            if (found == documentTemplate.fields.end())
                SetErrorMsg(fieldPath + ".selection", "참조한 필드가 없습니다: " + key);

            // notBeforeField는 현재 필드와 참조 필드가 모두 날짜 형식일 때만 사용
            if (key == field.selection.notBeforeField &&
                (field.validation != EIdCardFieldValidation::Date || found->validation != EIdCardFieldValidation::Date))
                SetErrorMsg(fieldPath + ".selection.notBeforeField", "날짜 필드 사이에서만 사용할 수 있습니다.");

            VisitFieldDependency(static_cast<size_t>(found - documentTemplate.fields.begin()), documentTemplate, states,
                                 path);
        }
    }

    // 모든 참조에 대한 검사가 끝나면 현재 필드를 완료 상태로 표시
    states[index] = EFieldDependencyState::Completed;
}

// 신분증 종류와 무관한 필드 참조와 순환 의존성 검사
void ValidateFieldDependencies(const TIdCardTemplate &documentTemplate, const std::string &path)
{
    std::vector<EFieldDependencyState> states(documentTemplate.fields.size(), EFieldDependencyState::NotVisited);
    for (size_t i = 0; i < states.size(); ++i)
        VisitFieldDependency(i, documentTemplate, states, path);
}

// 기본 정보 - 제외 영역 - 개별 필드 - 필드 간 참조 순서로 템플릿 검사
// 각 검증 함수는 처음 오류 발생 시 오류 메시지 생성
void ValidateTemplate(const TIdCardTemplate &documentTemplate, const std::string &path)
{
    ValidateTemplateBasics(documentTemplate, path);

    for (size_t i = 0; i < documentTemplate.excludeRegions.size(); ++i)
    {
        const std::string regionPath = path + ".excludeRegions[" + std::to_string(i) + "]";
        ValidateExcludeRegion(documentTemplate.excludeRegions[i], regionPath);
    }

    std::set<std::string> keys;
    for (size_t i = 0; i < documentTemplate.fields.size(); ++i)
    {
        const std::string fieldPath = path + ".fields[" + std::to_string(i) + "]";
        ValidateField(documentTemplate.fields[i], documentTemplate.minimumLineCount, keys, fieldPath);
    }

    // 모든 필드의 정의를 검사한 뒤 서로 참조하는 관계를 검사
    ValidateFieldDependencies(documentTemplate, path);
}

// 신분증 템플릿 생성
TIdCardTemplate CreateTemplate(const Json &object, const std::string &path)
{
    CheckObject(object, path,
                {"id", "displayName", "titleKeywords", "expectedTitleLineIndex", "minimumLineCount", "fields",
                 "excludeRegions"});
    
    TIdCardTemplate documentTemplate;
    documentTemplate.id = ReadString(Required(object, "id", path), path + ".id");
    documentTemplate.displayName = ReadString(Required(object, "displayName", path), path + ".displayName");
    documentTemplate.titleKeywords = ReadStrings(Required(object, "titleKeywords", path), path + ".titleKeywords");
    
    ReadOptional(object, "expectedTitleLineIndex", path, documentTemplate.expectedTitleLineIndex, ReadIndex);
    documentTemplate.minimumLineCount =
        ReadIndex(Required(object, "minimumLineCount", path), path + ".minimumLineCount");
    
    // json에 나열된 필드 순서를 유지하며, 이 순서가 파싱 결과의 표시 순서가 됨
    const auto &fields = Required(object, "fields", path);
    if (!fields.is_array())
        SetErrorMsg(path + ".fields", "필드 배열이 필요합니다.");

    for (size_t i = 0; i < fields.size(); ++i)
    {
        const auto &item = fields[i];
        const std::string fieldPath = path + ".fields[" + std::to_string(i) + "]";
        CheckObject(item, fieldPath,
                    {"key", "displayName", "position", "lineIndex", "trailingLineCount", "required", "separator",
                     "validation", "fixedValue", "labelAliases", "valuePattern", "selection"});
        TIdCardFieldTemplate field;
        field.key = ReadString(Required(item, "key", fieldPath), fieldPath + ".key");
        field.displayName = ReadString(Required(item, "displayName", fieldPath), fieldPath + ".displayName");
        
        const std::string position = ReadString(Required(item, "position", fieldPath), fieldPath + ".position");
        
        if (position == "FromStart")
            field.position = EIdCardFieldPosition::FromStart;
        else if (position == "FromEnd")
            field.position = EIdCardFieldPosition::FromEnd;
        else if (position == "Range")
            field.position = EIdCardFieldPosition::Range;
        else if (position == "Candidates")
            field.position = EIdCardFieldPosition::Candidates;
        else
            SetErrorMsg(fieldPath + ".position", "알 수 없는 위치 지정 방식: " + position);
        
        // 후보 방식은 selection으로 검색하고, 나머지 방식은 lineIndex로 행 위치를 지정함
        if (field.position != EIdCardFieldPosition::Candidates || item.contains("lineIndex"))
            field.lineIndex = ReadIndex(Required(item, "lineIndex", fieldPath), fieldPath + ".lineIndex");
        
        if (field.position == EIdCardFieldPosition::Candidates)
        {
            field.selection = ReadSelection(Required(item, "selection", fieldPath), fieldPath + ".selection");
        }
        else if (item.contains("selection"))
        {
            SetErrorMsg(fieldPath + ".selection", "Candidates 필드에서만 사용할 수 있습니다.");
        }
        
        if (field.position == EIdCardFieldPosition::Range)
        {
            field.trailingLineCount =
                ReadIndex(Required(item, "trailingLineCount", fieldPath), fieldPath + ".trailingLineCount");
        }
        else
        {
            ReadOptional(item, "trailingLineCount", fieldPath, field.trailingLineCount, ReadIndex);
        }

        if (item.contains("required"))
        {
            if (!item.at("required").is_boolean())
                SetErrorMsg(fieldPath + ".required", "true 또는 false가 필요합니다.");

            field.required = item.at("required").get<bool>();
        }
        ReadOptional(item, "separator", fieldPath, field.separator, ReadString, true);
        if (item.contains("validation"))
        {
            const std::string validation = ReadString(item.at("validation"), fieldPath + ".validation");
            if (validation == "None")
                field.validation = EIdCardFieldValidation::None;
            else if (validation == "ResidentRegistrationNumber")
                field.validation = EIdCardFieldValidation::ResidentRegistrationNumber;
            else if (validation == "DriverLicenseNumber")
                field.validation = EIdCardFieldValidation::DriverLicenseNumber;
            else if (validation == "Date")
                field.validation = EIdCardFieldValidation::Date;
            else if (validation == "KoreanName")
                field.validation = EIdCardFieldValidation::KoreanName;
            else
                SetErrorMsg(fieldPath + ".validation", "알 수 없는 형식 검사: " + validation);
        }
        ReadOptional(item, "fixedValue", fieldPath, field.fixedValue, ReadString, true);
        ReadOptional(item, "labelAliases", fieldPath, field.labelAliases, ReadStrings);
        ReadOptional(item, "valuePattern", fieldPath, field.valuePattern, ReadString, true);
        documentTemplate.fields.push_back(std::move(field));
    }

    // 제외 영역은 선택 설정이며, 텍스트 패턴을 생략하면 위치와 겹침 비율만으로 제외함
    if (object.contains("excludeRegions"))
    {
        const auto &regions = object.at("excludeRegions");
        if (!regions.is_array())
            SetErrorMsg(path + ".excludeRegions", "제외 영역 배열이 필요합니다.");

        for (size_t i = 0; i < regions.size(); ++i)
        {
            const std::string regionPath = path + ".excludeRegions[" + std::to_string(i) + "]";
            const auto &item = regions[i];
            CheckObject(item, regionPath, {"name", "rect", "minOverlapRatio", "textPattern", "matchText"});
            TIdCardExcludeRegion region;
            ReadOptional(item, "name", regionPath, region.name, ReadString, false);
            
            const auto &rect = Required(item, "rect", regionPath);
            if (!rect.is_array() || rect.size() != 4)
                SetErrorMsg(regionPath + ".rect", "[left, top, width, height] 배열이 필요합니다.");

            region.left = ReadNumber(rect[0], regionPath + ".rect[0]");
            region.top = ReadNumber(rect[1], regionPath + ".rect[1]");
            region.width = ReadNumber(rect[2], regionPath + ".rect[2]");
            region.height = ReadNumber(rect[3], regionPath + ".rect[3]");
            
            ReadOptional(item, "minOverlapRatio", regionPath, region.minOverlapRatio, ReadNumber);
            ReadOptional(item, "textPattern", regionPath, region.textPattern, ReadString, true);
            ReadOptional(item, "matchText", regionPath, region.textNormz, ReadNormalization);
            
            documentTemplate.excludeRegions.push_back(std::move(region));
        }
    }
    ValidateTemplate(documentTemplate, path);
    return documentTemplate;
}

// 공통 필드 참조를 실제 설정으로 펼치고 생략된 필드 위치와 후보 선택 기본값 구성
Json ExpandCompactTemplates(Json root)
{
    CheckObject(root, "root", {"fieldPresets", "templates"});
    const Json presets = root.value("fieldPresets", Json::object());
    
    if (!presets.is_object())
        SetErrorMsg("fieldPresets", "공통 필드 객체가 필요합니다.");
    
    for (auto it = presets.begin(); it != presets.end(); ++it)
    {
        if (Trim(it.key()).empty())
            SetErrorMsg("fieldPresets", "공통 필드 이름이 비어 있습니다.");
        
        // 프리셋 재참조(use)는 허용하지 않음
        CheckObject(it.value(), "fieldPresets." + it.key(),
                    {"key", "displayName", "position", "lineIndex", "trailingLineCount", "required", "separator",
                     "validation", "fixedValue", "labelAliases", "valuePattern", "selection"});
    }

    auto &templates = root.at("templates");
    if (!templates.is_array() || templates.empty())
        SetErrorMsg("templates", "템플릿이 하나 이상 있는 배열이 필요합니다.");
    
    for (size_t t = 0; t < templates.size(); ++t)
    {
        const auto path = "templates[" + std::to_string(t) + "]";
        auto &document = templates[t];
    
        // 필드 이외의 속성과 필드 내용은 펼친 후 CreateTemplate에서 검사
        if (!document.is_object())
            SetErrorMsg(path, "객체가 필요합니다.");
        
        const auto &fields = Required(document, "fields", path);
        
        if (!fields.is_array())
            SetErrorMsg(path + ".fields", "필드 배열이 필요합니다.");

        for (size_t i = 0; i < fields.size(); ++i)
        {
            const auto fieldPath = path + ".fields[" + std::to_string(i) + "]";
            auto &field = document["fields"][i];

            if (!field.is_object()) SetErrorMsg(fieldPath, "객체가 필요합니다.");
            
            if (field.contains("use"))
            {
                const auto name = ReadString(field.at("use"), fieldPath + ".use");
                const auto preset = presets.find(name);
                
                if (preset == presets.end())
                    SetErrorMsg(fieldPath + ".use", "공통 필드가 없습니다: " + name);
                
                Json merged = *preset;
                if (!merged.contains("key")) merged["key"] = name;
                
                // 일반 속성과 배열은 교체하고 selection만 속성별로 덮어씀
                for (auto it = field.begin(); it != field.end(); ++it)
                {
                    if (it.key() == "use") continue;

                    if (it.key() == "selection" && 
                        it.value().is_object() && 
                        merged.contains("selection") &&
                        merged["selection"].is_object())
                    {
                        merged["selection"].update(it.value());
                    }
                    else
                    {
                        merged[it.key()] = it.value();
                    }
                }

                field = std::move(merged);
            }

            if (!field.contains("position")) field["position"] = "Candidates";
            
            if (field["position"] == "Candidates" && !field.contains("selection")) field["selection"] = Json::object();
        }
    }
    root.erase("fieldPresets");
    return root;
}

} // namespace TemplateJson