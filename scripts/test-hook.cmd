@echo off
setlocal EnableDelayedExpansion

rem A stand-in for a user's own script, for testing the hooks in
rem Preferences -> Misc. Every run appends one line to hook-runs.txt next to
rem the logs, so that "did it run" can be answered by looking rather than by
rem guessing -- which is the whole point of V-24 block 1.
rem
rem   test-hook.cmd <label> [exit-code] [seconds-to-take]
rem
rem The label is whichever moment this was set as, so one script can be
rem pointed at all three and the file still says which fired:
rem
rem   ...\scripts\test-hook.cmd queue-empty
rem   ...\scripts\test-hook.cmd transfer-started
rem   ...\scripts\test-hook.cmd last-transfer-finished
rem
rem The exit code is there for the "script fails" case (V-24 item 1.5), and
rem the delay for watching a slow script not block the program.

set "LABEL=%~1"
if "%LABEL%"=="" set "LABEL=no-label"

set "CODE=%~2"
if "%CODE%"=="" set "CODE=0"

set "DELAY=%~3"

set "OUTDIR=%APPDATA%\rclone-browser\rclone-browser\logs"
if not exist "%OUTDIR%" mkdir "%OUTDIR%" 2>nul
set "OUT=%OUTDIR%\hook-runs.txt"

for /f "usebackq tokens=*" %%T in (`powershell -NoProfile -Command "Get-Date -Format o"`) do set "NOW=%%T"

>>"%OUT%" echo %NOW%  label=%LABEL%  exit=%CODE%  cwd=%CD%

if not "%DELAY%"=="" ping -n %DELAY% 127.0.0.1 >nul 2>&1

exit /b %CODE%
