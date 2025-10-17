#pragma once

#include "core.h"

#include <stdint.h>

struct Arena;

struct JobContext {
	Arena& arena;
	Arena& temp_arena;
	size_t batch_size;
	uint32_t worker_index;
};

using JobSystemTask = void(*)(const JobContext& context, void* user_data);

void job_system_init(uint32_t worker_count);
uint32_t job_system_get_worker_count();

void job_system_submit(JobSystemTask task, void* user_data, size_t batch_size = 1);

template<typename T>
void job_system_submit_batches(JobSystemTask task, Span<T> data, size_t batch_size) {
	PROFILE_FUNCTION();
	size_t batch_count = (data.count + batch_size - 1) / batch_size;

	for (size_t i = 0; i < batch_count; i++) {
		size_t offset = i * batch_size;
		size_t size = min(batch_size, data.count - offset);

		job_system_submit(task, data.values + offset, size);
	}
}

void job_system_wait_for_all(Arena& arena, Arena& temp_arena);

void job_system_shutdown();
