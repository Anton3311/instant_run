#include "job_system.h"

#include "core.h"
#include "log.h"
#include "platform.h"

#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <string>
#include <condition_variable>

struct Task {
	JobSystemTask task_func;
	void* user_data;
	size_t batch_size;
};

struct JobSystemState {
	std::vector<std::thread> worker_threads;

	std::atomic_bool is_running;
	std::atomic_uint32_t active_worker_count;

	std::mutex wake_mutex;
	std::condition_variable wake_var;

	// Task queue
	std::mutex queue_mutex;
	std::queue<Task> task_queue;
};

static JobSystemState s_job_sys_state;

static bool try_pop_task(Task* out_task) {
	PROFILE_FUNCTION();

	std::unique_lock lock(s_job_sys_state.queue_mutex);
	if (s_job_sys_state.task_queue.size() == 0) {
		return false;
	}

	*out_task = s_job_sys_state.task_queue.front();
	s_job_sys_state.task_queue.pop();

	return true;
}

static bool try_execute_single_task(JobContext& context) {
	PROFILE_FUNCTION();

	Task task{};
	bool has_task = try_pop_task(&task);

	if (!has_task) {
		return false;
	}

	s_job_sys_state.active_worker_count.fetch_add(1, std::memory_order::acquire);

	{
		context.batch_size = task.batch_size;
		task.task_func(context, task.user_data);
	}

	s_job_sys_state.active_worker_count.fetch_sub(1, std::memory_order::acquire);

	return true;
}

static void thread_worker(uint32_t index) {
	Arena logger_arena{};
	logger_arena.capacity = kb_to_bytes(4);

	Arena generic_arena{};
	generic_arena.capacity = mb_to_bytes(8);

	{
		std::string thread_name = std::string("worker") + std::to_string(index);

		log_init_thread(logger_arena, thread_name);
		PROFILE_NAME_THREAD(thread_name.c_str());
	}

	log_info("worker started");

	platform_initialize_thread();
	platform_set_this_thread_affinity_mask(1 << index);

	while (true) {
		bool is_running = s_job_sys_state.is_running.load(std::memory_order::relaxed);

		if (!is_running) {
			break;
		}

		JobContext context = { .arena = generic_arena, .worker_index = index };
		bool has_task = try_execute_single_task(context);

		if (!has_task) {
			log_info("task queue is empty");

			std::unique_lock lock(s_job_sys_state.wake_mutex);
			s_job_sys_state.wake_var.wait(lock);
		}
	}
	
	log_info("worker stopped");

	platform_shutdown_thread();

	log_shutdown_thread();

	arena_release(logger_arena);
}

void job_system_init(uint32_t worker_count) {
	PROFILE_FUNCTION();
	
	s_job_sys_state.is_running.store(true, std::memory_order::acquire);

	for (uint32_t i = 0; i < worker_count; i++) {
		s_job_sys_state.worker_threads.push_back(std::thread(thread_worker, i));
	}

	log_info("job system initialized");
}

uint32_t job_system_get_worker_count() {
	return (uint32_t)s_job_sys_state.worker_threads.size();
}

void job_system_submit(JobSystemTask task, void* user_data, size_t batch_size) {
	PROFILE_FUNCTION();

	{
		PROFILE_SCOPE("append_task");
		std::unique_lock lock(s_job_sys_state.queue_mutex);
		s_job_sys_state.task_queue.push(Task { .task_func = task, .user_data = user_data, .batch_size = batch_size });
	}

	s_job_sys_state.wake_var.notify_one();
}

void job_system_wait_for_all(Arena& task_execution_allocator) {
	PROFILE_FUNCTION();

	JobContext context = { .arena = task_execution_allocator, .worker_index = job_system_get_worker_count() };
	while (true) {
		if (!try_execute_single_task(context)) {
			break;
		}
	}

	{
		PROFILE_SCOPE("wait_idle");
		while (true) {
			uint32_t active_worker_count = s_job_sys_state.active_worker_count.load(std::memory_order::relaxed);
			if (active_worker_count == 0) {
				break;
			} else {
				std::this_thread::yield();
			}
		}
	}
}

void job_system_shutdown() {
	PROFILE_FUNCTION();

	s_job_sys_state.is_running.store(false, std::memory_order::acquire);
	s_job_sys_state.wake_var.notify_all();
	
	for (auto& worker : s_job_sys_state.worker_threads) {
		worker.join();
	}

	s_job_sys_state.worker_threads.clear();

	log_info("job system shutdown");
}
