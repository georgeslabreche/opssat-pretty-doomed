/*
 * test_framework.h - Minimal test framework for common/ unit tests
 *
 * Shared macros for run_test, ASSERT_EQ, ASSERT_TRUE, ASSERT_FALSE.
 */

#ifndef PRETTY_TEST_FRAMEWORK_H
#define PRETTY_TEST_FRAMEWORK_H

#include <iostream>
#include <sstream>
#include <stdexcept>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

static void run_test(const char* name, void (*fn)()) {
    g_tests_run++;
    try {
        fn();
        g_tests_passed++;
        std::cout << "  PASS: " << name << "\n";
    } catch (const std::exception& e) {
        g_tests_failed++;
        std::cerr << "  FAIL: " << name << " — " << e.what() << "\n";
    }
}

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::ostringstream oss; \
        oss << "Expected '" << (b) << "', got '" << (a) << "' at line " << __LINE__; \
        throw std::runtime_error(oss.str()); \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::ostringstream oss; \
        oss << "Assertion failed at line " << __LINE__; \
        throw std::runtime_error(oss.str()); \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

static int test_summary() {
    std::cout << "\n=========================\n";
    std::cout << g_tests_passed << "/" << g_tests_run << " passed";
    if (g_tests_failed > 0) {
        std::cout << ", " << g_tests_failed << " FAILED";
    }
    std::cout << "\n";
    return g_tests_failed > 0 ? 1 : 0;
}

#endif // PRETTY_TEST_FRAMEWORK_H
