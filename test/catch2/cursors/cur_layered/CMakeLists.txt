create_test_executable(catch2-cur-layered-tests
    SOURCES
        test_cur_layered.cpp
        ${CMAKE_SOURCE_DIR}/test/catch2/utils.cpp
        ${CMAKE_SOURCE_DIR}/test/catch2/wrappers/mock_connection.cpp
        ${CMAKE_SOURCE_DIR}/test/catch2/wrappers/mock_session.cpp
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
add_test(NAME cur_layered_fff_test COMMAND $<TARGET_FILE:catch2-cur-layered-tests>)
set_tests_properties(cur_layered_fff_test PROPERTIES LABELS "check;unittest")
