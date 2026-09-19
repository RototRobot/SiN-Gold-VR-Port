<#
.SYNOPSIS
    Runs SiN Gold VR unattended at the desk: loads a level, takes two screenshots,
    quits, and collects the logs and screenshots into a folder.

.DESCRIPTION
    Uses the installed mod (tools\deploy.ps1). Writes a small cfg into the mod folder,
    starts sin.exe with it, and waits for the game to quit, stopping it after
    -TimeoutSeconds if it does not. While the level starts, keys are posted to the
    game window to get past the "press any key" card and close the console it leaves
    down; both stop as soon as the world is being drawn. Then it
    moves the screenshots and the cfg out of
    the game folder, copies sinvr.log and qconsole.log, and converts the screenshots
    to PNG. With -Vr the headset path runs too (SteamVR starts if it is not running,
    and vr_force_render makes it draw for a headset that is not being worn).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\desk-test.ps1 -OutDir $env:TEMP\sinvr-flat

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\desk-test.ps1 -Vr -OutDir $env:TEMP\sinvr-vr

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\desk-test.ps1 -Vr -Foreground `
        -Commands "vr_dump_eye 1" -OutDir $env:TEMP\sinvr-eyes
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
    [string]$ExtraArgs = "",
    [string]$Commands = "",
    [switch]$Foreground,
    [switch]$KeepRunning,
    [switch]$NoCardKey
)

$ErrorActionPreference = "Stop"

$modDir = Join-Path $GameDir "sinvr"
if (-not (Test-Path (Join-Path $modDir "gamex86.dll"))) {
    throw "The mod is not installed in '$GameDir'. Run tools\deploy.ps1 first."
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

# One wait is one engine frame, so the two shots land at different moments.
$cfgName = "sinvr_desk_test.cfg"
# -Commands runs once the level is up and the first screenshot is taken. The
# engine splits it on semicolons, as it would a line typed into the console.
#
# A "wait" holds up everything behind it in the command buffer, including the
# commands the mod itself queues for a controller button, so -KeepRunning leaves
# the "quit" out: the cfg then runs dry, those commands get their turn, and the
# watchdog stops the game at -TimeoutSeconds.
$lines = @("// written by tools\desk-test.ps1", "map $Map") +
    (@("wait") * $WaitFrames) + @("screenshot") +
    @($Commands) + (@("wait") * $WaitFrames) + @("screenshot") +
    (@("wait") * 20) + $(if ($KeepRunning) { @() } else { @("quit") })
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
[DllImport("user32.dll")]
public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")]
public static extern bool BringWindowToTop(IntPtr hWnd);
[DllImport("user32.dll")]
public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")]
public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
'@
# SiN pauses itself when its window is not the active one, and paints the view
# over, so anything that reads the rendered image needs the window in front.
# Windows only lets the foreground process hand focus over, and pressing a key
# first is the accepted way of asking: it makes this process the last one with
# input, which lifts the lock for SetForegroundWindow.
function Show-Game($target) {
    if (-not $Foreground) { return }
    $target.Refresh()
    $window = $target.MainWindowHandle
    if ($window -eq [IntPtr]::Zero) { return }
    [SinVr.NativeInput]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)         # alt down
    [SinVr.NativeInput]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)         # alt up
    $null = [SinVr.NativeInput]::ShowWindow($window, 5)                   # SW_SHOW
    $null = [SinVr.NativeInput]::BringWindowToTop($window)
    $null = [SinVr.NativeInput]::SetForegroundWindow($window)
}

# The engine reads the scan code out of lParam rather than the virtual key, so
# both have to be right.
function Send-Key($target, [int]$key, [int]$scan) {
    $target.Refresh()
    $window = $target.MainWindowHandle
    if ($window -eq [IntPtr]::Zero) { return }
    $down = [IntPtr](($scan -shl 16) -bor 1)
    $up = [IntPtr](($scan -shl 16) -bor 0xC0000001)
    $null = [SinVr.NativeInput]::PostMessage($window, 0x100, [IntPtr]$key, $down)
    $null = [SinVr.NativeInput]::PostMessage($window, 0x101, [IntPtr]$key, $up)
}

$start = Get-Date
$process = Start-Process -FilePath (Join-Path $GameDir "sin.exe") -WorkingDirectory $GameDir -ArgumentList $arguments -PassThru
$null = $process.Handle
# Space gets past the level-start card, and stops as soon as the renderer log
# shows the world being drawn, so nothing is pressed once play starts.
$rendererLog = Join-Path $GameDir "sinvr.log"
$sawWorld = $false
while (-not $process.HasExited) {
    if (((Get-Date) - $start).TotalSeconds -gt $TimeoutSeconds) {
        Write-Host "timed out after $TimeoutSeconds s - stopping the game"
        Stop-Process -Id $process.Id -Force
        Start-Sleep -Seconds 1
        break
    }

    if (-not $sawWorld) {
        # -NoCardKey leaves the card to the mod itself, to test that it can.
        if (-not $NoCardKey) { Send-Key $process 0x20 0x39 }   # space
        $tail = Get-Content $rendererLog -Tail 60 -ErrorAction SilentlyContinue
        $sawWorld = [bool]($tail -match "RenderFrame")
    }

    Show-Game $process
    Start-Sleep -Milliseconds 500
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
