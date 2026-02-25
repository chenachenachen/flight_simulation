@echo off
chcp 65001 >nul
title BlueSky Collision Test Environment

set "SCRIPT_DIR=%~dp0"
set "PY=%SCRIPT_DIR%Bluesky\venv\Scripts\python.exe"
set "BS_PY=%SCRIPT_DIR%Bluesky\BlueSky.py"
set "BRIDGE=%SCRIPT_DIR%bluesky_bridge.py"
set "LIVE_ENGINE=%SCRIPT_DIR%generate_collision_scn_from_xplane.py"

:: 注意：Qt 编译后在 Windows 上通常会放在 release 文件夹下，请根据实际路径修改
set "QT_EXEC=%SCRIPT_DIR%release\QtBlueSkyDemo.exe" 

echo ==========================================
echo   清理历史进程 (防端口冲突与重影)
echo ==========================================
taskkill /F /IM python.exe /T >nul 2>&1
taskkill /F /IM QtBlueSkyDemo.exe /T >nul 2>&1
timeout /t 1 >nul

echo ==========================================
echo   启动联合仿真环境 (X-Plane + BlueSky + Bridge + Qt)
echo ==========================================

echo [1/3] 启动 BlueSky...
cd /d "%SCRIPT_DIR%Bluesky"
:: 打开独立的控制台窗口运行 BlueSky
start "BlueSky Console" "%PY%" "%BS_PY%"
cd /d "%SCRIPT_DIR%"
timeout /t 3 >nul

echo [2/3] 启动 Bridge 与 Live Engine...
set BS_DISABLE_AUTOSPAWN=1
:: 在后台静默运行 Bridge 和 Live Engine
start "Bridge" /B "%PY%" "%BRIDGE%"
start "LiveEngine" /B "%PY%" "%LIVE_ENGINE%"
timeout /t 2 >nul

echo [3/3] 启动 Qt 叠加层...
if exist "%QT_EXEC%" (
    start "" "%QT_EXEC%"
) else (
    echo [警告] 未找到 Qt 可执行文件 "%QT_EXEC%"，请确认是否已编译。
)

echo.
echo ==========================================
echo   ✅ 测试环境已全部启动！
echo   请在弹出的 BlueSky 黑色窗口中输入测试指令，例如:
echo   IC scenario/collision_G2_collision.scn
echo ==========================================
pause
