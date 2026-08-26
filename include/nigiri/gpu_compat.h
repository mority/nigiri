#pragma once

// Replaces cista/cuda_check.h's CISTA_CUDA_COMPAT, which keys off
// __CUDA_ARCH__ only. HIP never defines __CUDA_ARCH__; its device compilation
// pass is marked by __HIP_DEVICE_COMPILE__ instead, so functions annotated by
// cista's macro would stay host-only and become uncallable from HIP kernels.
//
// The two back ends also need the annotation at different times. nvcc only
// looks at device code in the device pass, so gating on __CUDA_ARCH__ leaves
// ordinary C++ translation units untouched. clang parses and type-checks
// __global__/__device__ bodies in the HIP *host* pass as well, and would
// report every call into an unannotated function there - so on HIP the
// annotation has to be present in both passes (__HIP__ rather than
// __HIP_DEVICE_COMPILE__).
//
// Either way, a translation unit built by a plain C++ compiler sees neither
// macro and the annotations expand to nothing, so nigiri/stop.h and friends
// still need no GPU compiler.

#if defined(__HIP__)
#define NIGIRI_GPU_DEVICE_COMPILE 1
#define NIGIRI_GPU_COMPAT __host__ __device__
#define NIGIRI_GPU_DEVICE_COMPAT __device__
#elif defined(__CUDA_ARCH__)
#define NIGIRI_GPU_DEVICE_COMPILE 1
#define NIGIRI_GPU_COMPAT __host__ __device__
#define NIGIRI_GPU_DEVICE_COMPAT __device__
#else
#define NIGIRI_GPU_COMPAT
#define NIGIRI_GPU_DEVICE_COMPAT
#endif
