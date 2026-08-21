cmake_minimum_required(VERSION 3.21)
project(password_manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libsodium is a required cryptographic dependency provided by vcpkg.
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(Threads REQUIRED)

include(CTest)

# FTXUI remains fetched as the terminal UI dependency.
include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
    GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

add_library(vaultcli_lib
    src/core/folder.cpp
    src/core/vault.cpp
    src/crypto/crypto.cpp
    src/cli/cli.cpp
    src/app/app.cpp
    src/storage/serializer.cpp
    src/storage/storage.cpp
    src/tui/clipboard.cpp
    src/tui/tui.cpp
)

target_link_libraries(vaultcli_lib PUBLIC
    unofficial-sodium::sodium
    Threads::Threads
    ftxui::screen
    ftxui::dom
    ftxui::component
)

if(WIN32)
    target_link_libraries(vaultcli_lib PRIVATE user32)
endif()

target_include_directories(vaultcli_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(password_manager main.cpp)
target_link_libraries(password_manager PRIVATE vaultcli_lib)

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)

    add_executable(vaultcli_tests
        tests/unit/crypto_tests.cpp
        tests/unit/domain_tests.cpp
        tests/unit/serializer_tests.cpp
        tests/unit/cli_tests.cpp
        tests/unit/clipboard_tests.cpp
        tests/unit/tui_tests.cpp
    )
    target_link_libraries(vaultcli_tests PRIVATE
        vaultcli_lib
        Catch2::Catch2WithMain
    )
    target_compile_definitions(vaultcli_tests PRIVATE VAULTCLI_TESTING)

    add_executable(vaultcli_integration_tests
        tests/unit/app_tests.cpp
        tests/unit/storage_tests.cpp
    )
    target_link_libraries(vaultcli_integration_tests PRIVATE
        vaultcli_lib
        Catch2::Catch2WithMain
    )

    include(Catch)
    catch_discover_tests(vaultcli_tests)
    catch_discover_tests(vaultcli_integration_tests)
endif()
