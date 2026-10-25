// GPU metrics via the `nvidia-smi` CLI.
//
// Same approach as the Python original, for the same reason: we shell out to
// nvidia-smi and parse its CSV output rather than touching NVML directly.
// nvidia-smi ships with the driver, is exactly what NVTOP uses, and sidesteps
// the NVML-initialization breakage this machine saw with late-version
// bindings. One subprocess per second is nothing on Linux.
#pragma once

#include "sample.hpp"

#include <vector>

namespace gpu {

// Return a GPUSample for every NVIDIA GPU on the machine.
// Returns {} (and logs the reason) when the driver is unreachable.
std::vector<GPUSample> collect();

}  // namespace gpu
