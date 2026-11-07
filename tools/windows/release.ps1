#requires -Version 7.0
# Brings a Windows release folder up to date with a build: release.ps1 -Build <dir> -Folder <dir>. The folder can be copied to another PC as it is.
# The data and save folders a player's runs leave in it are kept, and no game data comes from here, so a fresh folder holds nothing from a disc.
param(
    [Parameter(Mandatory)][string]$Build,
    [Parameter(Mandatory)][string]$Folder
)
$ErrorActionPreference = 'Stop'

# Named one by one, so that a build missing any of them fails here and not on the other PC.
$needed = @('darkcloud.exe', 'dcdata.exe', 'SDL3.dll', 'lang', 'glyphs', 'assets/data.zip', 'assets/datapacks')
$missing = @($needed | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Build $_)) })
if ($missing.Count -gt 0) {
    throw "release: the build has no $($missing -join ', ')"
}

New-Item -ItemType Directory -Force $Folder | Out-Null
# The executables first: a game left running from this folder locks them, and the build should stop before anything has changed.
foreach ($name in 'darkcloud.exe', 'dcdata.exe') {
    Copy-Item -LiteralPath (Join-Path $Build $name) -Destination $Folder -Force
}
Get-ChildItem -LiteralPath $Build -Filter '*.dll' -File | Copy-Item -Destination $Folder -Force
foreach ($name in 'lang', 'glyphs', 'assets') {
    $old = Join-Path $Folder $name
    if (Test-Path -LiteralPath $old) {
        Remove-Item -LiteralPath $old -Recurse -Force
    }
    Copy-Item -LiteralPath (Join-Path $Build $name) -Destination $Folder -Recurse
}

function Write-Launcher([string]$Name, [string[]]$Settings) {
    $lines = @('@echo off', 'pushd "%~dp0"') + $Settings + @('darkcloud.exe --data data --save save > chronicle.log 2>&1')
    Set-Content -LiteralPath (Join-Path $Folder $Name) -Value $lines -Encoding ascii
}
Write-Launcher 'Play.bat' @()
Remove-Item -LiteralPath (Join-Path $Folder 'Play HDR.bat') -ErrorAction SilentlyContinue # HDR is in Options now
Write-Host "kazarov: release folder $Folder"
