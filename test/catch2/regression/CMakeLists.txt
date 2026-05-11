include(${CMAKE_SOURCE_DIR}/test/ctest_helpers.cmake)

# Common include paths shared by all regression targets.
set(regression_includes
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Wrapper sources needed by mock-session based regression tests.
set(mock_wrapper_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/mock_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/mock_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/block_mods.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/config_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/item_wrapper.cpp
)

# Wrapper sources needed by follower-connection based regression tests.
set(follower_wrapper_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/connection_wrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/mock_connection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/mock_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/block_mods.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/config_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrappers/item_wrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../truncate/truncate_list_helpers.cpp
)

# Helper macro: create a regression target backed by mock_session.
macro(add_regression_mock ticket)
    create_test_executable(catch2-regression-${ticket}
        SOURCES
            regression_main.cpp
            ${ticket}/test_${ticket}.cpp
            ${ticket}/fakes_${ticket}.cpp
            ${mock_wrapper_sources}
        INCLUDES
            ${regression_includes}
        FLAGS
            ${COMPILER_DIAGNOSTIC_CXX_FLAGS}
        LIBS
            Catch2::Catch2
        CXX
    )
    add_test(
        NAME regression-${ticket}
        COMMAND $<TARGET_FILE:catch2-regression-${ticket}>
    )
    set_tests_properties(regression-${ticket} PROPERTIES LABELS "check;regression")
endmacro()

# Helper macro: create a regression target backed by follower_connection (palite).
macro(add_regression_follower ticket)
    create_test_executable(catch2-regression-${ticket}
        SOURCES
            regression_main.cpp
            ${ticket}/test_${ticket}.cpp
            ${ticket}/fakes_${ticket}.cpp
            ${follower_wrapper_sources}
        INCLUDES
            ${regression_includes}
        FLAGS
            ${COMPILER_DIAGNOSTIC_CXX_FLAGS}
        LIBS
            Catch2::Catch2
        CXX
    )
    add_test(
        NAME regression-${ticket}
        COMMAND $<TARGET_FILE:catch2-regression-${ticket}>
    )
    set_tests_properties(regression-${ticket} PROPERTIES LABELS "check;regression")
endmacro()

# ── Group 1: reconciliation visibility (mock session, all platforms) ─────────
add_regression_mock(wt_17383)
add_regression_mock(wt_17321)
add_regression_mock(wt_17322)
add_regression_mock(wt_17387_tombstone)

# ── Group 3: fff/__ut_ link-seam tests (mock session, all platforms) ─────────
add_regression_mock(wt_17248)
add_regression_mock(wt_17297)
add_regression_mock(wt_17403)

# ── Group 2: layered cursor / Group 3 palite tests (skip Windows) ────────────
if(NOT WT_WIN)
    add_regression_follower(wt_17379)
    add_regression_follower(wt_17266)
    add_regression_follower(wt_17336)
    add_regression_follower(wt_17252)
    add_regression_mock(wt_17255)
    add_regression_mock(wt_17387_seen_committed)
endif()
