// System memory metrics read straight from /proc/meminfo — no psutil needed.
#pragma once

#include "sample.hpp"

namespace ram {

// Return a RAMSample snapshot of virtual memory and swap (values in MiB).
RAMSample collect();

}  // namespace ram
