# The installed SDK's Config.cmake and .pc can contain stale lib64/include paths.
# Locate both files within the same prefix instead of importing that metadata.
if(ONNXRUNTIME_ROOT)
    find_path(OnnxRuntime_INCLUDE_DIR onnxruntime_cxx_api.h
        PATHS "${ONNXRUNTIME_ROOT}/include" "${ONNXRUNTIME_ROOT}/include/onnxruntime"
        NO_DEFAULT_PATH)
    find_library(OnnxRuntime_LIBRARY NAMES onnxruntime
        PATHS "${ONNXRUNTIME_ROOT}/lib" "${ONNXRUNTIME_ROOT}/lib64"
        NO_DEFAULT_PATH)
else()
    find_path(OnnxRuntime_INCLUDE_DIR onnxruntime_cxx_api.h
        PATH_SUFFIXES onnxruntime
        PATHS "/usr/local/onnxruntime/1.30.0/include")
    find_library(OnnxRuntime_LIBRARY NAMES onnxruntime
        PATHS "/usr/local/onnxruntime/1.30.0/lib" "/usr/local/onnxruntime/1.30.0/lib64")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OnnxRuntime
    REQUIRED_VARS OnnxRuntime_INCLUDE_DIR OnnxRuntime_LIBRARY
    REASON_FAILURE_MESSAGE "Set ONNXRUNTIME_ROOT to a Linux C/C++ SDK containing matching headers and libraries.")

if(OnnxRuntime_FOUND AND NOT TARGET OnnxRuntime::OnnxRuntime)
    add_library(OnnxRuntime::OnnxRuntime SHARED IMPORTED)
    set_target_properties(OnnxRuntime::OnnxRuntime PROPERTIES
        IMPORTED_LOCATION "${OnnxRuntime_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OnnxRuntime_INCLUDE_DIR}")
endif()
mark_as_advanced(OnnxRuntime_INCLUDE_DIR OnnxRuntime_LIBRARY)
