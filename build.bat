set CMD_ARGS=-m64
set PLATFORM=win
set ARCH=x86_64
set APP_NAME=instant_run
set SRC=instant_run\src

set FILES=%SRC%\main.cpp %SRC%\core.cpp %SRC%\platform.cpp %SRC%\renderer.cpp %SRC%\ui.cpp %SRC%\log.cpp

if [%1] == [release] (
	set CMD_ARGS=%CMD_ARGS% -DTRACY_ENABLE -DENABLE_PROFILING -O3
	set BIN_DIR=bin\release_%PLATFORM%_%ARCH%
	set BIN_INT_DIR=bin_int\release_%PLATFORM%_%ARCH%

	set FILES=%FILES% vendor\Tracy\TracyClient.cpp
) else (
	set BIN_DIR=bin\debug_%PLATFORM%_%ARCH%
	set BIN_INT_DIR=bin_int\debug_%PLATFORM%_%ARCH%
)

if not exist %BIN_DIR% mkdir %BIN_DIR%
if not exist %BIN_INT_DIR% mkdir %BIN_INT_DIR%

clang++ ^
	%FILES% ^
	-Ivendor\GLAD\include ^
	-Ivendor\stb_truetype ^
	-Ivendor\stb_image ^
	-Ivendor\Tracy ^
	-Iinstant_run_hook\src ^
	-o %BIN_DIR%\%APP_NAME%.exe ^
	%BIN_INT_DIR%\glad.o ^
	%BIN_INT_DIR%\stb_truetype.o ^
	%BIN_INT_DIR%\stb_image.o ^
	-std=c++20 -m64 -lgdi32 -lopengl32 -luser32 -ldwmapi -lshell32 -lole32 -lAdvapi32.lib -g -DUNICODE -D_UNICODE %CMD_ARGS%
