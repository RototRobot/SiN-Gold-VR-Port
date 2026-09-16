<#
.SYNOPSIS
    Runs SiN Gold VR unattended at the desk: loads a level, takes two screenshots,
    quits, and collects the logs and screenshots into a folder.

.DESCRIPTION
    Uses the installed mod (tools\deploy.ps1). Writes a small cfg into the mod folder,
    starts sin.exe with it, and waits for the game to quit, stopping it after
    -TimeoutSeconds if it does not. While it runs, a key is posted to the game window
    every second, so the level-start "press any key" card does not hold it up. Then it
    moves the screenshots and the cfg out of
    the game folder, copies sinvr.log and qconsole.log, and converts the screenshots
    to PNG. With -Vr the headset path runs too (SteamVR starts if it is not running,
    and vr_force_render makes it draw for a headset that is not being worn).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\desk-test.ps1 -OutDir $env:TEMP\sinvr-flat

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\desk-test.ps1 -Vr -OutDir $env:TEMP\sinvr-vr
#>
[CmdletBinding()]
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\SiN 1",
    [Parameter(Mandatory = $true)]
    [string]$OutDir,
    [switch]$Vr,
    [string]$Map = "bank",
    [int]$WaitFrames = 400,
    [int]$TimeoutSeconds = 180,
    [string]$ExtraArgs = ""
)

$ErrorActionPreference = "Stop"

$modDir = Join-Path $GameDir "sinvr"
if (-not (Test-Path (Join-Path $modDir "gamex86.dll"))) {
    throw "The mod is not installed in '$GameDir'. Run tools\deploy.ps1 first."
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

# One wait is one engine frame, so the two shots land at different moments.
$cfgName = "sinvr_desk_test.cfg"
$lines = @("// written by tools\desk-test.ps1", "map $Map") +
    (@("wait") * $WaitFrames) + @("screenshot") +
    (@("wait") * $WaitFrames) + @("screenshot") +
    (@("wait") * 20) + @("quit")
Set-Content -Path (Join-Path $modDir $cfgName) -Value $lines -Encoding ASCII

# vr_force_render: the runtime says an untouched headset needs no picture, and an
# unattended run should exercise the whole path anyway.
$vrArgs = if ($Vr) { "+set vr_enable 1 +set vr_force_render 1" } else { "+set vr_enable 0" }
$arguments = "+set game sinvr +set vid_ref vr $vrArgs +set logfile 2 $ExtraArgs +exec $cfgName"

# A level starts on SiN's "PRESS ANY KEY TO CONTINUE" card, and the engine does not
# run the command buffer while it is up, so an unattended run has to press a key
# itself - and posting the message works whether or not the window has focus. HOME
# is unbound in the stock config, so it does nothing else, in the card or in play.
Add-Type -Namespace SinVr -Name NativeInput -MemberDefinition @'
[DllImport("user32.dll")]
public static extern IntPtr PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
'@
function Send-AnyKey($target) {
    $target.Refresh()
    $window = $target.MainWindowHandle
    if ($window -eq [IntPtr]::Zero) { return }
    $null = [SinVr.NativeInput]::PostMessage($window, 0x100, [IntPtr]0x24, [IntPtr]0x01470001)  # down
    $null = [SinVr.NativeInput]::PostMessage($window, 0x101, [IntPtr]0x24, [IntPtr]0xC1470001)  # up
}

$start = Get-Date
$process = Start-Process -FilePath (Join-Path $GameDir "sin.exe") -WorkingDirectory $GameDir -ArgumentList $arguments -PassThru
$null = $process.Handle
while (-not $process.HasExited) {
    if (((Get-Date) - $start).TotalSeconds -gt $TimeoutSeconds) {
        Write-Host "timed out after $TimeoutSeconds s - stopping the game"
        Stop-Process -Id $process.Id -Force
        Start-Sleep -Seconds 1
        break
    }
    Send-AnyKey $process
    Start-Sleep -Milliseconds 1000
}
Write-Host ("exit code {0} after {1:N0} s" -f $process.ExitCode, ((Get-Date) - $start).TotalSeconds)

# Logs are copied; the cfg and the screenshots are moved out of the game folder.
Copy-Item (Join-Path $GameDir "sinvr.log") $OutDir -ErrorAction SilentlyContinue
Copy-Item (Join-Path $modDir "qconsole.log") $OutDir -ErrorAction SilentlyContinue
Move-Item (Join-Path $modDir $cfgName) $OutDir -Force
Get-ChildItem (Join-Path $modDir "players") -Recurse -Filter *.tga -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $start } |
    ForEach-Object { Move-Item $_.FullName $OutDir -Force }

# SiN writes uncompressed 24-bit TGA, which is BGR like a GDI+ 24bpp bitmap.
Add-Type -AssemblyName System.Drawing
foreach ($tga in Get-ChildItem $OutDir -Filter *.tga) {
    $bytes = [IO.File]::ReadAllBytes($tga.FullName)
    if ($bytes[2] -ne 2 -or $bytes[16] -ne 24) {
        Write-Host "skipped $($tga.Name): not an uncompressed 24-bit TGA"
        continue
    }

    $width = [BitConverter]::ToUInt16($bytes, 12)
    $height = [BitConverter]::ToUInt16($bytes, 14)
    $topDown = ($bytes[17] -band 0x20) -ne 0
    $offset = 18 + $bytes[0]
    $stride = $width * 3

    $bitmap = New-Object System.Drawing.Bitmap $width, $height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $width, $height
    $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bitmap.PixelFormat)
    for ($y = 0; $y -lt $height; $y++) {
        $sourceRow = if ($topDown) { $y } else { $height - 1 - $y }
        [Runtime.InteropServices.Marshal]::Copy($bytes, [int]($offset + $sourceRow * $stride),
            [IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $stride)
    }
    $bitmap.UnlockBits($data)
    $bitmap.Save([IO.Path]::ChangeExtension($tga.FullName, ".png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

Write-Host "collected in ${OutDir}:"
Get-ChildItem $OutDir | ForEach-Object { Write-Host "  $($_.Name)" }
