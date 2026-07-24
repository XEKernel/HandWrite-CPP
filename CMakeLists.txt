cmake_minimum_required(VERSION 3.16)
project(HandWrite VERSION 2.5.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Platform-specific settings
if(WIN32)
    # Static linking on Windows/MinGW to avoid runtime library issues
    set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
    set(CMAKE_SHARED_LINKER_FLAGS "-static-libgcc -static-libstdc++")
endif()

# Set AUTOUIC search paths
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent PrintSupport Svg)

# WebP (optional CMake config, fallback to bare link)
find_package(WebP QUIET)

# Include directories - target-local
set(INC_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Source files
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/tools.cpp
    src/core.cpp
    src/mainwindow.cpp
)

# Header files
set(HEADERS
    src/config.hpp
    src/tools.hpp
    src/core.hpp
    src/mainwindow.hpp
)

# UI files
set(UI_FILES
    ui/mainwindow.ui
)

# Resource files (if any)
set(RESOURCE_FILES
)

# Platform-specific resources
if(WIN32)
    # Windows resource file for application icon
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/app.rc
        @ONLY
    )
    set(PLATFORM_RESOURCE
        ${CMAKE_CURRENT_BINARY_DIR}/app.rc
    )
elseif(APPLE)
    # macOS icon resource
    set(ICON_RESOURCE ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.icns)
    if(EXISTS ${ICON_RESOURCE})
        set(PLATFORM_RESOURCE ${ICON_RESOURCE})
    else()
        set(PLATFORM_RESOURCE)
    endif()
else()
    set(PLATFORM_RESOURCE)
endif()

# Create executable
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${RESOURCE_FILES}
    ${PLATFORM_RESOURCE}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${INC_DIRS})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::PrintSupport
    Qt6::Svg
)
if(WebP_FOUND)
    target_link_libraries(${PROJECT_NAME} WebP::webp)
else()
    target_link_libraries(${PROJECT_NAME} webp)
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE HANDWRITE_VERSION="${PROJECT_VERSION}")

# Platform-specific executable properties
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
elseif(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.handwrite.generator"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_NAME "HandWrite"
    )
    if(EXISTS ${ICON_RESOURCE})
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE app.icns
        )
        set_source_files_properties(${ICON_RESOURCE} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()
endif()

# Copy ttf_library to build directory (if exists)
if(EXISTS "${CMAKE_SOURCE_DIR}/ttf_library")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/ttf_library"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/ttf_library"
    )
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../ttf_library")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/../ttf_library"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/ttf_library"
    )
endif()

# Install rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

# Install ttf_library
if(EXISTS "${CMAKE_SOURCE_DIR}/ttf_library")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/ttf_library"
        DESTINATION bin
    )
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../ttf_library")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/../ttf_library"
        DESTINATION bin
    )
endif()

# CLI executable
set(CLI_SOURCES
    src/cli.cpp
    src/config.cpp
    src/tools.cpp
    src/core.cpp
)
add_executable(handwrite-cli ${CLI_SOURCES})
target_include_directories(handwrite-cli PRIVATE ${INC_DIRS})
target_link_libraries(handwrite-cli
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
    Qt6::PrintSupport
    Qt6::Svg
)
if(WebP_FOUND)
    target_link_libraries(handwrite-cli WebP::webp)
else()
    target_link_libraries(handwrite-cli webp)
endif()
target_compile_definitions(handwrite-cli PRIVATE HANDWRITE_VERSION="${PROJECT_VERSION}")
if(WIN32)
    set_target_properties(handwrite-cli PROPERTIES WIN32_EXECUTABLE FALSE)
endif()