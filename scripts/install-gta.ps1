[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GtaDir,
    [ValidateSet('Install','Update','Uninstall','Status')][string]$Action = 'Status'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = (Resolve-Path -LiteralPath $GtaDir).Path.TrimEnd('\')
$exe = @('GTA5_Enhanced.exe','GTA5.exe') | Where-Object { Test-Path -LiteralPath (Join-Path $game $_) } | Select-Object -First 1
if (-not $exe) { throw 'Expected a GTA installation containing GTA5_Enhanced.exe or GTA5.exe.' }
$edition = if ($exe -eq 'GTA5_Enhanced.exe') { 'Enhanced' } else { 'Legacy' }
$version = (Get-Item -LiteralPath (Join-Path $game $exe)).VersionInfo.FileVersion
$manifestPath = Join-Path $root "diagnostics\install-$edition.json"
$package = Join-Path $root '.cache\gta-package'
Write-Output "Target: $edition $version at $game (Story Mode only)"
if ($Action -eq 'Status') {
    if (Test-Path -LiteralPath $manifestPath) { Get-Content -LiteralPath $manifestPath }
    else { Write-Output 'No installation recorded by this script.' }
    return
}
if (Get-Process -Name GTA5,GTA5_Enhanced,GTA5_BE,GTA5_Enhanced_BE -ErrorAction SilentlyContinue) {
    throw 'Close GTA before installing or removing the integration.'
}
if ($Action -in @('Install','Update')) { & (Join-Path $PSScriptRoot 'prepare-reshade.ps1') -GtaDir $game }
if ($Action -eq 'Update') {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.game -ne $game) { throw 'Manifest belongs to a different GTA directory.' }
    $plans = @()
    foreach ($source in Get-ChildItem -LiteralPath $package -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($package, $source.FullName)
        $target = [IO.Path]::GetFullPath((Join-Path $game $relative))
        if (-not $target.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid package path.' }
        $sha = (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash
        $owned = @($manifest.files | Where-Object relative -eq $relative)
        if ($owned.Count -gt 1) { throw 'Duplicate manifest entry.' }
        if ($owned.Count -eq 1 -and $owned[0].sha256 -eq $sha) { continue }
        if (Test-Path -LiteralPath $target) {
            if ($owned.Count -ne 1 -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $owned[0].sha256) {
                throw "Preserving file changed outside this installer: $target"
            }
        }
        $plans += [pscustomobject]@{ relative=$relative; source=$source.FullName; target=$target; sha256=$sha; existed=(Test-Path -LiteralPath $target) }
    }
    $backup = Join-Path $root ('diagnostics\backups\gta-update-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $backup 'manifest.json')
    foreach ($plan in $plans) {
        if ($plan.existed) {
            $priorFile = Join-Path $backup $plan.relative
            New-Item -ItemType Directory -Path (Split-Path $priorFile) -Force | Out-Null
            Copy-Item -LiteralPath $plan.target -Destination $priorFile
        }
    }
    try {
        foreach ($plan in $plans) {
            New-Item -ItemType Directory -Path (Split-Path $plan.target) -Force | Out-Null
            Copy-Item -LiteralPath $plan.source -Destination $plan.target
            $manifest.files = @($manifest.files | Where-Object relative -ne $plan.relative) +
                [pscustomobject]@{ relative=$plan.relative; sha256=$plan.sha256 }
        }
        $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath
    } catch {
        foreach ($plan in $plans) {
            if ($plan.existed) { Copy-Item -LiteralPath (Join-Path $backup $plan.relative) -Destination $plan.target }
            elseif (Test-Path -LiteralPath $plan.target) { Remove-Item -LiteralPath $plan.target }
        }
        Copy-Item -LiteralPath (Join-Path $backup 'manifest.json') -Destination $manifestPath
        throw
    }
    Write-Output "Updated $($plans.Count) owned/new package files; backup: $backup"
    return
}
if ($Action -eq 'Uninstall') {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.game -ne $game) { throw 'Manifest belongs to a different GTA directory.' }
    $remaining = @()
    foreach ($entry in $manifest.files) {
        $target = [IO.Path]::GetFullPath((Join-Path $game $entry.relative))
        if (-not $target.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid manifest path.' }
        if (-not (Test-Path -LiteralPath $target)) { continue }
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.sha256) {
            Write-Warning "Leaving changed file for manual review: $target"
            $remaining += $entry
            continue
        }
        Remove-Item -LiteralPath $target
    }
    $manifest.files = $remaining
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath
    Write-Output 'Removed unchanged files added by this installer. Saves were not touched.'
    return
}
if (Test-Path -LiteralPath $manifestPath) {
    $prior = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if (@($prior.files).Count) { throw 'An installation is already recorded; inspect Status first.' }
}
if (-not (Test-Path -LiteralPath $package)) { throw 'Stage the GTA package first.' }
$sources = @(Get-ChildItem -LiteralPath $package -File -Recurse)
if (-not $sources.Count) { throw 'Empty package.' }
$entries = @()
foreach ($source in $sources) {
    $relative = [IO.Path]::GetRelativePath($package, $source.FullName)
    $target = [IO.Path]::GetFullPath((Join-Path $game $relative))
    if (-not $target.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid package path.' }
    if (Test-Path -LiteralPath $target) { throw "Preserving existing file; installation stopped: $target" }
    $entries += [pscustomobject]@{ relative=$relative; sha256=(Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash }
}
New-Item -ItemType Directory -Path (Split-Path $manifestPath) -Force | Out-Null
$manifest = [pscustomobject]@{ game=$game; edition=$edition; exeVersion=$version; installedAt=[DateTime]::UtcNow.ToString('o'); files=@() }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath
try {
    foreach ($entry in $entries) {
        $target = Join-Path $game $entry.relative
        New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $package $entry.relative) -Destination $target
        $manifest.files += $entry
        $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath
    }
} catch {
    # Only files just added by this transaction are eligible for rollback.
    foreach ($entry in $manifest.files) {
        $target = Join-Path $game $entry.relative
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    }
    $manifest.files = @()
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath
    throw
}
Write-Output "Installed $($manifest.files.Count) files. F7 enables passthrough after entering Story Mode."
Write-Output 'Enhanced runtime support remains unverified until ScriptHookV.log confirms this executable.'
