cmake_minimum_required(VERSION 3.20)
project(ImgOcr VERSION 1.0.0 LANGUAGES CXX)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This project targets Linux.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(OPENCV_ROOT "" CACHE PATH "Optional OpenCV installation prefix")
set(ONNXRUNTIME_ROOT "" CACHE PATH "Optional ONNX Runtime installation prefix")
if(OPENCV_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${OPENCV_ROOT}")
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(OpenCV 4.6 QUIET COMPONENTS core imgproc imgcodecs)
if(NOT OpenCV_FOUND AND NOT OPENCV_ROOT AND EXISTS "/usr/local/opencv/4.10.0/lib/cmake/opencv4")
    # Compatibility with the existing WSL installation; standard installs win.
    set(OpenCV_DIR "/usr/local/opencv/4.10.0/lib/cmake/opencv4")
endif()
find_package(OpenCV 4.6 REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(OnnxRuntime REQUIRED)
find_package(Threads REQUIRED)

# Report mismatched SDK headers/library during configuration, before C++ wrappers
# initialize a null API pointer at application startup.
if(NOT CMAKE_CROSSCOMPILING)
    include(CheckCXXSourceRuns)
    get_filename_component(_ort_library_dir "${OnnxRuntime_LIBRARY}" DIRECTORY)
    set(CMAKE_REQUIRED_INCLUDES "${OnnxRuntime_INCLUDE_DIR}")
    set(CMAKE_REQUIRED_LIBRARIES "${OnnxRuntime_LIBRARY}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-Wl,-rpath,${_ort_library_dir}")
    unset(IMGOCR_ONNX_API_MATCHES CACHE)
    check_cxx_source_runs("#include <onnxruntime_c_api.h>
        int main() { return OrtGetApiBase()->GetApi(ORT_API_VERSION) ? 0 : 1; }"
        IMGOCR_ONNX_API_MATCHES)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(NOT IMGOCR_ONNX_API_MATCHES)
        message(FATAL_ERROR "ONNX Runtime cannot load or its headers/library are incompatible. Use one matching Linux SDK via ONNXRUNTIME_ROOT.")
    endif()
endif()

add_library(imgocr_core STATIC
    src/CommonUtil.cpp
    src/ImgProc.cpp
    src/ImgOcrEngine.cpp
    src/IdCardParser.cpp
    src/IdCardTemplateJson.cpp)
target_include_directories(imgocr_core PUBLIC src)
target_include_directories(imgocr_core SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
target_link_libraries(imgocr_core PUBLIC ${OpenCV_LIBS} OnnxRuntime::OnnxRuntime Threads::Threads)
target_compile_options(imgocr_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(imgocr cli/main.cpp cli/OcrConfig.cpp)
target_link_libraries(imgocr PRIVATE imgocr_core)
target_compile_options(imgocr PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(imgocr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH_USE_LINK_PATH TRUE)

# Stage runtime resources independently of the source tree.
set(IMGOCR_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources" CACHE PATH "Runtime model/configuration directory")
foreach(resource config.ini id_card_templates.json models/det.onnx models/rec.onnx models/dict.yml)
    if(NOT EXISTS "${IMGOCR_RESOURCES}/${resource}")
        message(FATAL_ERROR "Missing runtime resource: ${IMGOCR_RESOURCES}/${resource}. Download the complete source package including resources/models, or set IMGOCR_RESOURCES.")
    endif()
    configure_file("${IMGOCR_RESOURCES}/${resource}"
                   "${CMAKE_BINARY_DIR}/share/imgocr/${resource}" COPYONLY)
endforeach()

include(GNUInstallDirs)
install(TARGETS imgocr RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${IMGOCR_RESOURCES}/config.ini" "${IMGOCR_RESOURCES}/id_card_templates.json"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/imgocr)
install(DIRECTORY "${IMGOCR_RESOURCES}/models" DESTINATION ${CMAKE_INSTALL_DATADIR}/imgocr)
install(FILES src/third_party/nlohmann/LICENSE.MIT
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party/nlohmann)
