#ifndef TEMPLATE_JSON_H
#define TEMPLATE_JSON_H

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

#include "IdCardParseDef.h"

namespace TemplateJson
{
	// json 타입 별칭 사용
	using Json = nlohmann::json;

    // 오류 메시지 구성
	[[noreturn]] void SetErrorMsg(const std::string &path, const std::string &message);

    // json 객체가 유효한지 검사, 허용된 속성명 검사
    void CheckObject(const Json &object, const std::string &path, std::initializer_list<const char *> allowedKeys);

    // 로드된 json 객체에서 key에 해당하는 속성의 값을 가져옴 - 누락 시 json 경로를 포함한 예외 발생
	const Json &Required(const Json &object, const char *key, const std::string &path);

    // 가져온 json 값이 0 보다 큰 정수이고 size_t 범위에 있는지 검사하고 조건에 맞으면 반환
    size_t ReadIndex(const Json &value, const std::string &path);

    // 신분증 템플릿 생성
    TIdCardTemplate CreateTemplate(const Json &object, const std::string &path);

    // 공통 필드 참조를 실제 설정으로 펼치고 생략된 필드 위치와 후보 선택 기본값 구성
    Json ExpandCompactTemplates(Json root);

    } // namespace TemplateJson

#endif // template_json_h
