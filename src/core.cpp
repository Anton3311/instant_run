#include "core.h"

#include <windows.h>

typedef struct {
	size_t page_size;
} SystemMemorysSpec;

static SystemMemorysSpec s_sys_mem_spec;

void query_system_memory_spec() {
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
	size_t commit_size = page_count * s_sys_mem_spec.page_size;
	if (arena.commited + page_count >= arena.capacity) {
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
	if (arena.base == NULL) {
		return;
	}

	assert(VirtualFree(arena.base, 0, MEM_RELEASE) && "Failed to free arena");

	arena.base = NULL;
	arena.allocated = 0;
	arena.commited = 0;
}

