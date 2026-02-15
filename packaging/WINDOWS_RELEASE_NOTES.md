# Windows Release Notes

## System Requirements

- **Operating System**: Windows 10 (64-bit) or newer
- **Graphics**: OpenGL 3.3 compatible graphics card
- **Memory**: 512 MB RAM minimum

## Installation

1. Extract the ZIP archive to a folder of your choice
2. Run `ntrak.exe`

## Troubleshooting

### Application won't start / Crashes on startup

**Check Graphics Drivers:**
- Update your graphics card drivers to the latest version
- NVIDIA: https://www.nvidia.com/Download/index.aspx
- AMD: https://www.amd.com/en/support
- Intel: https://www.intel.com/content/www/us/en/download-center/home.html

**Try running in compatibility mode:**
1. Right-click `ntrak.exe`
2. Select "Properties"
3. Go to "Compatibility" tab
4. Check "Run this program in compatibility mode for:"
5. Select "Windows 8"
6. Click "Apply" and try running again

### Missing DLL errors

If you see errors about missing DLL files, try:
1. Install the latest Visual C++ Redistributable:
   - Download from: https://aka.ms/vs/17/release/vc_redist.x64.exe
2. Restart your computer after installation

### OpenGL errors

If you get OpenGL-related errors:
1. Update your graphics drivers (see above)
2. Check if your graphics card supports OpenGL 3.3 or higher
3. Try running on integrated graphics (if you have both dedicated and integrated)

### Permission errors

If the app can't save files:
1. Don't run from Program Files or other protected directories
2. Extract to a user folder like Documents or Desktop
3. Run as administrator (right-click → "Run as administrator")

## Known Issues

- Some older Intel HD Graphics chipsets may have OpenGL compatibility issues
- High-DPI displays may show UI scaling issues on Windows 10 versions before 1903

## Reporting Bugs

Please report issues at: https://github.com/[your-repo]/ntrak/issues

Include:
- Windows version (Win+R, type `winver`, press Enter)
- Graphics card model
- Error messages or crash logs
- Steps to reproduce the problem
