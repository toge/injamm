find_package(Catch2 CONFIG REQUIRED)
include(Catch)

add_executable(injamm-sqlite3_mock_test test_mock.cpp)
target_link_libraries(injamm-sqlite3_mock_test PRIVATE injamm-sqlite3 Catch2::Catch2WithMain)
catch_discover_tests(injamm-sqlite3_mock_test)
add_test(NAME injamm-sqlite3_mock_test COMMAND injamm-sqlite3_mock_test)

find_package(SQLite3 REQUIRED)

add_executable(injamm-sqlite3_test test_sqlite3.cpp)
target_link_libraries(injamm-sqlite3_test PRIVATE injamm-sqlite3 Catch2::Catch2WithMain SQLite3::SQLite3)
catch_discover_tests(injamm-sqlite3_test)
add_test(NAME injamm-sqlite3_test COMMAND injamm-sqlite3_test)
