#pragma once
// Minimal self-contained test harness (no external dependencies).

#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace pdfcpp_test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure : std::exception {
    std::string msg;
    explicit Failure(std::string m) : msg(std::move(m)) {}
    const char* what() const noexcept override { return msg.c_str(); }
};

inline int runAll() {
    int failed = 0;
    for (auto& t : registry()) {
        try {
            t.fn();
            std::cout << "[  OK  ] " << t.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << ": " << e.what() << "\n";
        }
    }
    std::cout << registry().size() - failed << "/" << registry().size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

/// Locates the Java reference test resources if available.
inline std::string referenceDir() {
    const char* env = std::getenv("PDFCPP_REFERENCE_DIR");
    if (env) return env;
    return PDFCPP_SOURCE_DIR "/reference/itext-java";
}

}  // namespace pdfcpp_test

#define PDFCPP_CAT2(a, b) a##b
#define PDFCPP_CAT(a, b) PDFCPP_CAT2(a, b)
#define TEST(name)                                                                              \
    static void name();                                                                         \
    static pdfcpp_test::Registrar PDFCPP_CAT(registrar_, name)(#name, name);                    \
    static void name()

#define EXPECT_TRUE(cond)                                                                              \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::ostringstream _ss;                                                                    \
            _ss << __FILE__ << ":" << __LINE__ << ": expected true: " #cond;                           \
            throw pdfcpp_test::Failure(_ss.str());                                                     \
        }                                                                                              \
    } while (0)

#define EXPECT_EQ(a, b)                                                                                \
    do {                                                                                               \
        auto _va = (a);                                                                                \
        auto _vb = (b);                                                                                \
        if (!(_va == _vb)) {                                                                           \
            std::ostringstream _ss;                                                                    \
            _ss << __FILE__ << ":" << __LINE__ << ": expected " #a " == " #b " but got '" << _va       \
                << "' vs '" << _vb << "'";                                                             \
            throw pdfcpp_test::Failure(_ss.str());                                                     \
        }                                                                                              \
    } while (0)

#define EXPECT_NEAR(a, b, eps)                                                                         \
    do {                                                                                               \
        double _va = (a), _vb = (b);                                                                   \
        if (std::fabs(_va - _vb) > (eps)) {                                                            \
            std::ostringstream _ss;                                                                    \
            _ss << __FILE__ << ":" << __LINE__ << ": expected " << _va << " ~= " << _vb;              \
            throw pdfcpp_test::Failure(_ss.str());                                                     \
        }                                                                                              \
    } while (0)

#define EXPECT_THROW(stmt)                                                                             \
    do {                                                                                               \
        bool _threw = false;                                                                           \
        try {                                                                                          \
            stmt;                                                                                      \
        } catch (...) {                                                                                \
            _threw = true;                                                                             \
        }                                                                                              \
        if (!_threw) {                                                                                 \
            std::ostringstream _ss;                                                                    \
            _ss << __FILE__ << ":" << __LINE__ << ": expected exception from " #stmt;                  \
            throw pdfcpp_test::Failure(_ss.str());                                                     \
        }                                                                                              \
    } while (0)

#define TEST_MAIN() \
    int main() { return pdfcpp_test::runAll(); }
