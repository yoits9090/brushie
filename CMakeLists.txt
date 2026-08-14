cmake_minimum_required(VERSION 3.20)
project(brushie VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(brushie STATIC src/codec.cpp)
target_link_libraries(brushie PUBLIC Threads::Threads)
target_include_directories(brushie PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_options(brushie PRIVATE -O3 -ffast-math -Wall -Wextra -Wpedantic)

# Optional PNG I/O (used by the CLI; the codec itself is self-contained).
find_package(PNG QUIET)
add_executable(brushie_cli src/main.cpp)
target_link_libraries(brushie_cli PRIVATE brushie Threads::Threads)
if(PNG_FOUND)
  target_compile_definitions(brushie_cli PRIVATE BRUSHIE_HAVE_PNG)
  target_include_directories(brushie_cli PRIVATE ${PNG_INCLUDE_DIRS})
  target_link_libraries(brushie_cli PRIVATE ${PNG_LIBRARIES})
endif()
set_target_properties(brushie_cli PROPERTIES OUTPUT_NAME brushie)

if(BUILD_TESTING)
  add_executable(brushie_test_codec tests/test_codec.cpp)
  target_link_libraries(brushie_test_codec PRIVATE brushie)
  # The test uses assert for its failure checks; keep them enabled in Release.
  target_compile_options(brushie_test_codec PRIVATE -UNDEBUG)
  add_test(NAME codec_roundtrip COMMAND brushie_test_codec)
  set_tests_properties(codec_roundtrip PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

include(GNUInstallDirs)
install(TARGETS brushie brushie_cli brushie_test_codec
        EXPORT brushieTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(EXPORT brushieTargets
        FILE brushieTargets.cmake
        NAMESPACE brushie::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/brushie)
