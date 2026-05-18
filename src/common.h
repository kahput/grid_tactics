#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
	#define alignof(type) _Alignof(type)
#elif defined(__GNUC__) || defined(__clang__)
	#define alignof(type) __alignof__(type)
#elif defined(_MSC_VER)
	#define alignof(type) __alignof(type)
#else
	#define alignof(T) (size_t)(&((struct {  char byte; T offset; } *)0)->offset)
#endif

#define sizeof_member(type, member) (sizeof(((type *)0)->member))
#define countof(array) (sizeof(array) / sizeof((array)[0]))
#define indexof(array, ptr) (uint32_t)(ptr - array)
#define container_of(ptr, T, member) ((T *)((uint8_t *)ptr - offsetof(T, member)))

#define memory_copy(dst, src, size) memcpy((dst), (src), (size))
#define memory_set(dst, byte, size) memset((dst), (byte), (size))
#define memory_compare(a, b, size) memcmp((a), (b), (size))

#define string_length(str) (strlen((str)))
#define string_size(str) (strlen((str)) + 1)

#define memory_copy_struct(d, s) memory_copy((d), (s), sizeof(*(d)))
#define memory_copy_array(d, s) memory_copy((d), (s), sizeof(d))
#define memory_copy_count(d, s, c) memory_copy((d), (s), sizeof(*(d)) * (c))

#define memory_zero(s, z) memory_set((s), 0, (z))
#define memory_zero_struct(s) memory_zero(&(s), sizeof((s)))
#define memory_zero_array(a) memory_zero((a), sizeof(a))
#define memory_zero_count(m, c) memory_zero((m), sizeof(*(m)) * (c))

#define memory_equals(a, b, z) (memory_compare((a), (b), (z)) == 0)
#define memory_equals_struct(a, b) memory_equals((a), (b), sizeof(*(a)))
#define memory_equals_array(a, b) memory_equals((a), (b), sizeof(a))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define SIGN(v) ((v) > 0 ? 1 : 0)

#define CLAMP(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))

#define FLAG_GET(flags, flag) ((flags & flag) == flag)
#define HEADER(ptr, T) ((T *)ptr - 1)
#define HEADER_SET(ptr, x, T) (*((T *)ptr - 1) = x)

#define STATIC_ASSERT_PASTE_(a, b) a##b
#define STATIC_ASSERT_PASTE(a, b) STATIC_ASSERT_PASTE_(a, b)

#define STRINGIFY(v) #v
#define STATIC_ASSERT(COND) typedef char STATIC_ASSERT_PASTE(static_assertion_failed_at_line_, __LINE__)[(COND) ? 1 : -1]

#define KB(bytes) ((uint64_t)(bytes) * 1000ULL)
#define MB(bytes) ((KB(bytes)) * 1000ULL)
#define GB(bytes) ((MB(bytes)) * 1000ULL)

#define KiB(bytes) ((uint64_t)(bytes) * 1024ULL)
#define MiB(bytes) ((KiB(bytes)) * 1024ULL)
#define GiB(bytes) ((MiB(bytes)) * 1024ULL)

static inline uint64_t alignup(uint64_t value, uint64_t alignment) {
	return ((value + (alignment - 1)) & ~(alignment - 1));
}
static inline uint64_t hash64(void *memory, size_t size) {
	uint64_t h = 0x100;
	for (size_t i = 0; i < size; i++) {
		h ^= ((uint8_t *)memory)[i];
		h *= 1111111111111111111;
	}
	return h;
}

// From https://stackoverflow.com/a/5889254
static inline uint64_t hash64_combine(uint64_t lhs, uint64_t rhs) {
	lhs ^= rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
	return lhs;
}

#define hash_struct(s) hash64(&(s), sizeof((s)))
#define hash_array(array) hash64((array), sizeof((array)))
#define hash_count(memory, count) hash64((void *)(memory), sizeof(*(memory)) * (count))

// Types
// clang-format off
typedef struct { float x, y; } float32x2;
typedef struct { float x, y, z; } float32x3;
typedef struct { float x, y, z, w; } float32x4;

typedef double float64;
typedef struct { float64 x, y; } float64x2;
typedef struct { float64 x, y, z; } float64x3;
typedef struct { float64 x, y, z, w; } float64x4;

typedef float64x2 double2;
typedef float64x3 double3;
typedef float64x4 double4;

typedef uint32_t uint32;
typedef struct { uint32 x, y; } uint32x2;
typedef struct { uint32 x, y, z; } uint32x3;
typedef struct { uint32 x, y, z, w; } uint32x4;

typedef uint64_t uint64;
typedef struct { uint64 x, y; } uint64x2;
typedef struct { uint64 x, y, z; } uint64x3;
typedef struct { uint64 x, y, z, w; } uint64x4;

typedef uint32x2 uint2;
typedef uint32x3 uint3;
typedef uint32x4 uint4;

typedef int32_t int32;
typedef struct { int32 x, y; } int32x2;
typedef struct { int32 x, y, z; } int32x3;
typedef struct { int32 x, y, z, w; } int32x4;

typedef int32x2 int2;
typedef int32x3 int3;
typedef int32x4 int4;

typedef struct { float min, max; } Interval;
typedef struct { float32x2 min, max; } Interval2;
typedef struct { float32x3 min, max; } Interval3;


#define rgba(r, g, b, a) (Color){r, g, b, a}
#define rgb(r, g, b) (Color){r, g, b, 255}

// clang-format on

typedef struct {
	uint8_t *pointer;
	size_t size;
} Buffer;

static inline Buffer buffer_make(void *ptr, size_t size) { return (Buffer){ .pointer = ptr, .size = size }; }

#define buffer_wrap_struct(obj) ((Buffer){ .pointer = (uint8_t *)&(obj), .size = sizeof(obj) })
#define buffer_wrap_array(arr) ((Buffer){ .pointer = (uint8_t *)(arr), .size = sizeof(arr) })
#define buffer_wrap_count(p, n) ((Buffer){ .pointer = (uint8_t *)(p), .size = sizeof(*(p)) * (n) })
#define buffer_wrap_literal(lit) ((Buffer){ .pointer = (uint8_t *)(lit), .size = sizeof(lit) - 1 })
#define buffer_wrap_string(s) ((Buffer){ .pointer = (uint8_t *)(s).chars, .size = (s).length })

static inline Buffer buffer_subbuffer(Buffer s, size_t offset, size_t length) {
	if (offset > s.size)
		return buffer_make(NULL, 0);
	if (offset + length > s.size)
		length = s.size - offset;
	return buffer_make(s.pointer + offset, length);
}
static inline bool buffer_empty(Buffer s) { return s.size == 0; }
static inline bool buffer_equal(Buffer a, Buffer b) {
	if (a.size != b.size)
		return false;
	return memory_equals(a.pointer, b.pointer, a.size);
}

typedef uint32_t Flag;
