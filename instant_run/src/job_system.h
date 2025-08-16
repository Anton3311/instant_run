#pragma once

#include <stdint.h>

struct Arena;

struct JobContext {
	Arena& arena;
	uint32_t worker_index;
};

using JobSystemTask = void(*)(const JobContext& context, void* user_data);

void job_system_init(uint32_t worker_count);
uint32_t job_system_get_worker_count();

void job_system_submit(JobSystemTask task, void* user_data);

void job_system_wait_for_all(Arena& task_execution_allocator);

void job_system_shutdown();
