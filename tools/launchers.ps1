<#
.SYNOPSIS
    The launchers SiN Gold VR puts beside sin.exe - shared by deploy.ps1 and package.ps1, so that
    an install from a build and one from the release zip start the game the same way.
#>

# Name of each launcher, and the lines of its .bat. logfile 2 keeps sinvr\qconsole.log (or
# sinvr2015\qconsole.log) written as it goes, which is what to send with a bug report.
function Get-SinVrLaunchers {
    $common = "+set vid_ref vr +set logfile 2"

    # Wages of Sin's menus, dialogue and their pictures are found in <game folder>\locale by the
    # folder's own name; without a link to 2015\locale the expansion shows SiN's menus, whose New
    # Game starts SiN. A junction needs no administrator, and copies and changes nothing of the
    # game's - made the first time, kept after.
    $link = 'if not exist "sinvr2015\locale" mklink /J "sinvr2015\locale" "2015\locale" >nul'

    $games = [ordered]@{
        "SiN VR.bat"                             = @{ Game = "sinvr";     Vr = 1 }
        "SiN VR (flatscreen).bat"                = @{ Game = "sinvr";     Vr = 0 }
        "SiN VR - Wages of Sin.bat"              = @{ Game = "sinvr2015"; Vr = 1 }
        "SiN VR - Wages of Sin (flatscreen).bat" = @{ Game = "sinvr2015"; Vr = 0 }
    }

    $launchers = [ordered]@{}
    foreach ($name in $games.Keys) {
        $g = $games[$name]
        $lines = @(
            "@echo off",
            "rem SiN Gold VR: starts SiN through the VR renderer and mod.",
            "cd /d `"%~dp0`""
        )
        if ($g.Game -eq "sinvr2015") {
            $lines += $link
        }
        $lines += "start `"`" sin.exe +set game $($g.Game) $common +set vr_enable $($g.Vr) %*"
        $launchers[$name] = $lines
    }
    return $launchers
}
