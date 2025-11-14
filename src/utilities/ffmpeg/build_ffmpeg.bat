@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "SH_SCRIPT=%SCRIPT_DIR%build_ffmpeg.sh"

if not exist "%SH_SCRIPT%" (
    echo Could not find build_ffmpeg.sh next to this script.
    exit /b 1
)

if not "%SYN_FFMPEG_BASH%"=="" (
    set "BASH_EXE=%SYN_FFMPEG_BASH%"
) else (
    for %%P in (bash.exe "%ProgramFiles%\Git\bin\bash.exe" "%ProgramFiles(x86)%\Git\bin\bash.exe" "%ProgramFiles%\Git\usr\bin\bash.exe" "%ProgramFiles(x86)%\Git\usr\bin\bash.exe") do (
        if not defined BASH_EXE (
            if exist "%%~fP" (
                set "BASH_EXE=%%~fP"
            )
        )
    )
    if not defined BASH_EXE (
        where bash >nul 2>nul
        if %ERRORLEVEL%==0 (
            for /f "delims=" %%B in ('where bash') do (
                if not defined BASH_EXE set "BASH_EXE=%%B"
            )
        )
    )
)

if not defined BASH_EXE (
    echo Unable to locate bash.exe. Install Git for Windows or set SYN_FFMPEG_BASH to a bash executable path.
    exit /b 1
)

"%BASH_EXE%" "%SH_SCRIPT%" %*
exit /b %ERRORLEVEL%
