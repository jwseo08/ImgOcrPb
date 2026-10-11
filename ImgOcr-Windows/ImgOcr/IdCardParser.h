#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "IdCardParseDef.h"

// ocr 인식 결과인 raw 텍스트를 신분증 템플릿 정의에 따라서 의미 있는 필드로 파싱
// 신분증 템플릿은 json 파일로 정의되어 있음 - id_card_templates.json
class IdCardParser
{
  public:
    IdCardParser();
    ~IdCardParser();

    // 신분증 템플릿이 정의되어 있는 json 파일 로드
    // 로드 시 정의되어 있는 템플릿들의 형식 검사 - 성공 시 템플릿 목록 교체 후 true 반환, 실패 시 기존 목록 유지 후 false 반환
    bool LoadTemplatesFromJson(const std::wstring &path, std::string &error);
    
    // 로드된 템플릿이 하나 이상 있으면 true 반환 - 파싱 전 템플릿 준비 여부 확인
    bool HasTemplates() const { return !m_templates.empty(); }

    // 파싱 작업 함수 - 외부 호출 함수
    // ocr 텍스트 박스의 텍스트와 좌표를 검사하고 제목에 맞는 신분증 템플릿 선택
    // 피상 제외 영역에 해당하는 후보 필드가 있으면 ParseCandidates()로 필드 추출, 없으면 Parse()로 필드 추출
    // 파싱 결과는 추출 필드, 오류, 경고, 파싱에서 제외한 텍스트 박스 내역
    TParsedIdCard ParseTextBoxes(const std::vector<TIdCardOcrTextBox> &ocrTextBoxes,
                                 float imageWidth = 0, float imageHeight = 0);

  private:
    // 신분증 필드 템플릿 목록
    std::vector<TIdCardTemplate> m_templates;

    // ParseTextBoxes 함수의 서브 함수
    // 위치 지정 필드만 있는 템플릿의 논리 행 텍스트에서 필드 값 추출
    TParsedIdCard Parse(const std::vector<std::string> &ocrLines, const TIdCardTemplate &documentTemplate);

    // ParseTextBoxes 함수의 서브 함수
    // 후보 필드가 포함된 템플릿의 필드 추출 - 위치 지정 필드도 함께 처리
    // ResolveField로 필드별 값과 참조 관계 처리 후 추출된 값을 템플릿 순서로 저장
    TParsedIdCard ParseCandidates(const std::vector<TIdCardOcrTextBox> &boxes,
                                  const TIdCardTemplate &documentTemplate,
                                  float imageWidth, float imageHeight);
    
    // index번째 필드의 위치 지정 또는 후보 선택 규칙에 따라 값 추출 - 처리 시작한 필드는 재처리 생략
    // context.values[index]로 추출 값, 위치, 성공 여부 반환 - 오류, 경고는 context.result에 기록
    void ResolveField(size_t index, TResolveContext &context);
};
