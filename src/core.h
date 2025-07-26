#pragma once

#include <stdint.h>

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

struct Vec2;

struct UVec2 {
	inline operator Vec2();

	uint32_t x;
	uint32_t y;
};

struct Vec2 {
	inline Vec2& operator+=(Vec2 other) {
		x += other.x;
		y += other.y;
		return *this;
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

//
// Rect
//

struct Rect {
	Vec2 min;
	Vec2 max;
};

inline bool rect_contains_point(const Rect& rect, Vec2 point) {
	return point.x >= rect.min.x && point.y >= rect.min.y
		&& point.x <= rect.max.x && point.y <= rect.max.y;
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
