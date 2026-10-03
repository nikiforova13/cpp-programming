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

g++ -std=c++17 -Wall -Wextra -O2 -o cornell-box.exe main.cpp -lopengl32 -lglu32 -lgdi32 -luser32
if errorlevel 1 (
    echo Сборка не удалась
    exit /b 1
)

echo Сборка завершена: cornell-box.exe
echo Запуск: cornell-box.exe
echo Пример снимка: cornell-box.exe geometry.txt --screenshot screenshot.bmp
endlocal
