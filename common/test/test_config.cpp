/*
 * test_config.cpp - Unit tests for pretty_config.h (load_config_map)
 *
 * Build:
 *   g++ -Wall -O2 -std=c++17 -I../include test_config.cpp -o test_config
 *
 * Run:
 *   ./test_config
 */

#include <cstdio>
#include <fstream>
#include <string>

#include "pretty_config.h"
#include "test_framework.h"

using namespace pretty;

// ── helpers ──────────────────────────────────────────────────────────

static const char* TEMP_CFG = "/tmp/test_pretty_config.cfg";

static std::string write_temp_file(const std::string& content) {
    std::ofstream f(TEMP_CFG);
    f << content;
    f.close();
    return TEMP_CFG;
}

// ── load_config_map() tests ──────────────────────────────────────────

void test_config_basic() {
    auto path = write_temp_file("key1=value1\nkey2=value2\n");
    auto result = load_config_map(path);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.values.size(), (size_t)2);
    ASSERT_EQ(result.values["key1"], "value1");
    ASSERT_EQ(result.values["key2"], "value2");
}

void test_config_comments_and_blanks() {
    auto path = write_temp_file("# comment\n\nkey=val\n# another comment\n");
    auto result = load_config_map(path);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.values.size(), (size_t)1);
    ASSERT_EQ(result.values["key"], "val");
}

void test_config_whitespace_trimming() {
    auto path = write_temp_file("  key1  =  value1  \nkey2 = value2\n");
    auto result = load_config_map(path);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.values["key1"], "value1");
    ASSERT_EQ(result.values["key2"], "value2");
}

void test_config_duplicate_keys() {
    auto path = write_temp_file("key=first\nkey=second\n");
    auto result = load_config_map(path);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.values["key"], "second");
}

void test_config_value_with_equals() {
    auto path = write_temp_file("key=a=b=c\n");
    auto result = load_config_map(path);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.values["key"], "a=b=c");
}

void test_config_empty_value() {
    auto path = write_temp_file("key=\n");
    auto result = load_config_map(path);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.values["key"], "");
}

void test_config_missing_file() {
    auto result = load_config_map("/tmp/nonexistent_pretty_test_file.cfg");
    ASSERT_FALSE(result.ok);
    ASSERT_EQ(result.values.size(), (size_t)0);
}

void test_config_no_equals_line() {
    auto path = write_temp_file("this line has no equals\nkey=val\n");
    auto result = load_config_map(path);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.values.size(), (size_t)1);
    ASSERT_EQ(result.values["key"], "val");
}

void test_config_uri_value() {
    auto path = write_temp_file("uri=ip:10.0.0.1\nfrequency=1296000000\n");
    auto result = load_config_map(path);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.values["uri"], "ip:10.0.0.1");
    ASSERT_EQ(result.values["frequency"], "1296000000");
}

// ── main ─────────────────────────────────────────────────────────────

int main() {
    std::cout << "pretty_config.h unit tests\n";
    std::cout << "=========================\n\n";

    std::cout << "load_config_map():\n";
    run_test("config_basic", test_config_basic);
    run_test("config_comments_and_blanks", test_config_comments_and_blanks);
    run_test("config_whitespace_trimming", test_config_whitespace_trimming);
    run_test("config_duplicate_keys", test_config_duplicate_keys);
    run_test("config_value_with_equals", test_config_value_with_equals);
    run_test("config_empty_value", test_config_empty_value);
    run_test("config_missing_file", test_config_missing_file);
    run_test("config_no_equals_line", test_config_no_equals_line);
    run_test("config_uri_value", test_config_uri_value);

    // Cleanup temp file
    std::remove(TEMP_CFG);

    return test_summary();
}
