$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio with Desktop development with C++ / 请安装 Visual Studio 的 C++ 桌面开发组件。' }
$toolset = (Get-ChildItem "$vs/VC/Tools/MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdk = "${env:ProgramFiles(x86)}/Windows Kits/10"
$version = (Get-ChildItem "$sdk/Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$env:INCLUDE = "$toolset/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um;$sdk/Include/$version/winrt"
$env:LIB = "$toolset/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
$env:PATH = "$toolset/bin/Hostx64/x64;$sdk/bin/$version/x64;$env:PATH"
New-Item -ItemType Directory -Path "$root/build" -Force | Out-Null
New-Item -ItemType Directory -Path "$root/out/obj" -Force | Out-Null
Push-Location "$root/build"
try {
    & "$toolset/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /O2 /MT /W4 /utf-8 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX "$root/src/main.cpp" "/Fo:$root/out/obj/main.obj" /Fe:Silkstride.exe /link /SUBSYSTEM:WINDOWS gdiplus.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib uuid.lib uiautomationcore.lib advapi32.lib /MANIFEST:EMBED "/MANIFESTINPUT:$root/app.manifest"
    if ($LASTEXITCODE -ne 0) { throw "C++ build failed / 编译失败: $LASTEXITCODE" }
} finally { Pop-Location }
Write-Host "Built / 已生成 $root/build/Silkstride.exe"
