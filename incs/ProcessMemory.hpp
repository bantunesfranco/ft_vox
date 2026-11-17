#ifndef PROCESS_MEMORY_HPP
#define PROCESS_MEMORY_HPP

#include <cstddef>

// Memory of this process as the OS counts it: the resident set (Windows: working set).
namespace ProcessMemory {

// Resident now, 0 when the OS does not tell.
[[nodiscard]] std::size_t currentBytes() noexcept;
// Highest resident so far.
[[nodiscard]] std::size_t peakBytes() noexcept;

}

#endif /* PROCESS_MEMORY_HPP */
