# Auto-generated symbol-coverage stubs for binary methods that haven't been
# ported yet. Regenerate via:  python tools/symbol-diff/gen_stubs.py
#
# Each stub method body is empty (or returns 0). asm-verify diffs the
# binary's real instructions against these no-ops so missing port code is
# visible at the instruction level rather than hidden behind a missing
# symbol entry.

file(GLOB FN_STUB_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (FN_STUB_SOURCES)
    add_library(fruit-ninja-stubs STATIC ${FN_STUB_SOURCES})
    target_link_libraries(fruit-ninja-stubs PUBLIC mortar_engine)
    target_include_directories(fruit-ninja-stubs PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine
    )
    if (MSVC)
        target_compile_options(fruit-ninja-stubs PRIVATE /w)
    else()
        target_compile_options(fruit-ninja-stubs PRIVATE -w)
    endif()
endif()
