param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $VsWhere)) { throw 'Install Visual Studio with Desktop development with C++ and a Windows 10/11 SDK.' }
$VsPath = & $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$VsPath) { throw 'Visual Studio C++ x64 tools were not found.' }
$VsMajor = (& $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion).Split('.')[0]
$Generator = if ([int]$VsMajor -ge 18) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
$MsBuild = Join-Path $VsPath 'MSBuild\Current\Bin\MSBuild.exe'
Push-Location $ProjectRoot
try {
    & cmake -S . -B build/core -G $Generator -A x64
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    & cmake --build build/core --config $Configuration -j 8
    if ($LASTEXITCODE) { throw 'Core build failed.' }
    & $MsBuild src/winui/WinHelper.vcxproj /restore "/p:Configuration=$Configuration" /p:Platform=x64 /m:6 /v:minimal /fl '/flp:logfile=build/winui-build.log;verbosity=normal'
    if ($LASTEXITCODE) { throw 'WinUI build failed. See build/winui-build.log.' }
    if (!$SkipTests) {
        & ctest --test-dir build/core -C $Configuration --output-on-failure
        if ($LASTEXITCODE) { throw 'Core tests failed.' }
    }
    Write-Host "WinUI: $ProjectRoot\build\winui\$Configuration\qemu-3dfx-winhelper-ui.exe"
    Write-Host "Launcher: $ProjectRoot\build\core\$Configuration\qemu-3dfx-winhelper.exe"
} finally { Pop-Location }
