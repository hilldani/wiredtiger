create_test_executable(catch2-wrap-test
    SOURCES
        test_my_wrap_test.cpp
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/test/catch2
    FLAGS
        ${COMPILER_DIAGNOSTIC_CXX_FLAGS}
    LIBS
        Catch2::Catch2
        fff::fff
    CXX
)

# --wrap is a GNU ld / LLVM lld feature; Apple ld does not support it.
# On Linux this intercepts wiredtiger_pack_start even within pack_stream.c.o
# (same archive member as wiredtiger_unpack_start), which is impossible with
# plain archive-resolution ordering.
if(NOT WT_DARWIN)
    #target_link_options(catch2-wrap-test PRIVATE -Wl,--wrap=wiredtiger_pack_start)
    target_compile_definitions(catch2-wrap-test PRIVATE HAVE_LINKER_WRAP)
endif()

add_test(NAME wrap_test COMMAND $<TARGET_FILE:catch2-wrap-test>)
set_tests_properties(wrap_test PROPERTIES LABELS "check;unittest")
