========================================
ntrak - SNES Music Tracker
========================================

Thank you for downloading ntrak!

QUICK START
-----------
1. Extract all files to a folder of your choice
2. Double-click ntrak.exe to run

IMPORTANT: Keep all files together in the same folder, especially:
- assets/ (contains fonts and resources)
- config/ (contains engine configurations)
- examples/ (sample projects)

SYSTEM REQUIREMENTS
-------------------
- Windows 10 (64-bit) or newer
- Graphics card with OpenGL 3.3 support
- 512 MB RAM minimum

========================================
TROUBLESHOOTING
========================================

If ntrak crashes or closes immediately:

STEP 1: Check the Debug Log
----------------------------
A debug log is automatically created when ntrak runs:
- Look for ntrak_debug.log in this folder
- Open it with Notepad to see what failed
- This tells you exactly where initialization failed

STEP 2: Run the Diagnostic Tool
--------------------------------
Double-click check_system_windows.bat to check:
- Windows version
- Graphics card information
- Whether required folders are present
- Visual C++ runtime installation

STEP 3: Common Solutions
-------------------------

Graphics Driver Issues (most common):
- Update your graphics card drivers to the latest version
- NVIDIA: https://www.nvidia.com/Download/index.aspx
- AMD: https://www.amd.com/en/support
- Intel: https://www.intel.com/content/www/us/en/download-center/home.html

Missing Assets:
- Make sure assets/, config/, and examples/ folders exist
- They must be in the same directory as ntrak.exe
- Don't move or rename these folders

OpenGL Not Supported:
- Your GPU must support OpenGL 3.3 or higher
- Update graphics drivers first
- If you have both integrated and dedicated graphics, try:
  Right-click ntrak.exe → Run with graphics processor →
  Try the other GPU option

Missing DLL Errors:
- Install Visual C++ Redistributable (usually not needed):
  https://aka.ms/vs/17/release/vc_redist.x64.exe
- Restart your computer after installation

Permission Errors:
- Don't run from Program Files or other protected directories
- Extract to Documents, Desktop, or another user folder
- Try running as administrator: right-click ntrak.exe →
  "Run as administrator"

Compatibility Mode:
If all else fails, try Windows 8 compatibility:
1. Right-click ntrak.exe → Properties
2. Go to Compatibility tab
3. Check "Run this program in compatibility mode for:"
4. Select "Windows 8"
5. Click Apply and try running again

========================================
KNOWN ISSUES
========================================

- Older Intel HD Graphics chipsets (pre-HD 4000) may not
  support OpenGL 3.3
- High-DPI displays may have UI scaling issues on Windows 10
  versions before 1903

========================================
GETTING HELP
========================================

User Guide:
See docs/USER_GUIDE.md for complete documentation

Bug Reports:
https://github.com/[your-repo]/ntrak/issues

When reporting issues, please include:
- Windows version (Win+R, type "winver", press Enter)
- Graphics card model (from check_system_windows.bat)
- Contents of ntrak_debug.log
- Error messages or screenshots
- Steps to reproduce the problem

========================================
