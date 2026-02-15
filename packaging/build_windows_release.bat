@echo off
REM Build script for Windows release

echo Building ntrak for Windows...

REM Create build directory
if not exist build-windows-release mkdir build-windows-release
cd build-windows-release

REM Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF

REM Build
cmake --build . --config Release -j

REM Copy to distribution folder
if not exist ..\dist mkdir ..\dist
if not exist ..\dist\ntrak-windows mkdir ..\dist\ntrak-windows

REM Copy executable and data
xcopy /Y /I bin\Release\ntrak.exe ..\dist\ntrak-windows\
xcopy /E /Y /I bin\Release\assets ..\dist\ntrak-windows\assets
xcopy /E /Y /I bin\Release\config ..\dist\ntrak-windows\config
xcopy /E /Y /I bin\Release\examples ..\dist\ntrak-windows\examples
xcopy /E /Y /I bin\Release\docs ..\dist\ntrak-windows\docs

echo.
echo Build complete! Output in dist\ntrak-windows\
echo.
echo IMPORTANT: Please test on a clean Windows 10 system without Visual Studio installed
echo to verify all dependencies are included.
pause
