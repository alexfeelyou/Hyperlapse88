@echo off
setlocal DisableDelayedExpansion

:: Force script to run in its own folder and lock the output path
cd /d "%~dp0"
set "OUT_ABS=%~dp0context_bundle.txt"

echo =================================================
echo         C++ / Shader Source Code Packer
echo =================================================
echo.

if exist "%OUT_ABS%" del "%OUT_ABS%"

:: DEFENSE: If files were dragged directly onto the .bat icon, process them and exit
if not "%~1"=="" (
    call :ParseInput %*
    goto CheckDone
)

:InputLoop
echo Drag file(s)/folder(s), type a name (e.g., Framework.cpp), or press ENTER to finish.
set "USER_INPUT="
set /p "USER_INPUT=> "

if not defined USER_INPUT goto CheckDone

:: BUG FIX 1: Fixes Windows 11 pasting multiple files without spaces ("file1""file2")
set "USER_INPUT=%USER_INPUT:""=" "%"

:: Send raw input to a parser to safely handle multiple dragged files
call :ParseInput %USER_INPUT%
goto InputLoop


:: ---------------------------------------------------
:: Subroutine: Parses space-separated, quote-wrapped inputs
:: ---------------------------------------------------
:ParseInput
if "%~1"=="" exit /b
set "TARGET=%~1"

:: 1. Check if the exact path exists
if exist "%TARGET%\*" goto HandleFolder
if exist "%TARGET%" goto HandleFile

:: 2. Deep Search: If not found immediately, search all subdirectories
echo [Search] Looking for "%TARGET%"...
set "FOUND="
for /f "delims=" %%S in ('dir /s /b "%~dp0%TARGET%" 2^>nul') do (
    set "FOUND=1"
    if exist "%%S\*" (
        echo [Folder Found] %%S
        for /f "delims=" %%F in ('dir /s /b /a-d "%%S\*.cpp" "%%S\*.h" "%%S\*.hpp" "%%S\*.hlsl" "%%S\*.hlsli" "%%S\*.cs" 2^>nul') do (
            call :AppendFile "%%F"
        )
    ) else (
        echo [File Found] %%S
        call :AppendFile "%%S"
    )
)

if defined FOUND (
    shift
    goto ParseInput
)

echo [Error] Path or file not found: "%TARGET%"
echo.
shift
goto ParseInput

:HandleFolder
echo [Folder] %TARGET%
for /f "delims=" %%F in ('dir /s /b /a-d "%TARGET%\*.cpp" "%TARGET%\*.h" "%TARGET%\*.hpp" "%TARGET%\*.hlsl" "%TARGET%\*.hlsli" "%TARGET%\*.cs" 2^>nul') do (
    call :AppendFile "%%F"
)
echo.
shift
goto ParseInput

:HandleFile
echo [File] %TARGET%
call :AppendFile "%TARGET%"
echo.
shift
goto ParseInput


:: ---------------------------------------------------
:: Subroutine: Formats and Appends the File
:: ---------------------------------------------------
:AppendFile
set "FILE_PATH=%~1"

:: Path Cleaner: Strips the long absolute path for the .txt output
setlocal EnableDelayedExpansion
set "REL_PATH=!FILE_PATH:%~dp0=!"

:: Check if this exact file was already packed to prevent duplicates
if exist "%OUT_ABS%" (
    findstr /l /c:"PATH: !REL_PATH!" "%OUT_ABS%" >nul
    if not errorlevel 1 (
        echo   - [Skipped] Already packed: %~nx1
        endlocal
        exit /b
    )
)

echo   - Packing: %~nx1
echo ================================================ >> "%OUT_ABS%"
echo FILE: %~nx1 >> "%OUT_ABS%"
echo PATH: !REL_PATH! >> "%OUT_ABS%"
echo ================================================ >> "%OUT_ABS%"

:: BUG FIX 2: End local expansion BEFORE findstr, or the "^" regex symbol breaks
endlocal

findstr /n "^" "%FILE_PATH%" | findstr /v /r "^[0-9][0-9]*:$" >> "%OUT_ABS%"
echo. >> "%OUT_ABS%"
exit /b


:: ---------------------------------------------------
:: Exit Routine
:: ---------------------------------------------------
:CheckDone
if exist "%OUT_ABS%" goto EndScript
echo.
echo [!] No files were packed.
pause
exit /b

:EndScript
echo.
echo ------------------------------------------------
echo Success! File created at:
echo %OUT_ABS%
echo ------------------------------------------------
pause
exit /b