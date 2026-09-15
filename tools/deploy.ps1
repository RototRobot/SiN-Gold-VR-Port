<#
.SYNOPSIS
    Installs the built SiN Gold VR files into a SiN Gold folder, or removes them.

.DESCRIPTION
    Copies the staging tree CMake builds (build\win32\stage\<Config>) into the game:

        ref_vr.dll, openxr_loader.dll   beside sin.exe
        sinvr\gamex86.dll               the mod folder

    and writes two launchers beside sin.exe. Nothing that shipped with the game is
    touched. Every file written is recorded in sinvr\installed-files.txt, and
    -Uninstall removes exactly those files.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\deploy.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -GameDir "D:\Games\SiN Gold" -Config Debug

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\SiN 1",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$BuildDir = "",
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

# Windows PowerShell 5.1 has not set $PSScriptRoot yet while it evaluates parameter defaults.
if (-not $BuildDir) {
    $BuildDir = Join-Path $PSScriptRoot "..\build\win32"
}

if (-not (Test-Path (Join-Path $GameDir "sin.exe"))) {
    throw "sin.exe not found in '$GameDir'. Pass the SiN Gold folder with -GameDir."
}

$modDir = Join-Path $GameDir "sinvr"
$manifest = Join-Path $modDir "installed-files.txt"

if ($Uninstall) {
    if (-not (Test-Path $manifest)) {
        Write-Host "Nothing to remove: $manifest does not exist."
        return
    }

    foreach ($relative in Get-Content $manifest) {
        $path = Join-Path $GameDir $relative
        if (Test-Path $path) {
            Remove-Item $path
            Write-Host "removed  $relative"
        }
    }
    Remove-Item $manifest

    if (-not (Get-ChildItem $modDir -Force)) {
        Remove-Item $modDir
        Write-Host "removed  sinvr\"
    } else {
        Write-Host "kept     sinvr\ - it holds files the game wrote there (configs, saves)"
    }
    return
}

$stage = Join-Path $BuildDir "stage\$Config"
if (-not (Test-Path (Join-Path $stage "ref_vr.dll"))) {
    throw "No $Config build in '$stage'. Build first: cmake --build --preset $($Config.ToLower())"
}

$installed = New-Object System.Collections.Generic.List[string]

function Install-StagedFile([string]$Relative, [switch]$Optional) {
    $source = Join-Path $stage $Relative
    if (-not (Test-Path $source)) {
        if ($Optional) { return }
        throw "Missing from the build: $Relative"
    }

    $target = Join-Path $GameDir $Relative
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item $source $target -Force
    $installed.Add($Relative)
    Write-Host "copied   $Relative"
}

Install-StagedFile "ref_vr.dll"
Install-StagedFile "ref_vr.pdb" -Optional
Install-StagedFile "openxr_loader.dll"
Install-StagedFile "sinvr\gamex86.dll"
Install-StagedFile "sinvr\gamex86.pdb" -Optional

# logfile 2 keeps sinvr\qconsole.log written line by line while this is in development.
$common = "+set game sinvr +set vid_ref vr +set logfile 2"
$launchers = [ordered]@{
    "SiN VR.bat"              = "$common +set vr_enable 1"
    "SiN VR (flatscreen).bat" = "$common +set vr_enable 0"
}

foreach ($name in $launchers.Keys) {
    $lines = @(
        "@echo off",
        "rem Written by SiN Gold VR (tools\deploy.ps1): starts SiN through the VR renderer and mod.",
        "cd /d `"%~dp0`"",
        "start `"`" sin.exe $($launchers[$name]) %*"
    )
    Set-Content -Path (Join-Path $GameDir $name) -Value $lines -Encoding ASCII
    $installed.Add($name)
    Write-Host "wrote    $name"
}

Set-Content -Path $manifest -Value $installed -Encoding ASCII
Write-Host ""
Write-Host "Installed into $GameDir"
