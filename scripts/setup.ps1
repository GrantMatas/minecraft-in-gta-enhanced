[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GtaDir,
    [Parameter(Mandatory)][string]$JavaHome,
    [Parameter(Mandatory)][string]$ScriptHookArchive,
    [Parameter(Mandatory)][string]$ScriptHookSdkArchive,
    [Parameter(Mandatory)][string]$ReShadeRuntime,
    [string]$SteamExecutable = (Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe'),
    [string]$AchromaJar,
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath (Join-Path $GtaDir 'GTA5_Enhanced.exe'))) { throw 'Choose the GTA Enhanced installation containing GTA5_Enhanced.exe.' }
foreach ($required in @((Join-Path $JavaHome 'bin\java.exe'),$SteamExecutable,$ScriptHookArchive,$ScriptHookSdkArchive,$ReShadeRuntime)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required file missing: $required" }
}
if (Get-Process -Name GTA5_Enhanced,GTA5 -ErrorAction SilentlyContinue) { throw 'Close GTA normally before setup.' }
$javaVersion = & (Join-Path $JavaHome 'bin\java.exe') -version 2>&1 | Out-String
if ($javaVersion -notmatch 'version "25[.\"]') { throw 'This source requires JDK 25.' }
$temp = Join-Path $root ('.cache\setup-' + [Guid]::NewGuid().ToString('N'))
$deps = Join-Path $root 'gta\third_party'
$package = Join-Path $root '.cache\gta-package'
New-Item -ItemType Directory -Force -Path $temp,$deps,(Join-Path $deps 'shv'),(Join-Path $deps 'reshade'),(Join-Path $package 'reshade-shaders\Shaders') | Out-Null
Expand-Archive -LiteralPath $ScriptHookSdkArchive -DestinationPath (Join-Path $temp 'sdk')
Expand-Archive -LiteralPath $ScriptHookArchive -DestinationPath (Join-Path $temp 'runtime')
foreach ($name in @('main.h','nativeCaller.h','types.h','ScriptHookV.lib')) {
    $file = @(Get-ChildItem -LiteralPath (Join-Path $temp 'sdk') -File -Recurse | Where-Object Name -eq $name)
    if ($file.Count -ne 1) { throw "SDK must contain exactly one $name" }
    Copy-Item -LiteralPath $file[0].FullName -Destination (Join-Path $deps "shv\$name")
}
foreach ($name in @('ScriptHookV.dll','dinput8.dll','xinput1_4.dll')) {
    $file = @(Get-ChildItem -LiteralPath (Join-Path $temp 'runtime') -File -Recurse | Where-Object Name -eq $name)
    if ($file.Count -ne 1) { throw "Compatible Enhanced Script Hook runtime must contain exactly one $name" }
    Copy-Item -LiteralPath $file[0].FullName -Destination (Join-Path $package $name)
}
foreach ($header in @('reshade.hpp','reshade_api.hpp','reshade_api_device.hpp','reshade_api_pipeline.hpp','reshade_api_resource.hpp','reshade_api_format.hpp','reshade_events.hpp','reshade_overlay.hpp')) {
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/crosire/reshade/v6.8.0/include/$header" -OutFile (Join-Path $deps "reshade\$header")
}
foreach ($header in @('ReShade.fxh','ReShadeUI.fxh')) {
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/crosire/reshade-shaders/slim/Shaders/$header" -OutFile (Join-Path $package "reshade-shaders\Shaders\$header")
}
Copy-Item -LiteralPath $ReShadeRuntime -Destination (Join-Path $package 'ReShade64.asi')
Copy-Item -LiteralPath (Join-Path $root 'gta\shaders\MCPassthrough.fx') -Destination (Join-Path $package 'reshade-shaders\Shaders\MCPassthrough.fx')
Copy-Item -LiteralPath (Join-Path $root 'launcher\ReShadePreset.ini') -Destination (Join-Path $package 'ReShadePreset.ini')
# This setting applies only to offline Story Mode. Steam options stay user controlled.
[IO.File]::WriteAllText((Join-Path $package 'args.txt'), '-nobattleye' + [Environment]::NewLine)
if ($AchromaJar) {
    if (-not (Test-Path -LiteralPath $AchromaJar -PathType Leaf)) { throw 'Achroma JAR not found.' }
    $mods = Join-Path $root 'mc\run\mods'
    New-Item -ItemType Directory -Force -Path $mods | Out-Null
    Copy-Item -LiteralPath $AchromaJar -Destination (Join-Path $mods 'achroma-1.1.0.jar')
}
$env:JAVA_HOME = (Resolve-Path -LiteralPath $JavaHome).Path
$env:GRADLE_USER_HOME = Join-Path $root '.cache\gradle'
& (Join-Path $root 'mc\gradlew.bat') -p (Join-Path $root 'mc') build --no-daemon --console=plain
if ($LASTEXITCODE) { throw 'Minecraft build failed.' }
& (Join-Path $root 'gta\build.bat')
if ($LASTEXITCODE) { throw 'GTA native build failed. Install MSVC C++ tools and Windows SDK or set VCVARS.' }
Copy-Item -LiteralPath (Join-Path $root 'gta\build\MCPassthrough.asi') -Destination (Join-Path $package 'MCPassthrough.asi')
@{javaHome=$env:JAVA_HOME;steamExecutable=(Resolve-Path -LiteralPath $SteamExecutable).Path} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'launcher.local.json')
& (Join-Path $PSScriptRoot 'build-launcher.ps1')
if (-not $BuildOnly) {
    $action = if (Test-Path -LiteralPath (Join-Path $root 'diagnostics\install-Enhanced.json')) { 'Update' } else { 'Install' }
    & (Join-Path $PSScriptRoot 'install-gta.ps1') -GtaDir $GtaDir -Action $action
}
Write-Output 'Built Minecraft in GTA.exe. For BuildOnly, install with scripts/install-gta.ps1 before running the launcher.'
Write-Output 'Downloaded runtime files, personal paths, caches and saves are local and excluded from Git.'
