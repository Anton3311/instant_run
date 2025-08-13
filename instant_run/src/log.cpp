#include "log.h"

#include "core.h"

#include <stdio.h>
#include <iostream>

struct LoggerState {
	FILE* file;
	bool output_to_stdout;
};

struct LoggerThreadState {
	Arena* arena;
	std::string_view name;
};

static LoggerState s_logger;
thread_local LoggerThreadState t_logger_thread;

bool log_init(const char* log_file_path, bool output_to_stdout) {
	PROFILE_FUNCTION();

	s_logger.output_to_stdout = output_to_stdout;

	errno_t error = fopen_s(&s_logger.file, log_file_path, "w");
	if (s_logger.file == nullptr || error == EINVAL) {
		fclose(s_logger.file);
		return false;
	}

	return true;
}

bool log_init_thread(Arena& arena, std::string_view thread_name) {
	t_logger_thread.arena = &arena;
	t_logger_thread.name = thread_name;

	return true;
}

void log_shutdown() {
	if (s_logger.file) {
		fclose(s_logger.file);
	}
}

void log_shutdown_thread() {
	t_logger_thread = {};
}

void log_message(std::string_view message, MessageType message_type) {
	PROFILE_FUNCTION();
	if (s_logger.file == nullptr && !s_logger.output_to_stdout) {
		return;
	}

	if (message.data()[message.length() - 1] == '\n') {
		message = message.substr(0, message.length() - 1);
	}

	ArenaSavePoint temp = arena_begin_temp(*t_logger_thread.arena);
	StringBuilder builder  = StringBuilder { .arena = t_logger_thread.arena };

	str_builder_append(builder, t_logger_thread.name);
	str_builder_append(builder, " ");

	switch (message_type) {
	case MessageType::Info:
		str_builder_append(builder, "[info] ");
		break;
	case MessageType::Error:
		str_builder_append(builder, "[error] ");
		break;
	case MessageType::Warn:
		str_builder_append(builder, "[warn] ");
		break;
	}

	str_builder_append(builder, message);
	str_builder_append(builder, "\n");

	fwrite(builder.string, sizeof(*builder.string), builder.length, s_logger.file);

	if (s_logger.output_to_stdout) {
		std::cout << std::string_view(builder.string, builder.length);
	}

	arena_end_temp(temp);
}

