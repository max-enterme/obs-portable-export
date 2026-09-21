cmake_minimum_required(VERSION 3.24)
project(portable_core LANGUAGES CXX C)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

option(PORTABLE_CORE_TESTS "Build portable_core tests" OFF)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  miniz
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
)
FetchContent_MakeAvailable(miniz)

add_library(miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR})
set_target_properties(miniz PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

add_library(portable_core STATIC
  src/convert.cpp
  src/export.cpp
  src/zip.cpp
)
target_include_directories(portable_core PUBLIC include)
target_compile_features(portable_core PUBLIC cxx_std_17)
target_link_libraries(portable_core PUBLIC nlohmann_json::nlohmann_json miniz)

if(MSVC)
  target_compile_options(portable_core PUBLIC /utf-8)
endif()

if(PORTABLE_CORE_TESTS)
  FetchContent_Declare(
    doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
  )
  FetchContent_MakeAvailable(doctest)

  enable_testing()

  add_executable(portable_core_tests
    tests/test_main.cpp
    tests/test_convert.cpp
    tests/test_export.cpp
    tests/test_zip.cpp
  )
  target_link_libraries(portable_core_tests PRIVATE portable_core doctest::doctest)

  if(MSVC)
    target_compile_options(portable_core_tests PRIVATE /utf-8)
  endif()

  list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
  include(doctest)
  doctest_discover_tests(portable_core_tests)

  # doctest_discover_tests はテスト名を --test-case=<名前> として argv で渡す。
  # MSVC のナロー main は GetCommandLineA() 経由で argv を作るため、
  # ACP(GitHub の windows-2022 ランナーは CP1252)で表現できない日本語名は
  # 1 文字ずつ '?' に落ち、どの TEST_CASE にも一致しなくなる。
  # doctest は失敗 0 件なら exit 0 を返すので、1 件も実行されないまま緑になる。
  # フィルタを一切渡さずに全件を 1 回走らせるエントリを別に登録して、その穴を塞ぐ。
  add_test(NAME "portable_core_tests: 全件(フィルタ無し)" COMMAND portable_core_tests)
endif()
