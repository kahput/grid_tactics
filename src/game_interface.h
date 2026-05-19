#pragma once

#include "common.h"

typedef struct {
	uint8_t *memory;
	uint64_t memory_size;

	uint8_t *transient_memory;
	uint64_t transient_memory_size;
} GameContext;

#ifdef PLATFORM_DESKTOP
	#define GLSL_VERSION 330
#else
	#define GLSL_VERSION 100
#endif
