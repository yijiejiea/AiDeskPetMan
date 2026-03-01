cmake_minimum_required(VERSION 3.28)

project(MikuDesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
  add_compile_options(/Zi /W4 /permissive- /EHsc)
  add_link_options(/DEBUG /OPT:REF /OPT:ICF)
endif()

option(MIKUDESK_ENABLE_LIVE2D "Enable Live2D integration" ON)
option(MIKUDESK_ENABLE_LLAMA "Enable local llama.cpp integration" OFF)
option(MIKUDESK_ENABLE_TTS "Enable TTS integration" OFF)
set(MIKUDESK_CUBISM_SDK_PATH
    "${CMAKE_SOURCE_DIR}/third_party/CubismSdkForNative"
    CACHE PATH "Path to Cubism SDK for Native root directory")

include(CTest)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR CMAKE_TOOLCHAIN_FILE STREQUAL "")
  message(STATUS "MikuDesk is using manual dependency mode. Set CMAKE_PREFIX_PATH to locate packages.")
endif()

find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

set(mikudesk_core_sources
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
  src/ui/settings_window.cpp
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

add_library(mikudesk_core
  ${mikudesk_core_sources}
  ${cubism_framework_sources}
)

set_target_properties(mikudesk_core PROPERTIES
  AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/window;${CMAKE_CURRENT_SOURCE_DIR}/ui/settings"
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
    Qt6::Widgets
  PRIVATE
    dbghelp
    crypt32
    pdh
    psapi
    ${cubism_core_library}
)

if(MSVC)
  target_compile_definitions(mikudesk_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MIKUDESK_ENABLE_LIVE2D)
  target_compile_definitions(mikudesk_core PRIVATE CSM_TARGET_WIN_GL)
  target_link_libraries(mikudesk_core PRIVATE opengl32)
  target_compile_definitions(mikudesk_core PUBLIC MIKUDESK_ENABLE_LIVE2D=1)
else()
  target_compile_definitions(mikudesk_core PUBLIC MIKUDESK_ENABLE_LIVE2D=0)
endif()

add_executable(mikudesk src/main.cpp)
target_link_libraries(mikudesk PRIVATE mikudesk_core)

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
add_test(NAME config_store_test COMMAND config_store_test)

add_executable(dpapi_secret_store_test tests/dpapi_secret_store_test.cpp)
target_link_libraries(dpapi_secret_store_test PRIVATE mikudesk_core)
add_test(NAME dpapi_secret_store_test COMMAND dpapi_secret_store_test)

add_executable(startup_smoke_test tests/startup_smoke_test.cpp)
target_link_libraries(startup_smoke_test PRIVATE mikudesk_core)
add_test(NAME startup_smoke_test COMMAND startup_smoke_test)

add_executable(crash_dump_integration_test tests/crash_dump_integration_test.cpp)
target_link_libraries(crash_dump_integration_test PRIVATE mikudesk_core)
add_test(NAME crash_dump_integration_test COMMAND crash_dump_integration_test)
set_tests_properties(crash_dump_integration_test PROPERTIES LABELS integration)

add_executable(performance_monitor_smoke_test tests/performance_monitor_smoke_test.cpp)
target_link_libraries(performance_monitor_smoke_test PRIVATE mikudesk_core)
add_test(NAME performance_monitor_smoke_test COMMAND performance_monitor_smoke_test)
