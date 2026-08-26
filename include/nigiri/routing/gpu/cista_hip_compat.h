#pragma once

// Force-included (-include) into the HIP translation units; see CMakeLists.
//
// cista annotates the device-usable parts of its containers with
// CISTA_CUDA_COMPAT, which cista/cuda_check.h defines as __host__ __device__
// only when __CUDA_ARCH__ is set. HIP never defines that macro, so on an AMD
// build every cista accessor - basic_vecvec::operator[] above all - would stay
// host-only and become uncallable from a kernel.
//
// Pulling cista's header in here first lets its #pragma once suppress the
// later include from cista's own containers, so these definitions are the ones
// the containers see. nigiri's own sources use NIGIRI_GPU_COMPAT
// (nigiri/gpu_compat.h) and are unaffected either way.
//
// The proper fix belongs upstream in cista: widen the condition in
// cista/cuda_check.h to
//   #if defined(__CUDA_ARCH__) || defined(__HIP_DEVICE_COMPILE__)
// at which point this file can go away.

#include "cista/cuda_check.h"

#undef CISTA_CUDA_COMPAT
#undef CISTA_CUDA_DEVICE_COMPAT

#define CISTA_CUDA_COMPAT __host__ __device__
#define CISTA_CUDA_DEVICE_COMPAT __device__
