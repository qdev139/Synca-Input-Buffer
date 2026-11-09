cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(InputBufferStudio VERSION 1.17.0)

# main.cpp (engine), menu.cpp (menu), menu_button.cpp and popups.cpp are separate translation units.
# The .inc / .hpp files are included by them; keep them in the generated IDE project.
file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB STUDIO_HEADERS CONFIGURE_DEPENDS src/*.inc src/*.hpp)
set_source_files_properties(${STUDIO_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${STUDIO_HEADERS})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
