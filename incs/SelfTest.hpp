#pragma once
#ifndef SELF_TEST_HPP
# define SELF_TEST_HPP

// ============================================================================
//  SelfTest : a tiny test harness, run with `ft_vox --selftest [filter]`
//
//      SELFTEST(RegionStore, roundtrip) {
//          CHECK(store.load(c) != nullptr);
//          CHECK_EQ(column.coord().x, 3);
//      }
//
//  Tests live in srcs/tests/*Test.cpp and register themselves. `filter`
//  keeps the tests whose "Suite.name" contains it. A failing CHECK records
//  the failure and the test keeps running; an exception fails the test.
// ============================================================================

#include <filesystem>
#include <sstream>
#include <string>

namespace selftest {

// A fresh directory under the system temp dir, removed when destroyed.
class TempDir {
public:
	TempDir();
	~TempDir();
	TempDir(const TempDir&)            = delete;
	TempDir& operator=(const TempDir&) = delete;
	[[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }
private:
	std::filesystem::path _path;
};

using TestFn = void (*)();

struct Registrar {
	Registrar(const char* suite, const char* name, TestFn fn);
};

void fail(const char* file, int line, const std::string& message);

// Streams `value` when it can be streamed, otherwise a placeholder.
template <typename T>
std::string show(const T& value) {
	if constexpr (requires(std::ostream& os) { os << value; }) {
		std::ostringstream os;
		os << value;
		return os.str();
	} else {
		return "<value>";
	}
}

} // namespace selftest

int runSelfTests(int argc, char** argv);

#define SELFTEST(suite, name)                                                            \
	static void selftest_##suite##_##name();                                             \
	static const ::selftest::Registrar selftest_reg_##suite##_##name(                    \
		#suite, #name, &selftest_##suite##_##name);                                      \
	static void selftest_##suite##_##name()

#define CHECK(cond)                                                                      \
	do {                                                                                 \
		if (!(cond))                                                                     \
			::selftest::fail(__FILE__, __LINE__, "CHECK(" #cond ")");                    \
	} while (0)

#define CHECK_EQ(a, b)                                                                   \
	do {                                                                                 \
		const auto& selftest_a_ = (a);                                                   \
		const auto& selftest_b_ = (b);                                                   \
		if (!(selftest_a_ == selftest_b_))                                               \
			::selftest::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b "): "            \
				+ ::selftest::show(selftest_a_) + " != " + ::selftest::show(selftest_b_)); \
	} while (0)

#endif /* SELF_TEST_HPP */
