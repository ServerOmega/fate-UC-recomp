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
echo     Arrows    move (d-pad and left stick: menus read the stick)
echo     A S D F   square / triangle / circle / cross
echo     Z X C V   L1 / L2 / R1 / R2
echo     Enter     start        Tab   select
echo     Shift     (free)       Space cross (cancel)
echo     F2        quick save   F4    quick load
echo     F1        quick menu   F11   full screen
echo.
echo   Japanese game: circle (D) confirms, cross (F or Space) goes back.
echo.

"%PS2RUN%" --disc "%GAME_DIR%\disc.iso" --memcard "%GAME_DIR%\card.ps2" --game-dir "%GAME_DIR%" --window
