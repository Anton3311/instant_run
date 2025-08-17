set CMD_ARGS=-m64
set PLATFORM=win
set ARCH=x86_64
set APP_NAME=instant_run
set SRC=instant_run\src

set FILES=%SRC%\app.cpp %SRC%\main.cpp %SRC%\core.cpp %SRC%\platform.cpp %SRC%\renderer.cpp %SRC%\ui.cpp %SRC%\log.cpp %SRC%\job_system.cpp

if [%1] == [release] (
	set CMD_ARGS=%CMD_ARGS% -O3 -DWINDOWS_SUBSYSTEM -DBUILD_RELEASE
	set BIN_DIR=bin\release_%PLATFORM%_%ARCH%

	:: BIN_INT_DIR is for precompiled libs, they are the same for 'release' and 'profiling', because they don't use tracy.
	set BIN_INT_DIR=bin_int\release_%PLATFORM%_%ARCH%
) else if [%1] == [profiling] (
	set CMD_ARGS=%CMD_ARGS% -DTRACY_ENABLE -DENABLE_PROFILING -O3 -g -DBUILD_PROFILING
	set BIN_DIR=bin\profiling_%PLATFORM%_%ARCH%

	set BIN_INT_DIR=bin_int\release_%PLATFORM%_%ARCH%
	set FILES=%FILES% vendor\Tracy\TracyClient.cpp
) else (
	set CMD_ARGS=%CMD_ARGS% -g -DBUILD_DEBUG

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
	-std=c++20 -lgdi32 -lopengl32 -luser32 -ldwmapi -lshell32 -lole32 -loleaut32.lib -lwindowsapp.lib -lShlwapi.lib -lAdvapi32.lib -DUNICODE -D_UNICODE %CMD_ARGS%
