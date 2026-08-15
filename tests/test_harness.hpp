#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test {

using TestFunction = std::function<void()>;

struct Case final {
    std::string name;
    TestFunction function;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar final {
    Registrar(std::string name, TestFunction function) {
        registry().push_back({std::move(name), std::move(function)});
    }
};

inline void require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                                 " requirement failed: " + expression);
    }
}

} // namespace test

#define M3D_JOIN_IMPL(a, b) a##b
#define M3D_JOIN(a, b) M3D_JOIN_IMPL(a, b)
#define TEST_CASE(name) \
    static void M3D_JOIN(test_fn_, __LINE__)(); \
    static ::test::Registrar M3D_JOIN(test_reg_, __LINE__)(name, M3D_JOIN(test_fn_, __LINE__)); \
    static void M3D_JOIN(test_fn_, __LINE__)()
#define REQUIRE(expression) ::test::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
