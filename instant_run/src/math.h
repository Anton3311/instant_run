#pragma once

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


