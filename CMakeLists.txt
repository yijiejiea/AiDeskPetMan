cmake_minimum_required(VERSION 3.28)

project(MikuDesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Disable C++20 module scanning to avoid Ninja dyndep crashes on large third-party trees.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
  set(_mikudesk_msvc_compile_options /Zi /W4 /permissive- /EHsc)
  set(_mikudesk_msvc_link_options /DEBUG /OPT:REF /OPT:ICF)
endif()

set(_mikudesk_default_llama_path "${CMAKE_SOURCE_DIR}/third_party/llama.cpp")
if(NOT EXISTS "${_mikudesk_default_llama_path}/CMakeLists.txt" AND
   EXISTS "${CMAKE_SOURCE_DIR}/third_party/llama/CMakeLists.txt")
  set(_mikudesk_default_llama_path "${CMAKE_SOURCE_DIR}/third_party/llama")
endif()
set(_mikudesk_default_llama_enabled OFF)
if(EXISTS "${_mikudesk_default_llama_path}/CMakeLists.txt")
  set(_mikudesk_default_llama_enabled ON)
endif()

option(MIKUDESK_ENABLE_LIVE2D "Enable Live2D integration" ON)
option(MIKUDESK_ENABLE_LLAMA "Enable local llama.cpp integration"
       ${_mikudesk_default_llama_enabled})
option(MIKUDESK_ENABLE_LLAMA_CUDA "Enable llama.cpp CUDA backend when CUDA toolkit is available"
       ON)
option(MIKUDESK_ENABLE_TTS "Enable TTS integration" OFF)
set(MIKUDESK_CUBISM_SDK_PATH
    "${CMAKE_SOURCE_DIR}/third_party/CubismSdkForNative"
    CACHE PATH "Path to Cubism SDK for Native root directory")
set(MIKUDESK_LLAMA_CPP_PATH
    "${_mikudesk_default_llama_path}"
    CACHE PATH "Path to llama.cpp root directory")

include(CTest)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR CMAKE_TOOLCHAIN_FILE STREQUAL "")
  message(STATUS "MikuDesk is using manual dependency mode. Set CMAKE_PREFIX_PATH to locate packages.")
endif()

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network)

set(mikudesk_core_sources
  src/ai/chat_context.cpp
  src/ai/chat_service.cpp
  src/ai/cloud_engine.cpp
  src/ai/local_engine.cpp
  src/ai/sse_parser.cpp
  src/app/application.cpp
  src/app/config.cpp
  src/core/scheduler.cpp
  src/diagnostics/logger.cpp
  src/diagnostics/crash_handler.cpp
  src/diagnostics/performance_monitor.cpp
  src/renderer/cubism_skin_loader.cpp
  src/renderer/live2d_canvas.cpp
  src/renderer/live2d_renderer.cpp
  src/renderer/skin_loader.cpp
  src/resource/config_store.cpp
  src/security/dpapi_secret_store.cpp
  src/ui/chat_bubble.cpp
  src/ui/chat_input_dialog.cpp
  src/ui/settings_window.cpp
  ui/chat/chat_input_dialog.ui
  src/window/desk_pet_window.cpp
  ui/settings/settings_window.ui
  ui/window/desk_pet_window.ui
  src/window/window_manager.cpp
)

set(cubism_framework_sources)
set(cubism_core_library)
if(MIKUDESK_ENABLE_LIVE2D)
  set(cubism_core_header "${MIKUDESK_CUBISM_SDK_PATH}/Core/include/Live2DCubismCore.h")
  set(cubism_framework_source_dir "${MIKUDESK_CUBISM_SDK_PATH}/Framework/src")
  set(cubism_framework_entry "${cubism_framework_source_dir}/CubismFramework.cpp")

  if(NOT EXISTS "${cubism_core_header}")
    message(FATAL_ERROR
      "MIKUDESK_ENABLE_LIVE2D=ON but Cubism Core header is missing: ${cubism_core_header}")
  endif()
  if(NOT EXISTS "${cubism_framework_entry}")
    message(FATAL_ERROR
      "MIKUDESK_ENABLE_LIVE2D=ON but Cubism Framework source is missing: ${cubism_framework_entry}")
  endif()

  file(GLOB_RECURSE cubism_framework_sources CONFIGURE_DEPENDS
    "${cubism_framework_source_dir}/*.cpp")
  # Build only the Cubism OpenGL backend for Qt OpenGL rendering.
  list(FILTER cubism_framework_sources EXCLUDE REGEX "/Rendering/D3D9/")
  list(FILTER cubism_framework_sources EXCLUDE REGEX "/Rendering/D3D11/")
  list(FILTER cubism_framework_sources EXCLUDE REGEX "/Rendering/Vulkan/")
  list(LENGTH cubism_framework_sources cubism_framework_source_count)
  if(cubism_framework_source_count EQUAL 0)
    message(FATAL_ERROR
      "MIKUDESK_ENABLE_LIVE2D=ON but no Cubism Framework source files were found under: ${cubism_framework_source_dir}")
  endif()

  file(GLOB cubism_core_library_candidates
    "${MIKUDESK_CUBISM_SDK_PATH}/Core/lib/windows/x86_64/*.lib"
    "${MIKUDESK_CUBISM_SDK_PATH}/Core/lib/windows/x86_64/*/*.lib"
    "${MIKUDESK_CUBISM_SDK_PATH}/Core/lib/windows/x86_64/*/*/*.lib")
  list(LENGTH cubism_core_library_candidates cubism_core_library_count)
  if(cubism_core_library_count EQUAL 0)
    message(FATAL_ERROR
      "MIKUDESK_ENABLE_LIVE2D=ON but no Cubism Core x64 library was found under: ${MIKUDESK_CUBISM_SDK_PATH}/Core/lib/windows/x86_64")
  endif()
  list(GET cubism_core_library_candidates 0 cubism_core_library)
  message(STATUS "Using Cubism Core library: ${cubism_core_library}")
endif()

if(MIKUDESK_ENABLE_LLAMA)
  if(NOT EXISTS "${MIKUDESK_LLAMA_CPP_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
      "MIKUDESK_ENABLE_LLAMA=ON but llama.cpp source is missing: ${MIKUDESK_LLAMA_CPP_PATH}")
  endif()

  if(MIKUDESK_ENABLE_LLAMA_CUDA)
    find_program(_mikudesk_nvcc NAMES nvcc nvcc.exe HINTS ENV CUDA_PATH PATH_SUFFIXES bin)
    if(_mikudesk_nvcc)
      if(MSVC)
        if(NOT CMAKE_CUDA_FLAGS MATCHES "(^| )-allow-unsupported-compiler( |$)")
          set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -allow-unsupported-compiler"
              CACHE STRING "Global CUDA compiler flags" FORCE)
        endif()
      endif()
      set(GGML_CUDA ON CACHE BOOL "ggml: use CUDA" FORCE)
      message(STATUS "Enabled llama.cpp CUDA backend (nvcc: ${_mikudesk_nvcc})")
    else()
      set(GGML_CUDA OFF CACHE BOOL "ggml: use CUDA" FORCE)
      message(WARNING
        "MIKUDESK_ENABLE_LLAMA_CUDA=ON but nvcc was not found. Falling back to CPU backend.")
    endif()
  else()
    set(GGML_CUDA OFF CACHE BOOL "ggml: use CUDA" FORCE)
  endif()

  # Keep llama.cpp static in this workspace to avoid runtime DLL lookup issues.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  add_subdirectory("${MIKUDESK_LLAMA_CPP_PATH}" "${CMAKE_BINARY_DIR}/llama.cpp")

  if(MSVC)
    foreach(_llama_target IN ITEMS llama llama_shared)
      if(TARGET ${_llama_target})
        # Some llama.cpp revisions use UTF-8 literals that break with MSVC char8_t mode.
        target_compile_options(${_llama_target} PRIVATE /Zc:char8_t-)
      endif()
    endforeach()
  endif()
endif()

add_library(mikudesk_core STATIC
  ${mikudesk_core_sources}
  ${cubism_framework_sources}
)

set_target_properties(mikudesk_core PROPERTIES
  AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/window;${CMAKE_CURRENT_SOURCE_DIR}/ui/settings;${CMAKE_CURRENT_SOURCE_DIR}/ui/chat"
)

target_include_directories(mikudesk_core PUBLIC src)
if(MIKUDESK_ENABLE_LIVE2D)
  target_include_directories(mikudesk_core PRIVATE
    "${MIKUDESK_CUBISM_SDK_PATH}/Core/include"
    "${MIKUDESK_CUBISM_SDK_PATH}/Framework/src"
  )
endif()

target_link_libraries(mikudesk_core
  PUBLIC
    fmt::fmt
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
    Qt6::Widgets
  PRIVATE
    dbghelp
    crypt32
    pdh
    psapi
    ${cubism_core_library}
)

if(MSVC)
  target_compile_options(mikudesk_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_compile_definitions(mikudesk_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MIKUDESK_ENABLE_LIVE2D)
  target_compile_definitions(mikudesk_core PRIVATE CSM_TARGET_WIN_GL)
  target_link_libraries(mikudesk_core PRIVATE opengl32)
  target_compile_definitions(mikudesk_core PUBLIC MIKUDESK_ENABLE_LIVE2D=1)
else()
  target_compile_definitions(mikudesk_core PUBLIC MIKUDESK_ENABLE_LIVE2D=0)
endif()

if(MIKUDESK_ENABLE_LLAMA)
  if(TARGET llama)
    target_link_libraries(mikudesk_core PRIVATE llama)
  elseif(TARGET llama_shared)
    target_link_libraries(mikudesk_core PRIVATE llama_shared)
  else()
    message(FATAL_ERROR "MIKUDESK_ENABLE_LLAMA=ON but no llama target was exported")
  endif()
  target_compile_definitions(mikudesk_core PUBLIC MIKUDESK_ENABLE_LLAMA=1)
else()
  target_compile_definitions(mikudesk_core PUBLIC MIKUDESK_ENABLE_LLAMA=0)
endif()

add_executable(mikudesk src/main.cpp)
target_link_libraries(mikudesk PRIVATE mikudesk_core)
if(MSVC)
  target_compile_options(mikudesk PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_link_options(mikudesk PRIVATE ${_mikudesk_msvc_link_options})
endif()

set(mikudesk_assets_source_dir "${CMAKE_SOURCE_DIR}/assets")
if(EXISTS "${mikudesk_assets_source_dir}")
  add_custom_command(
    TARGET mikudesk
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mikudesk>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${mikudesk_assets_source_dir}"
            "$<TARGET_FILE_DIR:mikudesk>/assets"
    COMMENT "Copying assets to runtime directory"
  )
else()
  message(WARNING "Assets directory not found: ${mikudesk_assets_source_dir}")
endif()

if(MIKUDESK_ENABLE_LIVE2D)
  set(cubism_shader_source_dir
      "${MIKUDESK_CUBISM_SDK_PATH}/Framework/src/Rendering/OpenGL/Shaders/Standard")
  if(EXISTS "${cubism_shader_source_dir}")
    add_custom_command(
      TARGET mikudesk
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mikudesk>/FrameworkShaders"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${cubism_shader_source_dir}"
              "$<TARGET_FILE_DIR:mikudesk>/FrameworkShaders"
      COMMENT "Copying Cubism OpenGL shaders to runtime directory"
    )
  else()
    message(WARNING "Cubism shader directory not found: ${cubism_shader_source_dir}")
  endif()
endif()

add_executable(config_store_test tests/config_store_test.cpp)
target_link_libraries(config_store_test PRIVATE mikudesk_core)
if(MSVC)
  target_compile_options(config_store_test PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_link_options(config_store_test PRIVATE ${_mikudesk_msvc_link_options})
endif()
add_test(NAME config_store_test COMMAND config_store_test)

add_executable(dpapi_secret_store_test tests/dpapi_secret_store_test.cpp)
target_link_libraries(dpapi_secret_store_test PRIVATE mikudesk_core)
if(MSVC)
  target_compile_options(dpapi_secret_store_test PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_link_options(dpapi_secret_store_test PRIVATE ${_mikudesk_msvc_link_options})
endif()
add_test(NAME dpapi_secret_store_test COMMAND dpapi_secret_store_test)

add_executable(startup_smoke_test tests/startup_smoke_test.cpp)
target_link_libraries(startup_smoke_test PRIVATE mikudesk_core)
if(MSVC)
  target_compile_options(startup_smoke_test PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_link_options(startup_smoke_test PRIVATE ${_mikudesk_msvc_link_options})
endif()
add_test(NAME startup_smoke_test COMMAND startup_smoke_test)

add_executable(crash_dump_integration_test tests/crash_dump_integration_test.cpp)
target_link_libraries(crash_dump_integration_test PRIVATE mikudesk_core)
if(MSVC)
  target_compile_options(crash_dump_integration_test PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_link_options(crash_dump_integration_test PRIVATE ${_mikudesk_msvc_link_options})
endif()
add_test(NAME crash_dump_integration_test COMMAND crash_dump_integration_test)
set_tests_properties(crash_dump_integration_test PROPERTIES LABELS integration)

add_executable(performance_monitor_smoke_test tests/performance_monitor_smoke_test.cpp)
target_link_libraries(performance_monitor_smoke_test PRIVATE mikudesk_core)
if(MSVC)
  target_compile_options(performance_monitor_smoke_test PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_link_options(performance_monitor_smoke_test PRIVATE ${_mikudesk_msvc_link_options})
endif()
add_test(NAME performance_monitor_smoke_test COMMAND performance_monitor_smoke_test)

add_executable(sse_parser_test tests/sse_parser_test.cpp)
target_link_libraries(sse_parser_test PRIVATE mikudesk_core)
if(MSVC)
  target_compile_options(sse_parser_test PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_link_options(sse_parser_test PRIVATE ${_mikudesk_msvc_link_options})
endif()
add_test(NAME sse_parser_test COMMAND sse_parser_test)

add_executable(chat_context_test tests/chat_context_test.cpp)
target_link_libraries(chat_context_test PRIVATE mikudesk_core)
if(MSVC)
  target_compile_options(chat_context_test PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${_mikudesk_msvc_compile_options}>)
  target_link_options(chat_context_test PRIVATE ${_mikudesk_msvc_link_options})
endif()
add_test(NAME chat_context_test COMMAND chat_context_test)
