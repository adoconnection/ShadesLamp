@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   WasmLED - WASM Program Builder
echo ============================================
echo.

:: Check if clang is available
where clang >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: clang not found in PATH.
    echo Install LLVM/clang with wasm32 target support.
    echo Download from: https://github.com/llvm/llvm-project/releases
    exit /b 1
)

echo [OK] clang found
echo.

:: Set paths
set "ROOT=%~dp0.."
set "PROGRAMS_DIR=%ROOT%\programs"
set "DATA_DIR=%ROOT%\data\programs"
set "COMMON_DIR=%PROGRAMS_DIR%\common"

:: Create output directories
if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"

:: Track results
set SUCCESS=0
set FAIL=0
set INDEX=0

:: List of programs to build (order determines LittleFS index)
set "PROGRAM_LIST=rgb_cycle rainbow random_blink"

for %%P in (%PROGRAM_LIST%) do (
    set "PROG_NAME=%%P"
    set "SRC=%PROGRAMS_DIR%\%%P\main.c"
    set "OUT=%PROGRAMS_DIR%\%%P\%%P.wasm"
    set "DATA_OUT=%DATA_DIR%\!INDEX!.wasm"

    echo Building: %%P
    echo   Source: !SRC!
    echo   Output: !OUT!

    if not exist "!SRC!" (
        echo   [FAIL] Source file not found: !SRC!
        set /a FAIL+=1
    ) else (
        clang --target=wasm32 -nostdlib -O2 ^
            -I "%COMMON_DIR%" ^
            -Wl,--no-entry ^
            -Wl,--export-dynamic ^
            -Wl,--allow-undefined ^
            -o "!OUT!" "!SRC!"

        if !ERRORLEVEL! neq 0 (
            echo   [FAIL] Compilation failed for %%P
            set /a FAIL+=1
        ) else (
            :: Get file size
            for %%F in ("!OUT!") do set "SIZE=%%~zF"
            echo   [OK] Compiled successfully ^(!SIZE! bytes^)

            :: Copy to data directory with numeric name
            copy /Y "!OUT!" "!DATA_OUT!" >nul
            echo   [OK] Copied to !DATA_OUT!
            set /a SUCCESS+=1
        )
    )

    set /a INDEX+=1
    echo.
)

echo ============================================
echo   Results: %SUCCESS% succeeded, %FAIL% failed
echo ============================================

if %FAIL% gtr 0 (
    echo.
    echo Some programs failed to build!
    exit /b 1
)

echo.
echo All programs built successfully.
echo WASM files are in: %PROGRAMS_DIR%\{name}\{name}.wasm
echo LittleFS data in:  %DATA_DIR%\
exit /b 0
