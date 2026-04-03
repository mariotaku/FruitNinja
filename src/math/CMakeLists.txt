# Header-only math library
add_library(fn_math INTERFACE)
target_include_directories(fn_math INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fruit-ninja PRIVATE fn_math)
