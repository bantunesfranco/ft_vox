#include "SelfTest.hpp"
#include "FrameProfiler.hpp"

#include <array>
#include <string>

SELFTEST(FrameProfiler, slow_frame_report) {
	std::array<double, FrameProfiler::PhaseCount> ms{};
	ms.fill(1.0);
	CHECK(!FrameProfiler::slowFrameReport(ms).has_value());   // 8 ms: not slow

	ms.fill(0.0);
	ms[FrameProfiler::Mesh] = 40.0;
	ms[FrameProfiler::Draw] = 15.0;
	const auto line = FrameProfiler::slowFrameReport(ms);
	CHECK(line.has_value());
	CHECK(line->rfind("slow frame: 55", 0) == 0);
	const auto mesh = line->find("Mesh 40");
	const auto draw = line->find("Draw 15");
	CHECK(mesh != std::string::npos);
	CHECK(draw != std::string::npos);
	CHECK(mesh < draw);                                        // largest first
	CHECK(line->find("Swap") == std::string::npos);            // under 1 ms: left out
}
