@echo off
echo ===================================
echo ntrak System Diagnostic Tool
echo ===================================
echo.

echo Checking system information...
echo.

echo Windows Version:
ver
echo.

echo Checking OpenGL support...
echo (This requires opening a temporary window)
echo.

REM Try to get GPU info using wmic
echo Graphics Card:
wmic path win32_VideoController get name
echo.

echo DirectX Version:
dxdiag /t dxdiag_output.txt
timeout /t 3 /nobreak >nul
if exist dxdiag_output.txt (
    findstr /C:"DirectX Version" dxdiag_output.txt
    del dxdiag_output.txt
)
echo.

echo Checking if assets folder exists...
if exist "assets\" (
    echo [OK] Assets folder found
) else (
    echo [ERROR] Assets folder NOT found! This will cause ntrak to fail.
    echo Please ensure assets folder is in the same directory as ntrak.exe
)
echo.

echo Checking if config folder exists...
if exist "config\" (
    echo [OK] Config folder found
) else (
    echo [ERROR] Config folder NOT found!
)
echo.

echo Checking Visual C++ Runtime...
echo (Checking registry for installed redistributables)
reg query "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" /v Version >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Visual C++ 2015-2022 Redistributable found
) else (
    echo [WARNING] Visual C++ redistributable may not be installed
    echo Download from: https://aka.ms/vs/17/release/vc_redist.x64.exe
)
echo.

echo ===================================
echo Diagnostic complete!
echo.
echo If ntrak still doesn't work, please check ntrak_debug.log for details.
echo ===================================
pause
