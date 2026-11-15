@echo off
REM ==========================================================================
REM  Fate/unlimited codes - native PC runner
REM ==========================================================================
REM
REM  Put your own disc image next to this file. Any name works: .iso, .bin.
REM  The launcher finds it, so you do not edit anything.
REM
REM  Usage:
REM    * double-click this file, or
REM    * drag your disc image onto this file, or
REM    * play.bat "D:\games\my copy of fate.iso"
REM
REM  No disc image is included. You bring a copy of a game you own.
REM ==========================================================================

setlocal enabledelayedexpansion
cd /d "%~dp0"

REM ------------------------------------------------ tuning (measured, see README)
set PS2_SCALE=1
set PS2_GPU_LOW_POWER=1
set PS2_QOS=1
set PS2_PRESENT_MODE=immediate
set PS2_SHARP_2D=0

set RUNNER=ps2run.exe
if not exist "%RUNNER%" (
    echo ERROR: ps2run.exe is not next to this file.
    echo.
    echo   The release should contain: ps2run.exe, SDL2.dll, dxcompiler.dll,
    echo   dxil.dll and this file, all in one folder.
    pause
    exit /b 1
)

REM ------------------------------------------------ find the disc image
set ISO=%~1
if not "!ISO!"=="" (
    if not exist "!ISO!" (
        echo ERROR: the file you gave does not exist:
        echo   !ISO!
        pause
        exit /b 1
    )
    goto :have_iso
)

REM No argument: take the first disc image in this folder. Preference order
REM matters - an ISO is what this was built against, .bin is the raw dump.
for %%E in (iso ISO bin BIN img IMG) do (
    if "!ISO!"=="" (
        for %%F in ("*.%%E") do (
            if "!ISO!"=="" set ISO=%%F
        )
    )
)
if "!ISO!"=="" (
    echo ERROR: no disc image found in this folder.
    echo.
    echo   Put your Fate/unlimited codes image here ^(.iso or .bin^) and run
    echo   this file again, or drag the image onto this file.
    echo.
    echo   Current folder: %CD%
    pause
    exit /b 1
)

:have_iso
echo Starting Fate/unlimited codes...
echo   disc: !ISO!
echo.

REM A memory card is created on first run if it is not there. It is the
REM conventional 8 MB PS2 card layout, and saving from the game writes to it.
set CARD=card.ps2

echo   Controls:
echo     Arrows    move (d-pad and left stick together)
echo     A S D F   square / triangle / circle / cross
echo     Z X C V   L1 / L2 / R1 / R2
echo     Enter     start        Tab    select
echo     Space     cross (cancel)
echo     F2        quick save   F4     quick load
echo     F1        quick menu   F11    full screen
echo.
echo   Japanese game: circle (D) confirms, cross (F or Space) goes back.
echo.

"%RUNNER%" --disc "!ISO!" --memcard "%CARD%" --window
set CODE=%ERRORLEVEL%

if not "%CODE%"=="0" (
    echo.
    echo The runner stopped with code %CODE%.
    echo A log was written beside the disc image; the last lines usually say why.
    pause
)
exit /b %CODE%
