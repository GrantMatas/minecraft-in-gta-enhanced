[CmdletBinding()]
param([string]$Destination)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Destination) { $Destination = Join-Path $root '.cache\public-repo' }
$destinationPath = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $destinationPath) { throw 'Choose a new export directory; existing exports are preserved.' }
New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
$files = @('.gitignore','README.md','LICENSE','THIRD_PARTY_NOTICES.md','mc/build.gradle','mc/settings.gradle','mc/gradle.properties','mc/gradlew','mc/gradlew.bat','mc/.gitignore','gta/build.bat','gta/.gitignore','gta/fetch_deps.sh',
    'scripts/setup.ps1','scripts/build-launcher.ps1','scripts/install-gta.ps1','scripts/prepare-reshade.ps1','scripts/export-public.ps1')
foreach ($directory in @('docs','licenses','gta/src','gta/shaders','mc/src','mc/tests','mc/gradle/wrapper','launcher')) {
    $files += Get-ChildItem -LiteralPath (Join-Path $root $directory) -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($root,$_.FullName).Replace('\','/') }
}
foreach ($name in @('block_collision_test.cpp','build_block_collision.bat','pause_overlay_test.cpp','build_pause_overlay.bat')) { $files += "gta/tests/$name" }
$privatePattern = '(?i)C:[\\/]Users[\\/](?!Public[\\/])|\.codex[\\/]|codex-remote-attachments|Medal[\\/]Edits|gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]+|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----'
foreach ($relative in ($files | Sort-Object -Unique)) {
    $source = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Allowlisted file missing: $relative" }
    if ([IO.Path]::GetExtension($source) -eq '.jar') {
        if ($relative -ne 'mc/gradle/wrapper/gradle-wrapper.jar') { throw "Unexpected binary: $relative" }
    } else {
        if ([IO.File]::ReadAllText($source) -match $privatePattern) { throw "Private data pattern in $relative; review before publishing." }
    }
    $target = Join-Path $destinationPath $relative
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
}
Write-Output "Exported $($files.Count) allowlisted files to $destinationPath. Review contents and commit metadata before publication."
