#pragma once

#include <string_view>

struct Arena;

bool log_init(const char* log_file_path, bool output_to_stdout);
bool log_init_thread(Arena& arena, std::string_view thread_name);

void log_shutdown();
void log_shutdown_thread();

enum class MessageType {
	Info,
	Error,
	Warn,
};

void log_message(std::string_view message, MessageType message_type);

inline void log_info(std::string_view message) {
	log_message(message, MessageType::Info);
}

inline void log_error(std::string_view message){ 
	log_message(message, MessageType::Error);
}

inline void log_warn(std::string_view message){ 
	log_message(message, MessageType::Warn);
}
