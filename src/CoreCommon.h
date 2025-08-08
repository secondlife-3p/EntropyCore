/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 Jonathan "Geenz" Goodman
 * This file is part of the Entropy Core project.
 */

#pragma once

/**
 * @file CoreCommon.h
 * @brief Core common utilities and debugging macros for EntropyCore
 * 
 * This header provides essential debugging utilities and common macros
 * used throughout the EntropyCore library. It includes debug assertions
 * and build configuration flags.
 */

#include <cassert>

#ifdef EntropyDebug
#define ENTROPY_DEBUG_BLOCK(code) do { code } while(0)
#undef NDEBUG
#define ENTROPY_ASSERT(condition, message) assert(condition)
#else
#define ENTROPY_DEBUG_BLOCK(code) ((void)0)
#define ENTROPY_ASSERT(condition, message) ((void)0)
#endif

namespace EntropyEngine {
    // Core utility namespace - currently empty but reserved for future utilities
}