@echo off
setlocal
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo Не найден g++. Установите MinGW-w64, например:
    echo   winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT
    echo Затем откройте новый терминал и снова запустите build.bat
    exit /b 1
)

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -o mini-game-engine.exe main.cpp -lopengl32 -lglu32 -lgdi32 -luser32
if errorlevel 1 (
    echo Сборка не удалась
    exit /b 1
)

echo Сборка завершена: mini-game-engine.exe
echo Запуск основной сцены: mini-game-engine.exe
echo Запуск примера:       mini-game-engine.exe scene-example.txt
endlocal
