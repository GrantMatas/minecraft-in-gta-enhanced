param([Parameter(Mandatory)][string]$GtaDir)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$runtime = Join-Path $root 'runtime\reshade'
$package = Join-Path $root '.cache\gta-package'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$config = Join-Path $runtime 'ReShade.ini'
$preset = Join-Path $runtime 'ReShadePreset.ini'
if (-not (Test-Path -LiteralPath $config)) {
    $contents = [IO.File]::ReadAllText((Join-Path $root 'launcher\ReShade.ini.template'))
    $contents = $contents.Replace('{{SHADERS}}', (Join-Path $GtaDir 'reshade-shaders\Shaders').Replace(',', ',,'))
    $contents = $contents.Replace('{{TEXTURES}}', (Join-Path $GtaDir 'reshade-shaders\Textures').Replace(',', ',,'))
    $contents = $contents.Replace('{{PRESET}}', $preset.Replace(',', ',,')).Replace('{{RUNTIME}}', $runtime.Replace(',', ',,'))
    [IO.File]::WriteAllText($config, $contents)
}
if (-not (Test-Path -LiteralPath $preset)) { Copy-Item -LiteralPath (Join-Path $root 'launcher\ReShadePreset.ini') -Destination $preset }
[IO.File]::WriteAllText((Join-Path $package 'ReShade.ini'), "[INSTALL]`r`nBasePath=$($runtime.Replace(',', ',,'))`r`n")
