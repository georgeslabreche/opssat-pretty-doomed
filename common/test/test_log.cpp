/*
 * test_log.cpp - Unit tests for pretty_log.h (trim, local_tm)
 *
 * Build:
 *   g++ -Wall -O2 -std=c++17 -I../include test_log.cpp -o test_log
 *
 * Run:
 *   ./test_log
 */

#include <cstring>
#include <string>

#include "pretty_log.h"
#include "test_framework.h"

using namespace pretty;

// ── trim() tests ─────────────────────────────────────────────────────

void test_trim_basic() {
    ASSERT_EQ(trim("hello"), "hello");
    ASSERT_EQ(trim("  hello  "), "hello");
    ASSERT_EQ(trim("\thello\n"), "hello");
    ASSERT_EQ(trim("  \t  hello world  \r\n  "), "hello world");
}

void test_trim_empty() {
    ASSERT_EQ(trim(""), "");
    ASSERT_EQ(trim("   "), "");
    ASSERT_EQ(trim("\t\r\n"), "");
}

void test_trim_trailing_nul() {
    // This is the bug that caused the v4 EM failure:
    // iio_device_debug_attr_read returns "1\0" (2 bytes)
    std::string with_nul("1\0", 2);
    ASSERT_EQ(trim(with_nul), "1");
}

void test_trim_nul_with_space() {
    // "1 \0" — trailing space then NUL
    std::string val("1 \0", 3);
    ASSERT_EQ(trim(val), "1");
}

void test_trim_multiple_trailing_nuls() {
    // "1\0\0\0" — multiple trailing NULs
    std::string val("1\0\0\0", 4);
    ASSERT_EQ(trim(val), "1");
}

void test_trim_only_nul() {
    std::string val("\0\0\0", 3);
    ASSERT_EQ(trim(val), "");
}

void test_trim_number_with_nul() {
    // Typical IIO readback: "2400000\0"
    std::string val("2400000\0", 8);
    ASSERT_EQ(trim(val), "2400000");
}

void test_trim_number_with_newline_nul() {
    // Some IIO attrs return "value\n\0"
    std::string val("2400000\n\0", 9);
    ASSERT_EQ(trim(val), "2400000");
}

void test_trim_preserves_internal_spaces() {
    ASSERT_EQ(trim("hello world"), "hello world");
    ASSERT_EQ(trim("  hello  world  "), "hello  world");
}

void test_trim_preserves_internal_nul() {
    // NUL in the middle should stay.
    // Our trim strips trailing NULs then trims whitespace.
    std::string val("a\0b", 3);
    std::string result = trim(val);
    ASSERT_EQ(result.size(), (size_t)3);
    ASSERT_EQ(result[0], 'a');
    ASSERT_EQ(result[1], '\0');
    ASSERT_EQ(result[2], 'b');
}

// ── local_tm() tests ─────────────────────────────────────────────────

void test_local_tm_epoch() {
    // Just verify it doesn't crash and returns a valid struct
    std::time_t t = 0;
    std::tm tm = local_tm(t);
    // Year should be 1969 or 1970 depending on timezone
    ASSERT_TRUE(tm.tm_year >= 69 && tm.tm_year <= 70);
}

void test_local_tm_known_date() {
    // 2026-03-10 00:00:00 UTC = 1773014400
    std::time_t t = 1773014400;
    std::tm tm = local_tm(t);
    // tm_year is years since 1900
    ASSERT_TRUE(tm.tm_year == 126 || tm.tm_year == 125); // timezone may shift to prev day
}

// ── main ─────────────────────────────────────────────────────────────

int main() {
    std::cout << "pretty_log.h unit tests\n";
    std::cout << "=========================\n\n";

    std::cout << "trim():\n";
    run_test("trim_basic", test_trim_basic);
    run_test("trim_empty", test_trim_empty);
    run_test("trim_trailing_nul", test_trim_trailing_nul);
    run_test("trim_nul_with_space", test_trim_nul_with_space);
    run_test("trim_multiple_trailing_nuls", test_trim_multiple_trailing_nuls);
    run_test("trim_only_nul", test_trim_only_nul);
    run_test("trim_number_with_nul", test_trim_number_with_nul);
    run_test("trim_number_with_newline_nul", test_trim_number_with_newline_nul);
    run_test("trim_preserves_internal_spaces", test_trim_preserves_internal_spaces);
    run_test("trim_preserves_internal_nul", test_trim_preserves_internal_nul);

    std::cout << "\nlocal_tm():\n";
    run_test("local_tm_epoch", test_local_tm_epoch);
    run_test("local_tm_known_date", test_local_tm_known_date);

    return test_summary();
}
