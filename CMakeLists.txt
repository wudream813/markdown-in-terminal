cmake_minimum_required(VERSION 3.14)
project(mdt LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(MDT_VERSION "0.1.0" CACHE STRING "mdt version string")

option(MDT_USE_CURL "link against libcurl when available (remote images)" ON)
option(MDT_STATIC_MINGW "link the MinGW runtime statically (no DLL dependencies)" ON)
option(MDT_BUILD_TESTS "build the small developer tools" ON)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------- quickjs ----
set(QJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/quickjs)
set(QJS_SRC
  ${QJS_DIR}/quickjs.c
  ${QJS_DIR}/libregexp.c
  ${QJS_DIR}/libunicode.c
  ${QJS_DIR}/cutils.c
  ${QJS_DIR}/xsum.c
  ${QJS_DIR}/quickjs-libc.c
)
add_library(qjs STATIC ${QJS_SRC})
target_include_directories(qjs PUBLIC ${QJS_DIR})
target_compile_definitions(qjs PRIVATE _GNU_SOURCE CONFIG_VERSION="\"2025-09-13\"")
if(MSVC)
  target_compile_definitions(qjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
  target_compile_options(qjs PRIVATE /W0)
else()
  target_compile_options(qjs PRIVATE -w -funsigned-char)
endif()
if(MINGW)
  target_link_libraries(qjs PUBLIC ws2_32)
else()
  target_link_libraries(qjs PUBLIC ${CMAKE_DL_LIBS} Threads::Threads m)
endif()

# --------------------------------------------------------------------- mdt ---
set(MDT_SRC
  src/main.cpp
  src/app.cpp
  src/md.cpp
  src/render.cpp
  src/render_draw.cpp
  src/term.cpp
  src/platform.cpp
  src/image.cpp
  src/svg.cpp
  src/math.cpp
  src/font.cpp
  src/util.cpp
  src/generated/assets_js.cpp
)

# Regenerate the embedded JS bundle when the asset changes (needs python3).
set(JS_BUNDLE ${CMAKE_CURRENT_SOURCE_DIR}/assets/js/mathjax-tex-svg.js)
if(EXISTS ${JS_BUNDLE})
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/assets_js.cpp
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_assets.py
      DEPENDS ${JS_BUNDLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_assets.py
      COMMENT "Embedding the maths JS bundle")
    set_source_files_properties(src/generated/assets_js.cpp PROPERTIES GENERATED FALSE)
  endif()
endif()

add_executable(mdt ${MDT_SRC})
target_include_directories(mdt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
target_compile_definitions(mdt PRIVATE MDT_VERSION="${MDT_VERSION}")
if(NOT MSVC)
  target_compile_options(mdt PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
endif()
target_link_libraries(mdt PRIVATE qjs)

# ------------------------------------------------------------- Windows ------
if(MINGW)
  # ws2_32 is needed by QuickJS' quickjs-libc; the runtime is linked statically
  # so the produced .exe has no libgcc/libstdc++/winpthread DLL dependencies.
  target_link_libraries(mdt PRIVATE ws2_32)
  if(MDT_STATIC_MINGW)
    target_link_options(mdt PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  target_compile_definitions(mdt PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 NOMINMAX MDT_WINDOWS)
  target_compile_definitions(qjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()

# libcurl is not used for cross builds (a host libcurl cannot be linked into PE)
if(MINGW)
  set(MDT_USE_CURL OFF)
endif()

if(MDT_USE_CURL)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    target_compile_definitions(mdt PRIVATE MDT_HAVE_CURL=1)
    target_link_libraries(mdt PRIVATE CURL::libcurl)
    message(STATUS "mdt: libcurl found - remote images enabled")
  else()
    message(STATUS "mdt: libcurl not found - remote images fall back to the curl binary")
  endif()
endif()

install(TARGETS mdt RUNTIME DESTINATION bin)

# --------------------------------------------------------------- dev tools ---
if(MDT_BUILD_TESTS)
  # the dev tool brings its own main(): reuse the sources without main.cpp
  set(MDT_LIB_SRC ${MDT_SRC})
  list(REMOVE_ITEM MDT_LIB_SRC src/main.cpp)
  add_executable(test_math tools/test_math.cpp ${MDT_LIB_SRC})
  target_include_directories(test_math PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
  target_compile_definitions(test_math PRIVATE MDT_VERSION="${MDT_VERSION}")
  target_link_libraries(test_math PRIVATE qjs)
  if(NOT MSVC)
    target_compile_options(test_math PRIVATE -w)
  endif()

  add_executable(test_svg tools/test_svg.cpp src/svg.cpp src/image.cpp src/util.cpp)
  target_include_directories(test_svg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
  target_link_libraries(test_svg PRIVATE m)
  if(NOT MSVC)
    target_compile_options(test_svg PRIVATE -w)
  endif()
endif()
