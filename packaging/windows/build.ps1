<#
.SYNOPSIS
  Builds xrdome for Windows and makes dist\xrdome-<version>-win64.zip.

.DESCRIPTION
  For a Windows 11 x64 machine without MSYS2. Run it in PowerShell from a
  checkout of xrdome:

    powershell -ExecutionPolicy Bypass -File packaging\windows\build.ps1

  It installs MSYS2 with winget if C:\msys64 is not there, brings MSYS2 up to
  date, then runs packaging/windows/build.sh in its UCRT64 environment (that
  script installs the compiler and libraries with pacman, builds, runs the
  tests and zips the result).

  No administrator rights are needed beyond what winget itself asks for when
  it installs MSYS2 (it may show one prompt, or none); everything else runs as
  your user, inside C:\msys64 and this checkout.
#>
[CmdletBinding()]
param(
    # Where MSYS2 is (winget installs it to C:\msys64).
    [string]$Msys2Root = 'C:\msys64'
)
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$shell = Join-Path $Msys2Root 'msys2_shell.cmd'

if (-not (Test-Path $shell)) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "MSYS2 is not in $Msys2Root and winget is not available. Install MSYS2 from https://www.msys2.org, then run this again."
    }
    winget install --id MSYS2.MSYS2 --exact --silent --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) { throw "winget install MSYS2.MSYS2 failed ($LASTEXITCODE)" }
    if (-not (Test-Path $shell)) {
        throw "MSYS2 was installed but $shell is missing: pass -Msys2Root with its folder."
    }
}

# Runs a command line in the UCRT64 shell, in the checkout, and stops on failure.
function Invoke-Msys2([string]$Command) {
    & $shell -defterm -here -no-start -ucrt64 -c $Command
    if ($LASTEXITCODE -ne 0) { throw "MSYS2 command failed ($LASTEXITCODE): $Command" }
}

Push-Location $repo
try {
    # Twice: the first run may stop after updating pacman itself or the runtime.
    Invoke-Msys2 'pacman -Syu --noconfirm'
    Invoke-Msys2 'pacman -Syu --noconfirm'
    Invoke-Msys2 './packaging/windows/build.sh'
    Get-ChildItem (Join-Path $repo 'dist') -Filter 'xrdome-*-win64.zip' |
        ForEach-Object { Write-Host "Made $($_.FullName) ($([math]::Round($_.Length / 1MB, 1)) MB)" }
} finally {
    Pop-Location
}
