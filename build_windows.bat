echo "Finding Visual Studio compiler install path and configuring build for x64 mode"
for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
echo LIB=%LIB%

cl.exe /permissive- /W3 /Gy /Zc:inline /D "NDEBUG" /D "_CONSOLE" /D "_UNICODE" /D "UNICODE" /Oi /O2 /MT /FC /EHsc /nologo mini_video_dx12.cpp /link user32.lib kernel32.lib

cl.exe /permissive- /W3 /Gy /Zc:inline /D "NDEBUG" /D "_CONSOLE" /D "_UNICODE" /D "UNICODE" /Oi /O2 /MT /FC /EHsc /nologo mini_video_vulkan.cpp /link user32.lib kernel32.lib

cl.exe /permissive- /W3 /Gy /Zc:inline /D "NDEBUG" /D "_CONSOLE" /D "_UNICODE" /D "UNICODE" /Oi /O2 /MT /FC /EHsc /nologo mini_video_dx11.cpp /link user32.lib kernel32.lib
