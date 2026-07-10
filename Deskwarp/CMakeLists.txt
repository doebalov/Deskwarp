cmake_minimum_required(VERSION 3.16)
project(Deskwarp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

set(APP_ICON_RESOURCE "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icon.ico")
    set(APP_ICON_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_ICON_RESOURCE}" "IDI_ICON1 ICON DISCARDABLE \"${CMAKE_CURRENT_SOURCE_DIR}/icon.ico\"")
endif()

add_executable(Deskwarp WIN32 
    app.cpp
    ${APP_ICON_RESOURCE}
)

target_link_options(Deskwarp PRIVATE "-static-libgcc" "-static-libstdc++")

target_link_libraries(Deskwarp PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Widgets 
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(Deskwarp PRIVATE
        d3d11
        dxgi
        dcomp
        d3dcompiler
        dwmapi
        ole32
        shell32
        user32
        gdi32
    )

    find_program(WINDEPLOYQT_EXECUTABLE windeployqt6 HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Deskwarp POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                "$<TARGET_FILE:Deskwarp>"
                --no-translations
            COMMENT "Deploying Qt runtime for Deskwarp.exe"
        )
    endif()
endif()