#pragma once

#include <stdint.h>
#include <string_view>

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

inline std::wstring_view arena_push_string(Arena& arena, std::wstring_view string) {
	wchar_t* copy = arena_alloc_array<wchar_t>(arena, string.length());
	std::memcpy(copy, string.data(), sizeof(wchar_t) * string.length());

	return std::wstring_view(copy, string.length());
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

//
// String Builder
//

struct StringBuilder {
	Arena* arena;
	const char* string;
	size_t length;
};

inline void str_builder_append(StringBuilder& builder, std::string_view string) {
	char* buffer = arena_alloc_array<char>(*builder.arena, string.length());
	memcpy(buffer, string.data(), sizeof(char) * string.length());

	builder.length += string.length();

	if (!builder.string) {
		builder.string = buffer;
	}
}

inline std::string_view str_builder_to_str(const StringBuilder& builder) {
	return std::string_view(builder.string, builder.length);
}

//
// File IO
//

Span<uint8_t> file_read_all_bytes(const char* path, Arena& allocator);
