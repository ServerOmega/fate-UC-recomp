@echo off
REM Starts Fate/unlimited codes with the settings that measured best in a real fight.
REM
REM All of these are also the defaults compiled into ps2run.exe (runtime/include/ps2/
REM tuned_defaults.hpp), so this file changes nothing the exe does not already do.
REM They are set here explicitly so the numbers are visible beside the run.
REM
REM Measured here (Ryzen 5 5500U, integrated Radeon, Direct3D 12):
REM   46.37 Hz and 60.6% stretched sound  ->  57.69 Hz and 7.8%
REM The single biggest step was turning off the 2D sharpening filter: half the CPU
REM (204% -> 102%) for slightly softer text.
REM
REM If the picture tears in fast movement, change immediate to mailbox. Smoother, but
REM it measured 46.4 Hz against 57.7.
REM   set PS2_PRESENT_MODE=mailbox

cd /d "%~dp0"

set PS2_SCALE=1
set PS2_GPU_LOW_POWER=1
set PS2_QOS=1
set PS2_PRESENT_MODE=immediate
set PS2_SHARP_2D=0

set GAME_DIR=.local\upstream\games\fate
set PS2RUN=.local\upstream\build\native-fate\ps2run.exe

if not exist "%PS2RUN%" (
    echo The runner is not built yet.
    echo.
    echo   ./build.sh "C:\path\to\your\Fate unlimited codes.iso"
    echo.
    echo See docs\BUILD.md.
    pause
    exit /b 1
)

echo Starting Fate/unlimited codes...
echo.
echo   Controls:
echo     W A S D   move
echo     E         confirm (Japanese game: circle confirms, cross goes back)
echo     Enter     start
echo     F5 / F8   quick save / quick load
echo     F11       full screen
echo.

"%PS2RUN%" --disc "%GAME_DIR%\disc.iso" --memcard "%GAME_DIR%\card.ps2" --game-dir "%GAME_DIR%" --window
