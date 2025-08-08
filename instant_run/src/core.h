#pragma once

#include <stdint.h>

#ifdef ENABLE_PROFILING 
#include <tracy/Tracy.hpp>

#define PROFILE_SCOPE(name) ZoneScopedN(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCSIG__)

#define PROFILE_BEGIN_FRAME(name) FrameMarkStart(name)
#define PROFILE_END_FRAME(name) FrameMarkEnd(name)

#define PROFILE_NAME_THREAD(name) tracy::SetThreadName(name)
#else
#define PROFILE_SCOPE(name)
#define PROFILE_FUNCTION()

#define PROFILE_BEGIN_FRAME(name)
#define PROFILE_END_FRAME(name)

#define PROFILE_NAME_THREAD(name)
#endif

#define HAS_FLAG(flag_set, flag) (((flag_set) & (flag)) == (flag))
#define HAS_ANY_FLAG(flag_set, flag) (((flag_set) & (flag)) != 0)

#define ENUM_TYPE(enum_name) std::underlying_type_t<enum_name>
#define IMPL_ENUM_FLAGS(enum_name) \
	constexpr enum_name operator&(enum_name a, enum_name b) \
		{ return (enum_name)((ENUM_TYPE(enum_name))a & (ENUM_TYPE(enum_name))b); } \
	constexpr enum_name operator|(enum_name a, enum_name b) \
		{ return (enum_name)((ENUM_TYPE(enum_name))a | (ENUM_TYPE(enum_name))b); } \
	constexpr bool operator==(enum_name a, int b) { return (int)a == b; } \
	constexpr bool operator!=(enum_name a, int b) { return (int)a != b; } \
	constexpr enum_name operator~(enum_name a) \
		{ return (enum_name)(~(ENUM_TYPE(enum_name))a); }

struct Color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

static constexpr Color BLACK = Color { 0, 0, 0, 255 };
static constexpr Color WHITE = Color { 255, 255, 255, 255 };
static constexpr Color TRANSPARENT = Color { 0, 0, 0, 0 };

inline uint32_t color_to_uint32(Color color) {
	return (static_cast<uint32_t>(color.r) << 24)
		| (static_cast<uint32_t>(color.g) << 16)
		| (static_cast<uint32_t>(color.b) << 8)
		| (static_cast<uint32_t>(color.a));
}

inline Color color_from_hex(uint32_t hex) {
	uint8_t r = static_cast<uint8_t>(hex >> 24);
	uint8_t g = static_cast<uint8_t>(hex >> 16);
	uint8_t b = static_cast<uint8_t>(hex >> 8);
	uint8_t a = static_cast<uint8_t>(hex);

	return Color { r, g, b, a };
}

struct Vec2;

struct UVec2 {
	inline operator Vec2();

	inline bool operator==(UVec2 other) const {
		return x == other.x && y == other.y;
	}

	inline bool operator!=(UVec2 other) const {
		return x != other.x || y != other.y;
	}

	uint32_t x;
	uint32_t y;
};

struct Vec2 {
	inline Vec2& operator+=(Vec2 other) {
		x += other.x;
		y += other.y;
		return *this;
	}

	inline Vec2& operator-=(Vec2 other) {
		x -= other.x;
		y -= other.y;
		return *this;
	}

	inline bool operator==(Vec2 other) const {
		return x == other.x && y == other.y;
	}

	inline bool operator!=(Vec2 other) const {
		return x != other.x || y != other.y;
	}

	inline Vec2 operator-() const {
		return Vec2 { -x, -y };
	}

	float x;
	float y;
};

//
// UVec2
//

inline UVec2::operator Vec2() {
	return Vec2 { static_cast<float>(x), static_cast<float>(y) };
}

inline Vec2 operator+(Vec2 a, Vec2 b) {
	return Vec2 { a.x + b.x, a.y + b.y };
}

inline Vec2 operator-(Vec2 a, Vec2 b) {
	return Vec2 { a.x - b.x, a.y - b.y };
}

inline Vec2 operator*(Vec2 a, float scalar) {
	return Vec2 { a.x * scalar, a.y * scalar };
}

inline float min(float a, float b) {
	return a < b ? a : b;
}

inline float max(float a, float b) {
	return a < b ? b : a;
}

inline Vec2 min(Vec2 a, Vec2 b) {
	return Vec2 { min(a.x, b.x), min(a.y, b.y) };
}

inline Vec2 max(Vec2 a, Vec2 b) {
	return Vec2 { max(a.x, b.x), max(a.y, b.y) };
}

//
// Rect
//

struct Rect {
	inline float width() const { return max.x - min.x; }
	inline float height() const { return max.y - min.y; }
	inline Vec2 size() const { return max - min; }
	inline Vec2 center() const { return (min + max) * 0.5f; }

	Vec2 min;
	Vec2 max;
};

inline bool rect_contains_point(const Rect& rect, Vec2 point) {
	return point.x >= rect.min.x && point.y >= rect.min.y
		&& point.x <= rect.max.x && point.y <= rect.max.y;
}

inline Rect combine_rects(Rect a, Rect b) {
	return Rect {
		.min = min(a.min, b.min),
		.max = max(a.max, b.max)
	};
}

//
// Span
//

template<typename T>
struct Span {
	Span() = default;
	Span(T* values, size_t count)
		: values(values), count(count) {}

	inline T& operator[](size_t index) {
		if (index >= count) {
			__debugbreak(); // TODO: assert
		}

		return values[index];
	}

	inline const T& operator[](size_t index) const {
		if (index >= count) {
			__debugbreak(); // TODO: assert
		}

		return values[index];
	}

	inline T* begin() { return values; }
	inline T* end() { return values + count; }

	inline const T* begin() const { return values; }
	inline const T* end() const { return values + count; }

	T* values;
	size_t count;
};

//
// Range
//

struct RangeU32 {
	uint32_t start;
	uint32_t count;
};

//
// Arena
//

inline size_t align(size_t value, size_t alignment) {
	return (value + alignment - 1) / alignment * alignment;
}

struct Arena {
	size_t capacity;
	size_t commited;
	size_t allocated;
	uint8_t* base;
};
void query_system_memory_spec();
void* arena_alloc_aligned(Arena& arena, size_t size, size_t alignment);

inline void arena_reset(Arena& arena) {
	arena.allocated = 0;
}

template<typename T>
inline T* arena_alloc(Arena& arena) {
	return reinterpret_cast<T*>(arena_alloc_aligned(arena, sizeof(T), alignof(T)));
}

template<typename T>
inline T* arena_alloc_array(Arena& arena, size_t count) {
	return reinterpret_cast<T*>(arena_alloc_aligned(arena, sizeof(T) * count, alignof(T)));
}

void arena_release(Arena& arena);

struct ArenaSavePoint {
	Arena* arena;
	size_t allocated_state;
};

inline ArenaSavePoint arena_begin_temp(Arena& arena) {
	return ArenaSavePoint { .arena = &arena, .allocated_state = arena.allocated };
}

inline void arena_end_temp(ArenaSavePoint save_point) {
	save_point.arena->allocated = save_point.allocated_state;
}

constexpr size_t kb_to_bytes(size_t kb) { return kb * 1024; }
constexpr size_t mb_to_bytes(size_t mb) { return kb_to_bytes(mb * 1024); }
