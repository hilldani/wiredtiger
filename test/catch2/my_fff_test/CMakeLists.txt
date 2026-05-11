create_test_executable(catch2-my-fff-test
    SOURCES
        test_my_fff_test.cpp
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

add_test(NAME my_fff_test COMMAND $<TARGET_FILE:catch2-my-fff-test>)
set_tests_properties(my_fff_test PROPERTIES LABELS "check;unittest")
