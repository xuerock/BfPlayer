#!/usr/bin/env bash
# BfPlayer Windows (MinGW64) 一键构建脚本
# 解决本沙箱环境下的三个构建坑：
#   1. cc1.exe 找不到 libmpfr/libgmp/libisl 等依赖 DLL（已复制到 cc1 自身目录）
#   2. ld / cc1 在 C:\Windows 创建临时文件被拒 -> 设 TMPDIR 到可写目录
#   3. qmake 生成的 Makefile 里 windres / moc_predefs 用裸工具名，沙箱子进程找不到
#      -> 把关键工具改成本机绝对路径，并加 -pipe 规避编译期临时文件
#
# 路径约定（重要）：本机 Bash 是 Git-Bash/MSYS，命令搜索 PATH 只认 /d/... 形式，
# 不认 D:/... 形式（后者作为“直接执行的程序路径”可以，但放进 PATH 搜不到）。
# 因此此处 TOOLCHAIN/BIN 一律用 /d/...；TMPDIR 用原生 Windows 的 D:/...（cc1/ld 是
# 原生 MinGW 程序，认 D:/...，且此前实测可写）。
#
# 注意：qmake 会打印 "WARNING: Failure to find: debug/BfPlayer_res.o"，属正常提示，
# 但其退出码非零，故不能开 set -e，否则脚本会误判失败。
TOOLCHAIN="/d/workspace/tools/cpp-toolchain/msys64"
MINGW="$TOOLCHAIN/mingw64"
BIN="$MINGW/bin"
CC1DIR="$MINGW/lib/gcc/x86_64-w64-mingw32/16.2.0"
TMPBUILD="D:/workspace/tmpbuild"            # 前向斜杠：用于 mkdir（MSYS bash 可识别）
# 关键：ld/collect2 创建临时文件时只认 Windows 反斜杠形式的 TMPDIR（D:/... 与 /d/... 均被忽略，
# 导致回退到不可写的 C:\Windows 而报 "Permission denied"）。故 TMPDIR/TMP/TEMP 必须用反斜杠。
TMPBUILD_ENV='D:\workspace\tmpbuild'

mkdir -p "$TMPBUILD"

# 1) 确保 cc1 的依赖 DLL 在其自身目录（Windows DLL 搜索第一优先级是 exe 所在目录）
for d in libgcc_s_seh-1.dll libgmp-10.dll libisl-23.dll libmpc-3.dll libmpfr-6.dll libwinpthread-1.dll zlib1.dll libzstd.dll; do
    if [ -f "$BIN/$d" ] && [ ! -f "$CC1DIR/$d" ]; then
        cp -v "$BIN/$d" "$CC1DIR/$d"
    fi
done

# 2) 环境：PATH 用 /d/ 形式让工具可搜索；TMPDIR/TMP/TEMP 用反斜杠形式（ld 才认）
export PATH="$BIN:$TOOLCHAIN/usr/bin:$PATH"
export TMPDIR="$TMPBUILD_ENV"
export TMP="$TMPBUILD_ENV"
export TEMP="$TMPBUILD_ENV"

PRO_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$PRO_DIR"

# 3) 生成 Makefile（.pro 里已让 swscale/swresample 走 .dll.a 导入库）
"$BIN/qmake-qt5.exe" BfPlayer.pro

# 4) 修补 Makefile：工具改绝对路径 + 加 -pipe
# 4a. CC/CXX/LINKER 强制覆盖为工具链绝对路径（/d/ 形式，make 执行时可直接运行）
python3 - "$PRO_DIR/Makefile.Release" <<'PY'
import sys
p = sys.argv[1]
with open(p, 'r', encoding='utf-8', newline='') as f:
    lines = f.readlines()
BIN = "/d/workspace/tools/cpp-toolchain/msys64/mingw64/bin"
out = []
for ln in lines:
    if ln.startswith('CC\t') or ln.startswith('CC  '):
        out.append('\t' + BIN + '/gcc.exe\n')
    elif ln.startswith('CXX\t') or ln.startswith('CXX  '):
        out.append('\t' + BIN + '/g++.exe\n')
    elif ln.startswith('LINKER'):
        out.append('LINKER\t= ' + BIN + '/g++.exe\n')
    else:
        out.append(ln)
lines = out
with open(p, 'w', encoding='utf-8', newline='') as f:
    f.writelines(lines)
PY

sed -i "s|^\twindres |\t/d/workspace/tools/cpp-toolchain/msys64/mingw64/bin/windres.exe |" Makefile.Release
# moc_predefs 那行裸 g++ 改绝对路径
python3 - <<'PY'
p='Makefile.Release'
with open(p,'r',encoding='utf-8',newline='') as f:
    lines=f.readlines()
for i,l in enumerate(lines):
    if l.lstrip().startswith('g++ -fno-keep-inline-dllexport -pipe') and 'moc_predefs' in l:
        lines[i]='\t/d/workspace/tools/cpp-toolchain/msys64/mingw64/bin/g++.exe'+l.lstrip()[3:]
with open(p,'w',encoding='utf-8',newline='') as f:
    f.writelines(lines)
PY
# 全部加 -pipe（编译期不写临时 .s 文件）
sed -i 's|-fno-keep-inline-dllexport |-fno-keep-inline-dllexport -pipe |g' Makefile.Release

# 5) 编译链接
"$TOOLCHAIN/usr/bin/make.exe" -j4 -f Makefile.Release

echo
echo "构建完成 -> $PRO_DIR/release/BfPlayer.exe"
ls -la "$PRO_DIR/release/BfPlayer.exe"
