cmake_minimum_required(VERSION 4.0)
project(program)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS  ON)

file(GLOB_RECURSE GAME_SOURCES "src/game/*.c" "src/game/*.h" )

if(${PLATFORM} STREQUAL "Web")
    message(STATUS "Building for Web...")

    add_executable("index" "src/main_web.c" "src/game_interface.h" ${GAME_SOURCES})
    target_include_directories("index" PRIVATE "./src/")
    target_compile_definitions("index" PRIVATE PLATFORM_WEB)

    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    add_definitions(-DPLATFORM_WEB) 

    set(RAYLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/raylib") 
    
    add_library(raylib STATIC IMPORTED)
    set_target_properties(raylib PROPERTIES
        IMPORTED_LOCATION "${RAYLIB_DIR}/lib/libraylib.a"
        INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_DIR}/include"
    )
    
    target_link_libraries("index" PRIVATE raylib)

    target_link_options("index" PRIVATE
        "-sUSE_GLFW=3"
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASYNCIFY"
        "-sTOTAL_STACK=64mb"
# "-sASSERTIONS=2" 
#     "-sSAFE_HEAP=1"
#     "-sSTACK_OVERFLOW_CHECK=2"
#     "-g" # Generate source maps to see C code in browser console
    )

    if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
        target_link_options("index" PRIVATE 
            "--preload-file" "${CMAKE_SOURCE_DIR}/assets@/assets"
        )
    endif()

else()
    include(FetchContent)

    message(STATUS "Building for Desktop...")
    if(MSVC)
        set(CONFIG $<CONFIG>)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    else()
        set(CONFIG ${CMAKE_BUILD_TYPE})
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
    endif()

    add_executable(${PROJECT_NAME} "src/main_desktop.c" "src/game_interface.h")
    target_include_directories(${PROJECT_NAME} PRIVATE "./src/")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -pedantic -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable)

    FetchContent_Declare(
        raylib
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
        URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
        FIND_PACKAGE_ARGS
    )

    FetchContent_MakeAvailable(raylib)
    
    target_link_libraries(${PROJECT_NAME} PRIVATE dl raylib m )

    add_library(game SHARED ${GAME_SOURCES})
    target_include_directories(game PRIVATE "./src/" "./src/game/")
    target_compile_options(game PRIVATE -Wall -pedantic -Wextra -Werror
        -Wno-unused-parameter -Wno-unused-variable -Wno-override-init)
    target_compile_definitions(game PRIVATE PLATFORM_DESKTOP)

    if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
        set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
        file(GLOB_RECURSE ASSET_FILES "${ASSETS_DIR}/*.*")

        foreach(ASSET_FILE ${ASSET_FILES})
            file(RELATIVE_PATH REL_PATH "${ASSETS_DIR}" "${ASSET_FILE}")
            set(DEST_FILE "${CMAKE_BINARY_DIR}/bin/${CONFIG}/assets/${REL_PATH}")

            get_filename_component(FILE_EXTENSION "${ASSET_FILE}" EXT)
            
            # Logic: If .glsl, compile to .spv. Otherwise, copy.
            if (${FILE_EXTENSION} STREQUAL ".glsl") 
                get_filename_component(DEST_DIR ${DEST_FILE} DIRECTORY)
                get_filename_component(DEST_NAME ${DEST_FILE} NAME_WE)
                set(SPV_FILE ${DEST_DIR}/${DEST_NAME}.spv)
                
                add_custom_command(
                    OUTPUT "${SPV_FILE}"
                    COMMAND glslc "${ASSET_FILE}" -o "${SPV_FILE}"
                    DEPENDS "${ASSET_FILE}"
                    COMMENT "Compiling GLSL shader: ${REL_PATH} -> ${REL_PATH}.spv"
                    VERBATIM
                )
                list(APPEND ASSET_OUTPUTS "${SPV_FILE}")
            else()
                add_custom_command(
                    OUTPUT "${DEST_FILE}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASSET_FILE}" "${DEST_FILE}"
                    DEPENDS "${ASSET_FILE}"
                    COMMENT "Copying asset: ${REL_PATH}"
                    VERBATIM
                )
                list(APPEND ASSET_OUTPUTS "${DEST_FILE}")
            endif()
        endforeach()

        add_custom_target(copy_assets ALL DEPENDS ${ASSET_OUTPUTS})
    endif()
endif()
