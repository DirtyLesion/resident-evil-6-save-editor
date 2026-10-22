cmake_minimum_required(VERSION 3.20)
project(RE6SaveEditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Qt6 6.3 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

add_executable(RE6SaveEditor
    src/main_linux.cpp
    src/save_linux.cpp
    src/fields.cpp
)
target_include_directories(RE6SaveEditor PRIVATE src)
target_link_libraries(RE6SaveEditor PRIVATE Qt6::Widgets)
target_compile_options(RE6SaveEditor PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(RE6SaveFileTests
        tests/save_linux_test.cpp
        src/save_linux.cpp
        src/fields.cpp
    )
    target_include_directories(RE6SaveFileTests PRIVATE src)
    target_compile_options(RE6SaveFileTests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME linux-save-roundtrip COMMAND RE6SaveFileTests)
endif()

install(TARGETS RE6SaveEditor RUNTIME DESTINATION bin)
install(FILES packaging/re6-save-editor.desktop DESTINATION share/applications)
install(FILES packaging/re6-save-editor.svg DESTINATION share/icons/hicolor/scalable/apps)
