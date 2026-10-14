#pragma once

// Minimal test helpers shared by all test executables (no external framework).
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace testutil {

inline int& failureCount() {
    static int count = 0;
    return count;
}

// Makes enum classes printable in failure messages.
template <typename T>
auto printable(const T& value) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<long long>(value);
    } else {
        return value;
    }
}

template <typename A, typename B>
void checkEq(const A& actual, const B& expected, const char* actualText,
             const char* expectedText, const char* file, int line) {
    if (!(actual == expected)) {
        std::cerr << file << ":" << line << " CHECK_EQ failed: " << actualText << " = "
                  << printable(actual) << ", expected " << expectedText << " = "
                  << printable(expected) << "\n";
        ++failureCount();
    }
}

// Call at the end of main(): prints a summary and returns the exit code.
inline int finish(const char* suiteName) {
    if (failureCount() == 0) {
        std::cout << suiteName << ": all checks passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << suiteName << ": " << failureCount() << " check(s) failed\n";
    return EXIT_FAILURE;
}

}  // namespace testutil

#define CHECK_EQ(actual, expected) \
    ::testutil::checkEq((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define CHECK_THROWS(expr)                                                        \
    do {                                                                          \
        bool threw = false;                                                       \
        try { (void)(expr); } catch (const std::invalid_argument&) { threw = true; } \
        if (!threw) {                                                             \
            std::cerr << __FILE__ << ":" << __LINE__                              \
                      << " expected std::invalid_argument from: " << #expr << "\n"; \
            ++::testutil::failureCount();                                         \
        }                                                                         \
    } while (0)