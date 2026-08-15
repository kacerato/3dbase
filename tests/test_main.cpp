#include "test_harness.hpp"

int main() {
    int failed = 0;
    for (const auto& item : test::registry()) {
        try {
            item.function();
            std::cout << "[PASS] " << item.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << item.name << "\n  " << error.what() << '\n';
        }
    }
    std::cout << "Executed " << test::registry().size() << " tests, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}
