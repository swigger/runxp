param([string]$Destination = '', [switch]$IncludeToolsIso, [string]$Archive = '')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
if (!$Destination) { $Destination = Join-Path $ProjectRoot 'dist\qemu-3dfx-winhelper' }
$Destination = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $Destination) { throw 'Destination already exists. Choose a new empty path; this script never deletes an existing package or VM.' }
$GuiBuild = Join-Path $ProjectRoot 'build\winui\Release'
$Cli = Join-Path $ProjectRoot 'build\core\Release\qemu-helper-cli.exe'
$Launcher = Join-Path $ProjectRoot 'build\core\Release\qemu-3dfx-winhelper.exe'
if (!(Test-Path -LiteralPath "$GuiBuild\qemu-3dfx-winhelper-ui.exe") -or !(Test-Path -LiteralPath $Cli) -or !(Test-Path -LiteralPath $Launcher)) { throw 'Run tools\build.ps1 first.' }
if ($Archive -and (Test-Path -LiteralPath $Archive)) { throw 'Archive already exists. Choose a new archive path.' }
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$VsPath = & $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$RedistRoot = Join-Path $VsPath 'VC\Redist\MSVC'
$Redist = Get-ChildItem -LiteralPath $RedistRoot -Directory | Where-Object { $_.Name -match '^\d+\.' } | Sort-Object { [Version]$_.Name } -Descending | Select-Object -First 1
$Crt = Get-ChildItem -LiteralPath (Join-Path $Redist.FullName 'x64') -Directory | Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } | Select-Object -First 1
if (!$Crt) { throw 'Cannot find the app-local Visual C++ runtime.' }
New-Item -ItemType Directory -Path $Destination | Out-Null
$UiDestination = Join-Path $Destination 'bin\winui'
New-Item -ItemType Directory -Path $UiDestination,(Join-Path $Destination 'machines') | Out-Null
# Preserve the tested WinUI component closure; omit development artifacts.
foreach ($File in (Get-ChildItem -LiteralPath $GuiBuild -File -Recurse)) {
    if ($File.Extension -in '.pdb','.ilk','.lib','.exp') { continue }
    if ($File.Name -like 'qemu-3dfx-winhelper.exe*') { continue } # Pre-0.2 build residue.
    $Relative = [IO.Path]::GetRelativePath($GuiBuild, $File.FullName)
    $Target = Join-Path $UiDestination $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path $Target -Parent) | Out-Null
    Copy-Item -LiteralPath $File.FullName -Destination $Target
}
Copy-Item -LiteralPath $Launcher -Destination $Destination
Get-ChildItem -LiteralPath $Crt.FullName -Filter '*.dll' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $UiDestination }
# Audit stages into its own fresh folder; merge the result into bin without
# mixing QEMU DLLs with WinUI DLLs in the child application's search directory.
$QemuStage = Join-Path $Destination '.qemu-stage'
& python (Join-Path $PSScriptRoot 'audit-runtime.py') --source (Join-Path $ProjectRoot 'runtime-binary') --stage $QemuStage --output (Join-Path $Destination 'bin\runtime-manifest.json') | Out-Null
if ($LASTEXITCODE) { throw 'Runtime audit/staging failed. The partial package is retained for inspection.' }
$ResolvedStage = (Resolve-Path -LiteralPath $QemuStage).Path
if (!$ResolvedStage.StartsWith($Destination + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Staging boundary check failed.' }
Get-ChildItem -LiteralPath $ResolvedStage | ForEach-Object { Move-Item -LiteralPath $_.FullName -Destination (Join-Path $Destination 'bin') }
Remove-Item -LiteralPath $ResolvedStage # Empty, owned staging directory only.
Copy-Item -LiteralPath $Cli -Destination (Join-Path $Destination 'bin')
if ($IncludeToolsIso) { Copy-Item -LiteralPath (Join-Path $ProjectRoot 'guest-tools.iso') -Destination $Destination }
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'docs\使用说明.txt') -Destination $Destination
New-Item -ItemType Directory -Force -Path (Join-Path $Destination 'bin\licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'third_party\nlohmann\LICENSE.MIT') -Destination (Join-Path $Destination 'bin\licenses\nlohmann-json.txt')
$NugetRoot = if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else { Join-Path $env:USERPROFILE '.nuget\packages' }
$WinUiLicense = Join-Path $NugetRoot 'microsoft.windowsappsdk.winui\1.8.260803003\license.txt'
Copy-Item -LiteralPath $WinUiLicense -Destination (Join-Path $Destination 'bin\licenses\WindowsAppSDK.txt')
if (Test-Path -LiteralPath (Join-Path $ProjectRoot 'licenses')) {
    Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'licenses') -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Destination 'bin\licenses') }
}
# Deployment is the publisher's responsibility; do this before producing a ZIP.
$Checks = & (Join-Path $Destination 'bin\qemu-helper-cli.exe') $Destination diagnose
if ($LASTEXITCODE) { throw "Packaged runtime validation failed: $Checks" }
Write-Host 'Packaged QEMU, image tool, firmware and hardware preset validated.'
& python (Join-Path $PSScriptRoot 'verify-package.py') $Destination
if ($LASTEXITCODE) { throw 'Package layout/icon/dependency verification failed.' }
if ($Archive) {
    $Archive = [IO.Path]::GetFullPath($Archive)
    Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $Destination | Select-Object -ExpandProperty FullName) -DestinationPath $Archive -CompressionLevel Optimal
    & python (Join-Path $PSScriptRoot 'verify-package.py') $Destination --archive $Archive
    if ($LASTEXITCODE) { throw 'ZIP verification failed.' }
    Write-Host "Archive: $Archive"
}
Write-Host "Portable folder: $Destination"
Write-Host ('Size: {0:N1} MiB' -f ((Get-ChildItem -LiteralPath $Destination -File -Recurse | Measure-Object Length -Sum).Sum / 1MB))
