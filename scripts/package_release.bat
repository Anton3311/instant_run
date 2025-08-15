set OUT_DIR=%1
set PLATFORM=win
set ARCH=x86_64
set APP_NAME=instant_run

if not exist %OUT_DIR% mkdir %OUT_DIR%

set BIN_DIR=bin\release_%PLATFORM%_%ARCH%

copy %BIN_DIR%\%APP_NAME%.exe %OUT_DIR%\%APP_NAME%.exe
copy %BIN_DIR%\%APP_NAME%.dll %OUT_DIR%\%APP_NAME%.dll
robocopy "assets" %OUT_DIR%\assets /E
