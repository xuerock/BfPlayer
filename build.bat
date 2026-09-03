@echo off
REM BfPlayer 构建脚本
REM 用法: 双击 build.bat 或在命令行运行

setlocal

REM 设置 MSYS2 环境路径
set "MSYS2_ROOT=D:\workspace\tools\cpp-toolchain\msys64"
set "PATH=%MSYS2_ROOT%\mingw64\bin;%MSYS2_ROOT%\usr\bin;%PATH%"

echo ========================================
echo  Building BfPlayer...
echo ========================================

REM 创建构建目录
if not exist release mkdir release

REM 生成 Makefile
echo [1/2] Running qmake...
qmake-qt5.exe BfPlayer.pro
if errorlevel 1 (
    echo ERROR: qmake failed!
    exit /b 1
)

REM 编译
echo [2/2] Compiling...
mingw32-make -j4
if errorlevel 1 (
    echo ERROR: Build failed!
    exit /b 1
)

echo.
echo ========================================
echo  BUILD SUCCESS: release\BfPlayer.exe
echo ========================================

endlocal
