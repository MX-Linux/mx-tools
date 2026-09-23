# **********************************************************************
# * Copyright (C) 2017-2026 MX Authors
# *
# * Authors: Adrian
# *          MX Linux <http://mxlinux.org>
# *
# * This file is part of mx-tools.
# *
# * mx-tools is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * mx-tools is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with mx-tools.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VERSION_FROM_CHANGELOG "")

if(DEFINED PROJECT_VERSION_OVERRIDE AND NOT "${PROJECT_VERSION_OVERRIDE}" STREQUAL "")
    set(PROJECT_VERSION_FROM_CHANGELOG "${PROJECT_VERSION_OVERRIDE}")
    message(STATUS "Using version override: ${PROJECT_VERSION_FROM_CHANGELOG}")
else()
    # Try dpkg-parsechangelog first (Debian/Ubuntu)
    execute_process(
        COMMAND dpkg-parsechangelog -SVersion
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_VERSION_FROM_CHANGELOG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE DPKG_RESULT
        ERROR_QUIET
    )

    if(DPKG_RESULT EQUAL 0 AND NOT "${PROJECT_VERSION_FROM_CHANGELOG}" STREQUAL "")
        message(STATUS "Using version from debian/changelog: ${PROJECT_VERSION_FROM_CHANGELOG}")
    elseif(EXISTS ${CMAKE_SOURCE_DIR}/debian/changelog)
        file(READ ${CMAKE_SOURCE_DIR}/debian/changelog PROJECT_CHANGELOG_CONTENT)
        string(REGEX MATCH "^[^\\(]*\\(([^)]+)\\)" _match "${PROJECT_CHANGELOG_CONTENT}")
        if(NOT _match)
            message(FATAL_ERROR "Could not parse version from debian/changelog")
        endif()
        set(PROJECT_VERSION_FROM_CHANGELOG "${CMAKE_MATCH_1}")
        message(STATUS "Using version parsed from debian/changelog: ${PROJECT_VERSION_FROM_CHANGELOG}")
    else()
        message(FATAL_ERROR "Could not determine version: dpkg-parsechangelog not available and debian/changelog missing")
    endif()
endif()

# CMake's project() only accepts a dot-separated numeric version, but
# debian/changelog may carry a non-numeric distro suffix (e.g. "26.09.5mx25"
# for MX-25/Trixie builds), so strip it here; the full string with the
# suffix is still used below for the version shown in the app.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" PROJECT_VERSION_NUMERIC "${PROJECT_VERSION_FROM_CHANGELOG}")
if(NOT PROJECT_VERSION_NUMERIC)
    message(FATAL_ERROR "Could not extract a numeric version from '${PROJECT_VERSION_FROM_CHANGELOG}'")
endif()

project(mx-tools
    VERSION ${PROJECT_VERSION_NUMERIC}
    DESCRIPTION "MX Tools - Dashboard application for configuration tools in MX Linux"
    LANGUAGES CXX
)

include(CTest)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile commands export for IDEs and tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Concurrent
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    LinguistTools
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

# These policies were introduced after the minimum Qt version available on
# some supported distributions. Only set them when the installed Qt knows
# about them.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    qt6_policy(SET QTP0001 NEW)
    set(MX_TOOLS_QML_RESOURCE_PREFIX "/qt/qml")
else()
    set(MX_TOOLS_QML_RESOURCE_PREFIX "")
endif()
# QTP0001 decides where the QML module's resources live; C++ needs the same path.
add_compile_definitions(MX_TOOLS_LOGO_RESOURCE=":${MX_TOOLS_QML_RESOURCE_PREFIX}/MxTools/icons/logo.svg")
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt6_policy(SET QTP0004 NEW)
endif()

# Enable automatic MOC, UIC, and RCC processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

# Define source files
set(SOURCES
    src/main.cpp
    src/toolmodel.cpp
)

set(HEADERS
    src/toolmodel.h
)

# Get all translation files
file(GLOB TRANSLATION_FILES "translations/*.ts")
list(FILTER TRANSLATION_FILES EXCLUDE REGEX "/mx-tools_en_US\\.ts$")

# Create the executable
add_executable(mx-tools
    ${SOURCES}
    ${HEADERS}
)

# The Settings QML type moved from Qt.labs.settings into QtCore in Qt 6.5.
# On older Qt (e.g. Debian Bookworm's 6.4), QtCore does not provide it yet,
# so "import QtCore" leaves Main.qml unable to resolve Settings. Only rewrite
# the import for those older Qt versions; Main.qml is embedded unmodified
# everywhere else (e.g. Trixie's Qt 6.8).
set(MX_TOOLS_MAIN_QML qml/Main.qml)
if(Qt6_VERSION VERSION_LESS 6.5)
    message(STATUS "Qt ${Qt6_VERSION} < 6.5: using Qt.labs.settings instead of QtCore Settings in Main.qml")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml MX_TOOLS_MAIN_QML_CONTENT)
    string(REPLACE "import QtCore" "import Qt.labs.settings" MX_TOOLS_MAIN_QML_CONTENT "${MX_TOOLS_MAIN_QML_CONTENT}")
    set(MX_TOOLS_MAIN_QML ${CMAKE_CURRENT_BINARY_DIR}/generated-qml/Main.qml)
    file(WRITE ${MX_TOOLS_MAIN_QML} "${MX_TOOLS_MAIN_QML_CONTENT}")
    set_source_files_properties(${MX_TOOLS_MAIN_QML} PROPERTIES QT_RESOURCE_ALIAS "qml/Main.qml")
endif()

qt6_add_qml_module(mx-tools
    URI MxTools
    VERSION 1.0
    QML_FILES
        ${MX_TOOLS_MAIN_QML}
        qml/components/CategoryButton.qml
        qml/components/CategoryRepeater.qml
        qml/components/MenuVisibilityToggle.qml
        qml/components/ModernSwitch.qml
        qml/components/SecondaryButton.qml
        qml/components/ThemeToolTip.qml
        qml/components/ToolCard.qml
    RESOURCES
        icons/logo.svg
)

# Link Qt6 libraries
target_link_libraries(mx-tools PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

# Set compiler flags
# Warnings shared by the application and the tests; every warning is an error.
add_library(mx-tools-warnings INTERFACE)
target_compile_options(mx-tools-warnings INTERFACE
    -Wall
    -Wextra
    -Wshadow
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
    -Werror
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(mx-tools-warnings INTERFACE -Werror=return-stack-address)
else()
    target_compile_options(mx-tools-warnings INTERFACE -Werror=return-local-addr)
endif()
target_link_libraries(mx-tools PRIVATE mx-tools-warnings)

# Set compile definitions
target_compile_definitions(mx-tools PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    VERSION="${PROJECT_VERSION_FROM_CHANGELOG}"
)

# Release builds optimize for size, and use link-time optimization where the toolchain
# supports it. CMake's Release flags already define NDEBUG.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(mx-tools PRIVATE -Os)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT MX_TOOLS_IPO_SUPPORTED OUTPUT MX_TOOLS_IPO_OUTPUT LANGUAGES CXX)
    if(MX_TOOLS_IPO_SUPPORTED)
        set_property(TARGET mx-tools PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "Link-time optimization is not available: ${MX_TOOLS_IPO_OUTPUT}")
    endif()
endif()

# Handle translations
qt6_add_translations(mx-tools
    TS_FILES ${TRANSLATION_FILES}
    LRELEASE_OPTIONS -compress -nounfinished -removeidentical -silent
    QM_FILES_OUTPUT_VARIABLE qm_files
)

# Set target properties
set_target_properties(mx-tools PROPERTIES
    OUTPUT_NAME "mx-tools"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)



# Install everything here, so that every packager gets the same files from
# 'cmake --install' (dh_auto_install on Debian).
include(GNUInstallDirs)
# The "MX Tools" submenu; Debian gets it from desktop-defaults-mx-common instead.
option(MX_TOOLS_INSTALL_MENU "Install the MX Tools menu directory and merge file" OFF)

install(TARGETS mx-tools RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${qm_files} DESTINATION ${CMAKE_INSTALL_DATADIR}/mx-tools/locale)
install(FILES data/mx-tools.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES icons/mx-tools.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/96x96/apps)
install(FILES icons/mx-tools.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES help/license.html help/custom.css DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES help/mx-tools.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
if(MX_TOOLS_INSTALL_MENU)
    install(FILES data/menu/mx-tools-menu.directory DESTINATION ${CMAKE_INSTALL_DATADIR}/desktop-directories)
    # FULL_SYSCONFDIR is /etc for a /usr prefix, where the relative form would give /usr/etc.
    install(FILES data/menu/mx-tools-menu.menu
            DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/menus/applications-merged)
endif()

if(BUILD_TESTING)
    add_executable(test-toolmodel
        tests/test_toolmodel.cpp
        src/toolmodel.cpp
        src/toolmodel.h
    )
    target_link_libraries(test-toolmodel PRIVATE
        Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::Test
    )
    target_link_libraries(test-toolmodel PRIVATE mx-tools-warnings)
    target_compile_definitions(test-toolmodel PRIVATE
        MX_TOOLS_APPLICATIONS_PATH="${CMAKE_CURRENT_BINARY_DIR}/test-applications"
        MX_TOOLS_CHANGELOG_PATH="${CMAKE_CURRENT_BINARY_DIR}/test-changelog.gz"
        MX_TOOLS_FAKE_XFCONF_QUERY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fake-xfconf-query"
        MX_TOOLS_TESTING
    )
    target_include_directories(test-toolmodel PRIVATE src)
    add_test(NAME toolmodel COMMAND test-toolmodel)
    set_tests_properties(toolmodel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
