#include "core.h"

#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include <fstream>

typedef struct {
	size_t page_size;
} SystemMemorysSpec;

static SystemMemorysSpec s_sys_mem_spec;

void query_system_memory_spec() {
	PROFILE_FUNCTION();
	if (s_sys_mem_spec.page_size != 0) {
		return;
	}

	SYSTEM_INFO sys_info = {};
	GetSystemInfo(&sys_info);

	s_sys_mem_spec.page_size = (size_t)sys_info.dwPageSize;
}

// NOTE: Assumes the sys mem spec had already been queried
inline size_t compute_page_count(size_t bytes) {
	return (bytes + s_sys_mem_spec.page_size - 1) / s_sys_mem_spec.page_size;
}

size_t align_to_page_size(size_t bytes) {
	return align(bytes, s_sys_mem_spec.page_size);
}

void arena_reserve(Arena& arena, size_t initial_size) {
	PROFILE_FUNCTION();

	assert(initial_size < arena.capacity);

	size_t aligned_allocation = align(initial_size, s_sys_mem_spec.page_size);

	arena.base = (uint8_t*)VirtualAlloc(NULL,
			(SIZE_T)align(arena.capacity, s_sys_mem_spec.page_size),
			MEM_RESERVE,
			PAGE_READWRITE);

	assert(arena.base != NULL);

	assert(VirtualAlloc(arena.base, (SIZE_T)aligned_allocation, MEM_COMMIT, PAGE_READWRITE) != NULL);

	arena.commited = aligned_allocation;
}

void arena_commit_page(Arena& arena, size_t page_count) {
	PROFILE_FUNCTION();

	size_t commit_size = page_count * s_sys_mem_spec.page_size;
	if (arena.commited + commit_size >= arena.capacity) {
		printf("Out of arena memory");
		assert(false);
	}

	void* result = VirtualAlloc(arena.base + arena.commited,
			(SIZE_T)commit_size,
			MEM_COMMIT,
			PAGE_READWRITE);

	assert(result != NULL);

	arena.commited += commit_size;
}

void* arena_alloc_aligned(Arena& arena, size_t size, size_t alignment) {
	size_t allocation_base = align(arena.allocated, alignment);
	size_t new_allocated_ptr = allocation_base + size;

	if (arena.base == NULL) {
		arena_reserve(arena, size);
	} else if (new_allocated_ptr > arena.commited) {
		arena_commit_page(arena, compute_page_count(new_allocated_ptr - arena.allocated));
	}

	void* allocation = arena.base + allocation_base;
	arena.allocated = new_allocated_ptr;
	return allocation;
}

void arena_release(Arena& arena) {
	PROFILE_FUNCTION();

	if (arena.base == NULL) {
		return;
	}

	assert(VirtualFree(arena.base, 0, MEM_RELEASE) && "Failed to free arena");

	arena.base = NULL;
	arena.allocated = 0;
	arena.commited = 0;
}

//
// String
//

wchar_t* cstring_to_wide(const char* string, Arena& arena) {
	PROFILE_FUNCTION();

	ArenaSavePoint temp = arena_begin_temp(arena);
	
	size_t string_length = strlen(string);
	wchar_t* buffer = arena_alloc_array<wchar_t>(arena, string_length + 1);

	size_t chars_converted = 0;
	errno_t error = mbstowcs_s(&chars_converted,
			buffer,
			string_length + 1,
			string,
			string_length);

	if (error == 0) {
		return buffer;
	}

	// delete the allocated buffer, because the convertion failed
	arena_end_temp(temp);
	return nullptr;
}

std::wstring_view string_to_wide(std::string_view string, Arena& arena) {
	PROFILE_FUNCTION();

	ArenaSavePoint temp = arena_begin_temp(arena);
	
	size_t string_length = string.length();

	// Allocate with one extra char for a null-terminator
	wchar_t* buffer = arena_alloc_array<wchar_t>(arena, string_length + 1);

	size_t chars_converted = 0;
	errno_t error = mbstowcs_s(&chars_converted,
			buffer,
			string_length + 1,
			string.data(),
			string_length);

	if (error == 0) {
		// Get rid of the null-terminator, by moving
		// the arena pointer back (deallocating it from the arena)
		arena.allocated -= sizeof(*buffer);
		return std::wstring_view(buffer, chars_converted - 1);
	}

	// delete the allocated buffer, because the convertion failed
	arena_end_temp(temp);
	return nullptr;
}

//
// File IO
//

bool read_text_file(const std::filesystem::path& path, Arena& arena, std::string_view* out_content) {
	PROFILE_FUNCTION();

	std::ifstream stream(path);
	if (!stream.is_open()) {
		return false;
	}

	stream.seekg(0, std::ios::end);
	size_t size = stream.tellg();
	stream.seekg(0, std::ios::beg);

	ArenaSavePoint temp = arena_begin_temp(arena);
	char* buffer = arena_alloc_array<char>(arena, size);

	stream.read(buffer, size);

	*out_content = std::string_view(buffer, size);

	return true;
}
