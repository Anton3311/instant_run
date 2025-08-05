clang++ ^
	src\main.cpp ^
	src\core.cpp ^
	src\platform.cpp ^
	src\renderer.cpp ^
	src\ui.cpp ^
	-Ivendor\GLAD\include ^
	-Ivendor\stb_truetype ^
	-Ivendor\stb_image ^
	-Ivendor\Tracy ^
	-o bin\debug_win_x86_64\instant_run.exe ^
	bin_int\debug_win_x86_64\glad.o ^
	bin_int\debug_win_x86_64\stb_truetype.o ^
	bin_int\debug_win_x86_64\stb_image.o ^
	vendor\Tracy\TracyClient.cpp ^
	-std=c++20 -m64 -lgdi32 -lopengl32 -luser32 -ldwmapi -lshell32 -lole32 -g -DUNICODE -D_UNICODE -DTRACY_ENABLE
