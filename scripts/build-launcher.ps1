param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $OutputPath) { $OutputPath = Join-Path $root 'Minecraft in GTA.exe' }
New-Item -ItemType Directory -Path (Split-Path ([IO.Path]::GetFullPath($OutputPath))) -Force | Out-Null
$compiler = Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:winexe /platform:x64 "/out:$OutputPath" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Web.Extensions.dll (Join-Path $root 'launcher\Launcher.cs') (Join-Path $root 'launcher\Startup.cs')
if ($LASTEXITCODE) { throw 'Launcher compilation failed.' }
Write-Output $OutputPath
