@echo off
cd /d "c:\Users\fg906\Downloads\paimbnails-2.4.0\Paimbnails 2.4.0"
"C:\Program Files\CMake\bin\cmake.EXE" --build build --config Debug --target PaimonThumbnails -j 12
echo.
echo BUILD EXIT CODE: %ERRORLEVEL%
