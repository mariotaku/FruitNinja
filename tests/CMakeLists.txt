# tests/CMakeLists.txt — minimal ctest registration.

add_executable(test_localisation test_localisation.cpp)
target_link_libraries(test_localisation PRIVATE mortar_engine)
target_include_directories(test_localisation PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)

add_test(NAME localisation COMMAND test_localisation)
