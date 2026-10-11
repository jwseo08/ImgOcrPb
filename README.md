# ImgOcr — C++ 신분증 OCR

**OpenCV · ONNX Runtime 기반 이미지 전처리, 텍스트 인식 및 신분증 필드 추출 프로그램**

신분증 이미지에서 글자를 검출·인식하고, JSON 템플릿에 정의한 규칙으로 성명, 등록번호, 주소, 발행일자 등의 필드를 추출하는 C++17 프로젝트입니다. 이미지 입력부터 전처리, 모델 추론, 문자 디코딩, 필드 추출·검증까지의 처리 과정을 구현했습니다.

이 저장소는 동일한 OCR 처리 목적을 가진 두 플랫폼 버전을 함께 관리합니다. **Windows 버전은 Visual Studio 2022와 MFC를 사용하는 GUI 프로그램**, **Linux 버전은 WSL Ubuntu 환경에서 Visual Studio Code를 사용하여 개발한 CLI 프로그램**입니다. 핵심 처리의 역할과 모델 및 템플릿 형식은 공통이며, 현재 소스와 빌드 구성은 플랫폼별 디렉터리에 나누어 관리합니다.

텍스트 검출과 인식에는 **PaddleOCR의 검출 모델과 인식 모델을 사용**했습니다. PaddleOCR 라이브러리를 프로그램에서 직접 호출하는 방식이 아니라, **모델만 가져와 ONNX 형식으로 변환한 뒤 C++에서 ONNX Runtime으로 추론**하는 구조입니다. 모델을 가급적 운영체제에 독립적으로 활용하고 Windows와 Linux에서 같은 모델 파일을 사용하기 위해 ONNX 형식을 선택했습니다. 실행 프로그램, OpenCV, ONNX Runtime 라이브러리는 각 운영체제에 맞게 별도로 빌드했습니다.

모델 출처: [PaddleOCR 공식 GitHub 저장소 (PaddlePaddle/PaddleOCR)](https://github.com/PaddlePaddle/PaddleOCR). 본 프로젝트는 해당 프로젝트의 검출·인식 모델을 ONNX로 변환하여 사용하며, PaddleOCR 라이브러리 의존성은 없습니다.

## 주요 기능

- **이미지 전처리**: 크기·채널 보정, 문서 윤곽 검출, 원근 보정, 배경무늬 억제
- **텍스트 검출·인식**: PaddleOCR의 검출·인식 모델을 ONNX로 변환하고 ONNX Runtime으로 실행
- **후처리·디코딩**: DB 후처리로 텍스트 영역 구성, 영역별 원근 변환, CTC 문자 디코딩
- **신분증 종류 판별**: OCR 제목 문자열과 템플릿 키워드 비교
- **필드 추출·검증**: 위치, 영역, 정규식, 참조 필드 조건을 적용하고 이름, 등록번호, 날짜 형식, 날짜 관계 검사
- **JSON 템플릿 확장**: 지원하는 규칙 범위에서 파서 코드 수정 없이 신분증별 필드 설정 추가·조정
- **플랫폼별 결과 확인**: Windows GUI의 검출 이미지·판독 결과 표시, Linux CLI의 신분증 필드·OCR 원문 터미널 출력 및 선택적 JSON·디버그 이미지 저장

기본 신분증 템플릿은 **주민등록증과 자동차운전면허증**을 대상으로 합니다. Linux CLI는 신분증 필드 추출을 기본 처리 과정에 포함하며, 필드 추출에 실패해도 OCR이 성공한 경우 인식 원문을 확인할 수 있습니다.

## 구현에서 중점을 둔 부분

| 구현 요소 | 설계 및 역할 |
| --- | --- |
| 처리 단계 분리 | 전처리, OCR 추론, 신분증 파싱, 사용자 인터페이스를 구분하여 관리 |
| 모델의 플랫폼 간 재사용 | PaddleOCR 모델을 ONNX로 변환하여 Windows·Linux에서 같은 모델 파일을 활용하고, 추론을 ONNX Runtime C++ API로 수행 |
| 모델 추론과 후처리 연결 | 검출 결과를 인식용 이미지로 변환하고 CTC 디코딩 결과를 좌표와 함께 필드 파서에 전달 |
| 규칙의 데이터화 | 신분증 종류별 추출 조건을 JSON에 정의하고 공통 필드 설정을 재사용 |
| 템플릿 검증 | 속성·정규식·필드 참조 관계를 검사하며, 전체 검증 성공 후 설정을 교체하고 실패 시 기존 설정 유지 |
| Linux 실행부 분리 | OCR 핵심 코드를 `imgocr_core` 정적 라이브러리로 구성하고 CLI 실행 파일에 연결 |
| 결과 활용 | Linux에서 필드와 OCR 원문을 터미널에 표시하고, 필요에 따라 구조화된 JSON과 OCR 영역 표시 이미지를 추가 저장 |

## 처리 흐름

```text
이미지 입력 (Windows GUI / Linux CLI)
    ↓
크기·채널 보정 → 문서 윤곽 검출·원근 보정 → 배경무늬 억제
    ↓
텍스트 검출 ONNX 추론 → DB 후처리 → 텍스트 영역 추출
    ↓
영역별 인식 ONNX 추론 → CTC 디코딩
    ↓
신분증 템플릿 선택 → 제외 영역 적용 → 필드 추출·검증
    ↓
Windows: GUI 결과 표시
Linux: 필드·OCR 원문을 텍스트로 출력 / 선택적 JSON·디버그 이미지 저장
```

Linux CLI는 기본 배경무늬 억제와 템플릿 기반 필드 추출을 항상 사용합니다.

## 저장소 구조

현재 프로젝트 폴더 이름을 유지한 저장소 루트 기준입니다. 아래는 주요 소스와 리소스 경로이며, 라이브러리와 빌드 결과의 준비 위치는 플랫폼별 안내를 참고합니다.

```text
.
├─ README.md
├─ test-image/                      # OCR 및 신분증 필드 추출 테스트 이미지
├─ ImgOcr-Windows/                  # Windows 프로젝트
│  ├─ ImgOcr.sln
│  ├─ library/                     # Windows용 OpenCV·ONNX Runtime SDK
│  ├─ x64/Release/                  # 실행용 DLL·설정·모델 및 빌드 출력 위치
│  └─ ImgOcr/                       # MFC 및 OCR 소스
│     ├─ ImgOcrDlg.cpp
│     ├─ ImgProc.cpp
│     ├─ ImgOcrEngine.cpp
│     ├─ IdCardParser.cpp
│     └─ IdCardTemplateJson.cpp
└─ ImgOcr-Ubuntu/                   # Linux 프로젝트
   ├─ CMakeLists.txt
   ├─ Makefile
   ├─ cmake/                       # ONNX Runtime 탐색
   ├─ cli/                         # CLI 진입점 및 설정 읽기
   ├─ src/                         # 전처리·OCR·필드 파서
   ├─ build/                       # 개발 환경의 실행 파일·설정·모델 포함
   └─ resources/                   # 설정·템플릿·모델·사전
```

빌드 후 테스트에 필요한 파일을 쉽게 사용할 수 있도록 Windows의 `x64/Release/`와 Linux의 `build/` 폴더를 함께 유지합니다. Linux의 `build/bin/imgocr`는 개발 환경에서 생성한 실행 파일이며, `build/share/imgocr/`에는 실행용 설정·템플릿·모델·사전이 들어 있습니다. 포함된 Linux 실행 파일을 사용하려면 호환되는 OpenCV·ONNX Runtime 공유 라이브러리와 시스템 런타임이 필요합니다.

## 테스트 이미지

저장소 루트의 `test-image/`에는 **OCR 인식과 신분증 필드 추출 동작을 확인하기 위한 테스트용 이미지**가 포함되어 있습니다. Windows GUI와 Linux CLI에서 같은 이미지를 입력하여 결과를 확인할 수 있습니다.

| 파일 | 테스트 대상 |
| --- | --- |
| `resident-registration-card.jpg` | 주민등록증 |
| `driver-license-card01.jpg` | 자동차운전면허증 |
| `driver-license-card02.jpg` | 자동차운전면허증의 다른 테스트 이미지 |

Windows에서는 **찾기** 버튼으로 `test-image/`의 이미지를 선택합니다. Linux에서는 아래 실행 예시처럼 Linux 프로젝트 폴더 기준 `../test-image/` 경로를 사용합니다.

## 기술 스택 및 개발 환경

| 구분 | Windows | Linux |
| --- | --- | --- |
| 실행 환경 | Windows x64 | WSL Ubuntu 24.04, x86_64 |
| 언어 | C++17 | C++17 |
| 개발·빌드 도구 | Visual Studio 2022, MSVC v143, Windows SDK 10.0 계열 | VS Code, GCC 13.3.0, CMake 3.28.3, GNU Make 4.3 |
| OCR 모델 | PaddleOCR 검출·인식 모델을 ONNX로 변환 | 동일한 ONNX 모델 사용 |
| 사용자 인터페이스 | MFC GUI | CLI |
| 이미지 처리 | OpenCV 4.10.0 | OpenCV 4.10.0 |
| 모델 실행 | ONNX Runtime 1.30.0 | ONNX Runtime 1.30.0 CPU |
| JSON 처리 | nlohmann/json | nlohmann/json 3.12.0, 단일 헤더 포함 |

Linux 버전은 VS Code에서 소스를 편집하고 WSL Ubuntu의 GCC·CMake·Make로 빌드하는 구성이며, CLI 형식으로 실행합니다. Linux 빌드 환경은 C++17 지원 컴파일러, CMake 3.20 이상, make로 구성되어 있습니다.

C++ 신분증 OCR 프로그램의 Windows 및 Ubuntu 실행 패키지입니다.

## 실행 파일 다운로드

- [ImgOcr-Windows-Exe.zip](https://github.com/jwseo08/ImgOcrPb/releases/download/v1.0.0/ImgOcr-Windows-Exe.zip): Windows x64 실행 파일, 모델 및 설정 파일
- [ImgOcr-Ubuntu-Bin.zip](https://github.com/jwseo08/ImgOcrPb/releases/download/v1.0.0/ImgOcr-Ubuntu-Bin.zip): Ubuntu x86_64 실행 파일, 모델 및 설정 파일

### 실행 환경
- Windows: 필요한 DLL과 Visual C++·MFC 런타임이 필요합니다.
- Ubuntu: 호환되는 OpenCV 및 ONNX Runtime 공유 라이브러리가 필요합니다.

압축 해제 시 폴더 구조를 유지하십시오.
사용 방법과 테스트 이미지 안내는 저장소의 README를 참고하십시오.

## Windows 빌드 및 실행

### 1. 개발 도구와 라이브러리 준비

Visual Studio 2022에 **C++ 데스크톱 개발**, **v143용 MFC**, **Windows SDK** 구성요소가 설치되어 있는지 확인합니다.

```text
ImgOcr-Windows/library/
├─ opencv-4.10.0-shared/
│  ├─ include/
│  └─ lib/
└─ onnxruntime-win-x64-1.30.0/
   ├─ include/
   └─ lib/
```

다른 SDK 위치를 사용하면 Visual Studio 프로젝트의 포함·라이브러리 경로를 변경합니다.

### 2. 빌드

`ImgOcr-Windows/ImgOcr.sln`을 열고 **Release | x64** 구성으로 빌드합니다.

### 3. 실행 리소스 준비

```text
ImgOcr-Windows/x64/Release/
├─ ImgOcr.exe
├─ onnxruntime.dll
├─ opencv_world4100.dll
├─ config.ini
├─ id_card_templates.json
└─ models/
   ├─ det.onnx
   ├─ rec.onnx
   └─ dict.yml
```

ONNX Runtime DLL은 라이브러리 폴더에서 복사되며 OpenCV DLL, 모델·사전·설정 파일은 빌드 결과 폴더인 x64/Release 폴더에 포함되어 있습니다.
실행 환경에 따라서 비주얼 스튜디오 재배포 가능 패키지 설치가 필요할 수 있습니다.

### 4. 사용

1. `ImgOcr.exe`를 실행합니다.
2. **찾기** 버튼으로 JPG, JPEG, BMP 또는 PNG 이미지를 선택합니다.
3. **판독** 버튼을 눌러 OCR 및 신분증 필드 추출을 실행합니다.
4. 검출 영역과 필드별 결과를 확인하고, 필요한 경우 OCR 원문·경고를 함께 확인합니다.

## Linux / WSL Ubuntu 빌드 및 실행

다음 명령은 저장소 루트에서 Ubuntu 터미널을 열었음을 기준으로 합니다.

### 1. 개발 도구와 SDK 준비

```bash
cd ImgOcr-Ubuntu
sudo apt update
sudo apt install build-essential cmake libopencv-dev
```

프로젝트의 인식 결과 검증에는 4.10.0을 사용했습니다.

ONNX Runtime은 **Linux C/C++ SDK의 헤더와 공유 라이브러리**가 필요합니다. 기존 검증 환경은 1.30.0 CPU SDK를 사용합니다.

### 2. 빌드

포함된 `build/` 폴더에는 개발 당시의 절대 경로가 기록된 CMake 캐시가 있습니다. 저장소를 다른 경로에 내려받거나 다른 컴퓨터에서 빌드할 때는 기존 폴더를 유지한 채 새 빌드 폴더를 사용합니다. SDK가 탐색 가능한 환경에서는 다음 명령을 실행합니다.

```bash
make BUILD_DIR=build-local JOBS=2
```

이 명령으로 만든 실행 파일은 `./build-local/bin/imgocr`입니다. 아래 실행 예시의 `./build/bin/imgocr`를 이 경로로 바꿔 실행합니다. 기존 개발 경로에서 기존 빌드 폴더를 재사용할 때는 `make JOBS=2`를 사용할 수 있습니다.

개발 시 기록된 SDK 위치는 `/usr/local/opencv/4.10.0`과 `/usr/local/onnxruntime/1.30.0`입니다. 다른 위치에 설치했다면 실제 경로로 지정합니다.

```bash
make BUILD_DIR=build-custom JOBS=2 \
  CMAKE_ARGS="-DOPENCV_ROOT=/path/to/opencv -DONNXRUNTIME_ROOT=/path/to/onnxruntime"
```

이 경우 실행 경로는 `./build-custom/bin/imgocr`입니다. SDK 변경 시에는 새 빌드 폴더를 사용하여 기존 CMake 탐색 캐시와의 충돌을 피합니다.

빌드 시 `resources/`의 설정, 템플릿, 모델과 사전을 선택한 빌드 폴더의 `share/imgocr/`로 복사합니다. 기본 빌드에는 `config.ini`, `id_card_templates.json`, `models/det.onnx`, `models/rec.onnx`, `models/dict.yml`이 모두 필요합니다.

### 3. 실행 예시

아래 명령은 `ImgOcr-Ubuntu/` 폴더에서 실행하며, 저장소에 포함된 테스트 이미지를 사용합니다. 포함된 실행 파일의 의존성이 준비된 환경에서는 그대로 실행하고, 새로 빌드했다면 실행 파일 경로를 해당 빌드 폴더로 변경합니다.

```bash
# 신분증 필드와 OCR 원문을 터미널에서 확인
./build/bin/imgocr --input ../test-image/resident-registration-card.jpg

# 터미널 출력을 유지하면서 JSON 결과를 추가 저장
./build/bin/imgocr --input ../test-image/resident-registration-card.jpg --output ./result.json

# JSON 결과와 OCR 영역 표시 이미지를 함께 저장
mkdir -p results
./build/bin/imgocr --input ../test-image/driver-license-card01.jpg \
  --output ./results/result.json \
  --save-debug-image ./results/detected.png
```

한 번에 이미지 한 장을 처리합니다. 출력 폴더는 미리 생성하며 기존 출력 파일은 덮어씁니다. CLI의 상대 경로는 현재 작업 폴더 기준입니다.

### CLI 옵션

| 옵션 | 설명 |
| --- | --- |
| `--input PATH` | 입력 이미지 지정, 필수 |
| `--output PATH` | JSON 결과를 파일로 추가 저장, 터미널 출력 유지 |
| `--save-debug-image PATH` | 전처리 이미지 위에 OCR 영역을 표시하여 저장 |

실행 옵션은 위 세 가지입니다. 인자를 생략하거나 잘못 입력하면 표준 오류에 사용법을 출력하고 종료 코드 `2`로 끝납니다. 별도의 도움말·버전 옵션, OCR만 수행하는 모드, 출력 형식 선택, 영역 표시 없는 전처리 이미지 저장 옵션은 제공하지 않습니다.

### 결과 형식과 종료 코드

터미널에는 사람이 읽기 쉬운 `[신분증 파싱 결과]`와 `[OCR 원문]`을 표시합니다. 필드 추출 오류·경고와 제외된 OCR 텍스트도 결과에 따라 출력합니다. JSON이나 디버그 이미지를 저장해도 터미널 출력은 유지됩니다.

JSON 결과의 주요 항목은 다음과 같습니다.

| 항목 | 내용 |
| --- | --- |
| `schema_version`, `input`, `success` | 출력 스키마 버전, 입력 경로, 전체 처리 성공 여부 |
| `image` | 전처리 이미지 크기와 좌표 기준 |
| `ocr.success`, `ocr.error`, `ocr.lines` | OCR 성공 여부, 오류, 텍스트·점수·영역 좌표 |
| `parsed.fields` | 추출 필드의 키·표시 이름·값 |
| `parsed.template_id`, `parsed.document_type` | 선택된 템플릿과 신분증 종류 |
| `parsed.errors`, `parsed.warnings`, `parsed.excluded_text_boxes` | 필드 추출 오류·경고 및 제외 텍스트 |

영역 좌표와 디버그 이미지는 **전처리된 이미지 기준**입니다. 결과는 UTF-8이며 진단 메시지와 ONNX Runtime 로그는 표준 오류로 출력합니다. OCR 성공 후 필드 추출에 실패한 경우에도 OCR 원문과 파싱 오류를 확인할 수 있습니다.

| 종료 코드 | 의미 |
| --- | --- |
| `0` | 처리 성공 |
| `1` | 설정·리소스·처리·출력 오류 |
| `2` | 잘못된 인자 또는 출력 경로 충돌 |
| `3` | OCR 성공, 신분증 필드 추출 실패 |

## 모델 및 설정

두 플랫폼은 다음과 같은 INI 형식을 사용합니다.

```ini
[MODEL_PATH]
DET=./models/det.onnx
REC=./models/rec.onnx
DIC=./models/dict.yml
TEMPLATE=./id_card_templates.json
```

INI 내부의 상대 경로는 **설정 파일이 있는 폴더 기준**입니다. Windows에서는 실행 파일 옆에 `config.ini`를 둡니다. Linux는 실행 파일 옆의 설정을 먼저 찾고, 없으면 실행 파일 기준 `../share/imgocr/config.ini`를 찾습니다. Linux의 설정 변경은 자동 탐색되는 기본 설정 파일을 편집하여 적용하며, 설정 경로를 지정하는 CLI 옵션은 제공하지 않습니다.

## 신분증 템플릿

`id_card_templates.json`에서 신분증별 제목 키워드, 필드 추출 조건 및 제외 영역을 관리합니다.

| 항목 | 역할 |
| --- | --- |
| `templates` | 신분증 종류별 키워드·필드·제외 영역 |
| `fieldPresets`, `use` | 공통 필드 정의 및 개별 설정 덮어쓰기 |
| `position` | `FromStart`, `FromEnd`, `Range`, `Candidates` 방식의 검색 |
| `selection` | 영역·기준 문구·정규식·후보 선택·참조 필드 관계 |
| `excludeRegions` | 도장 등 필드 추출에서 제외할 영역과 텍스트 조건 |

## 동작 범위와 한계

- 흐린 문자, 반사광, 그림자, 회전 및 배경무늬에 따라 인식 결과가 달라집니다.
- 문서 윤곽 검출에 실패하거나 이미지에서 카드가 차지하는 영역이 작은 경우 원근 보정이 생략될 수 있습니다.
- 필드 검증은 문자열 형식과 설정된 관계를 확인하는 기능이며 신분증 진위를 판별하지 않습니다.
- 인식 점수는 CTC 디코딩에서 선택된 문자들의 평균 출력 점수로, 실제 정확도를 보장하는 확률은 아닙니다. 종료 코드 `0`도 처리 성공을 뜻합니다.
- 텍스트 검출과 인식에서 CPU를 사용하여 작업을 처리합니다.
- 별도로 측정한 정확도·처리 속도 벤치마크는 제공하지 않습니다.

## 이미지 전처리 설계

OCR 전처리는 크기·채널 보정, 문서 윤곽 검출·원근 보정, 배경무늬 억제 순서로 수행합니다. 입력 행렬은 변경하지 않고, 결과는 8비트 3채널 BGR 이미지로 생성합니다.

### 문서 윤곽과 문자 획 보존

노이즈 제거 영상은 문서 윤곽 검출에만 사용합니다. 실제 OCR 입력은 원본 이미지의 크기·원근 보정 결과에서 생성하여 숫자의 점이나 가는 획이 노이즈 제거 과정에서 손상되는 것을 줄입니다.

문서 윤곽 후보의 최소 면적은 입력 이미지의 50%와 기존 최소 면적 설정 중 큰 값으로 정합니다. 카드 내부의 사진이나 도장을 문서 전체로 오인하여 잘라내는 것을 방지하기 위한 조건입니다. 적합한 윤곽을 찾지 못하면 원근 보정을 생략하고 크기 보정 결과를 사용합니다.

### 배경무늬 억제

밝은 보안 무늬의 대비가 커지는 것을 피하기 위해 OCR 입력에는 CLAHE나 평균 밝기를 0.5로 낮추는 자동 감마 보정을 적용하지 않습니다. 대신 회색 명암에 연속적인 LUT(밝기 변환표)를 적용합니다.

| 명암 구간 | 기본 처리 |
| --- | --- |
| 0~60 | 진한 문자 획 보존 |
| 60 초과~180 미만 | 중간 명암을 연속적으로 밝게 조정 |
| 180 이상 | 흰색 배경으로 변환 |

## Linux 빌드·실행 구성의 세부 사항

### 빌드 옵션과 리소스 관리

| 변수 | 용도 및 기본값 |
| --- | --- |
| `BUILD_DIR` | 빌드 폴더, 기본값 `build` |
| `JOBS` | 병렬 컴파일 작업 수, 기본값 `2` |
| `CMAKE_ARGS` | CMake에 전달할 추가 설정 |
| `PREFIX` | 설치 루트, 기본값 `/usr/local` |
| `OPENCV_ROOT` | CMake에서 사용할 OpenCV 설치 루트 |
| `ONNXRUNTIME_ROOT` | CMake에서 사용할 ONNX Runtime SDK 루트 |
| `IMGOCR_RESOURCES` | CMake에서 사용할 리소스 원본 폴더 |
