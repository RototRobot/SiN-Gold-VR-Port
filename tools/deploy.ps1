<#
.SYNOPSIS
    Installs the built SiN Gold VR files into a SiN Gold folder, or removes them.

.DESCRIPTION
    Copies the staging tree CMake builds (build\win32\stage\<Config>) into the game:

        ref_vr.dll, openxr_loader.dll   beside sin.exe
        sinvr\gamex86.dll               the mod folder
        sinvr2015\gamex86.dll           Wages of Sin's, which the engine runs with the 2015 data

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

    # The link to Wages of Sin's locale folder goes as a link: rmdir on a
    # junction takes the link alone, where a recursive delete would follow it.
    $link = Join-Path $GameDir "sinvr2015\locale"
    if ((Test-Path $link) -and ((Get-Item $link -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        cmd /c rmdir "$link" | Out-Null
        Write-Host "removed  sinvr2015\locale (the link; 2015\locale is untouched)"
    }

    foreach ($folder in @("sinvr2015", "sinvr")) {
        $path = Join-Path $GameDir $folder
        if (-not (Test-Path $path)) { continue }
        if (-not (Get-ChildItem $path -Force)) {
            Remove-Item $path
            Write-Host "removed  $folder\"
        } else {
            Write-Host "kept     $folder\ - it holds files the game wrote there (configs, saves)"
        }
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
# Wages of Sin: sin.exe adds the expansion's own 2015 folder under any game folder
# with "2015" in its name, so this one runs the expansion with the VR game module.
Install-StagedFile "sinvr2015\gamex86.dll"
Install-StagedFile "sinvr2015\gamex86.pdb" -Optional

# The engine finds menus, dialogue and their pictures in <game folder>\locale,
# by the folder's own name, so without this the expansion would show SiN's
# menus - whose New Game starts SiN, not Wages of Sin. A junction, so nothing
# of the game's is copied or changed.
$link = Join-Path $GameDir "sinvr2015\locale"
if (-not (Test-Path $link)) {
    New-Item -ItemType Junction -Path $link -Target (Join-Path $GameDir "2015\locale") | Out-Null
    Write-Host "linked   sinvr2015\locale -> 2015\locale"
}

. (Join-Path $PSScriptRoot "launchers.ps1")
$launchers = Get-SinVrLaunchers
foreach ($name in $launchers.Keys) {
    Set-Content -Path (Join-Path $GameDir $name) -Value $launchers[$name] -Encoding ASCII
    $installed.Add($name)
    Write-Host "wrote    $name"
}

Set-Content -Path $manifest -Value $installed -Encoding ASCII
Write-Host ""
Write-Host "Installed into $GameDir"
