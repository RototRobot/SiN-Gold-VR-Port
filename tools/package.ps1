<#
.SYNOPSIS
    Packs a Release build of SiN Gold VR into a zip that players unpack into their SiN Gold folder.

.DESCRIPTION
    Takes the staging tree CMake builds (build\win32\stage\Release) and writes
    dist\SiN-Gold-VR-<version>.zip, laid out like the game folder:

        ref_vr.dll, openxr_loader.dll            beside sin.exe
        sinvr\gamex86.dll                        SiN
        sinvr2015\gamex86.dll                    Wages of Sin
        SiN VR*.bat                              the launchers (tools\launchers.ps1)
        SiN Gold VR - Read Me.txt                docs\RELEASE_README.txt
        SiN Gold VR licences\                    the licences that go with the files

    No debug symbols, logs or settings go in. The version is the one in CMakeLists.txt.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\package.ps1
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [string]$OpenXrSdk = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $BuildDir) { $BuildDir = Join-Path $root "build\win32" }
if (-not $OpenXrSdk) { $OpenXrSdk = Join-Path (Split-Path $root -Parent) "OpenXR-SDK-Source-release-1.1.63" }

$stage = Join-Path $BuildDir "stage\Release"
foreach ($f in @("ref_vr.dll", "openxr_loader.dll", "sinvr\gamex86.dll", "sinvr2015\gamex86.dll")) {
    if (-not (Test-Path (Join-Path $stage $f))) {
        throw "Missing from the build: $f. Build first: cmake --build --preset release"
    }
}

$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern "project\(\S+ VERSION ([0-9.]+)").Matches[0].Groups[1].Value
$name = "SiN-Gold-VR-$version"
$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force $dist | Out-Null
# assembled in a fresh folder of its own each time, and left there to look at
$work = Join-Path ([IO.Path]::GetTempPath()) ("sinvr-package-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force $work, (Join-Path $work "sinvr"), (Join-Path $work "sinvr2015"),
    (Join-Path $work "SiN Gold VR licences") | Out-Null

Copy-Item (Join-Path $stage "ref_vr.dll") $work
Copy-Item (Join-Path $stage "openxr_loader.dll") $work
Copy-Item (Join-Path $stage "sinvr\gamex86.dll") (Join-Path $work "sinvr")
Copy-Item (Join-Path $stage "sinvr2015\gamex86.dll") (Join-Path $work "sinvr2015")

. (Join-Path $PSScriptRoot "launchers.ps1")
$launchers = Get-SinVrLaunchers
foreach ($l in $launchers.Keys) {
    Set-Content -Path (Join-Path $work $l) -Value $launchers[$l] -Encoding ASCII
}

$readme = (Get-Content (Join-Path $root "docs\RELEASE_README.txt") -Raw) -replace "\{VERSION\}", $version
Set-Content -Path (Join-Path $work "SiN Gold VR - Read Me.txt") -Value $readme -Encoding ASCII

$licences = Join-Path $work "SiN Gold VR licences"
Copy-Item (Join-Path $root "game\license.txt") (Join-Path $licences "Night Dive game source licence (gamex86.dll).txt")
$ownLicence = Join-Path $root "LICENSE"
if (Test-Path $ownLicence) {
    Copy-Item $ownLicence (Join-Path $licences "SiN Gold VR licence - Apache 2.0 (ref_vr.dll and the mod's code).txt")
    Copy-Item (Join-Path $root "NOTICE") (Join-Path $licences "NOTICE.txt")
    Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.md") (Join-Path $licences "THIRD_PARTY_NOTICES.md")
} else {
    Write-Warning "No LICENSE at the repository root: the zip has no licence for the mod's own code."
}
$xrLicence = Join-Path $OpenXrSdk "LICENSE"
if (Test-Path $xrLicence) {
    Copy-Item $xrLicence (Join-Path $licences "OpenXR loader licence (openxr_loader.dll).txt")
} else {
    Write-Warning "The OpenXR SDK's LICENSE was not found at ${OpenXrSdk}: add it by hand (Apache-2.0)."
}

$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $work "*") -DestinationPath $zip
Write-Host "Wrote $zip (assembled in $work)"
Get-ChildItem $work -Recurse -File | ForEach-Object { "  " + $_.FullName.Substring($work.Length + 1) }
