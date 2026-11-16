#include "SelfTest.hpp"

SELFTEST(Harness, runs) {
	CHECK(1 + 1 == 2);
	CHECK_EQ(2 * 3, 6);
}
