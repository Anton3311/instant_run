#include "app.h"

#include "windows.h"

#if defined(WINDOWS_SUBSYSTEM)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
#else
int main() {
#endif
	int arg_count = 0;
	wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &arg_count);

	CommandLineArgs cmd_args{};
	cmd_args.arguments = arguments;
	cmd_args.count = (size_t)arg_count;

	return run_app(cmd_args);
}
