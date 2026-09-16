@echo off
rem launches counter-strike through steam and injects fixhop.dll
setlocal
cd /d "%~dp0"

rem extra launch options go here
set "PARAMS=-insecure -console"

if not exist "fixhop.dll" ( echo [run] fixhop.dll missing, build the project first & pause & exit /b 1 )
if not exist "inject.exe" ( echo [run] inject.exe missing, build the project first & pause & exit /b 1 )

rem app 10 is counter strike. steam carries the launch options through the url,
rem but asks to confirm them first, so expect one dialog per launch
start "" "steam://run/10//%PARAMS%/"

rem --new skips any hl.exe that was already running, so a stale instance cannot
rem swallow the injection meant for the one being launched now
inject.exe hl.exe "%~dp0fixhop.dll" 180 --new
if errorlevel 1 (
    echo.
    echo [run] injection FAILED, see the message above
    pause
    exit /b 1
)

echo.
echo [run] injected. fixhop_version should exist in the console now; the fix
echo       itself goes live when you join a map, which fixhop.log reports as
echo       "layout validated".
echo.
timeout /t 10 >nul
endlocal
