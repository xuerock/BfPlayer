#include <QApplication>
#include <QIcon>
#include "ui/MainWindow.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

// ============================================================================
// Windows 崩溃现场捕获器（仅诊断用，不影响正常逻辑）
//  - SEH 过滤器：抓访问违规/除零等硬件异常 -> bfplayer_crash.txt(含调用栈+模块名)
//  - Qt 消息钩子：抓 Qt 断言失败(qFatal) -> bfplayer_qfatal.txt(含 文件:行号)
//  - SIGABRT 钩子：abort() 路径也写 crash
// 回传后用 addr2line -e BfPlayer.exe 把"本模块(BfPlayer)内"的帧映射到 文件:行号
// ============================================================================
#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <QMessageLogContext>

static void bfResolveModule(uintptr_t addr, char *out, size_t outLen)
{
    HMODULE hMod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &hMod) && hMod) {
        char full[MAX_PATH] = {0};
        if (GetModuleFileNameA(hMod, full, MAX_PATH)) {
            const char *slash = strrchr(full, '\\');
            strncpy(out, slash ? slash + 1 : full, outLen - 1);
            out[outLen - 1] = '\0';
            return;
        }
    }
    strncpy(out, "?", outLen - 1);
    out[outLen - 1] = '\0';
}

static void bfDumpStack(FILE *f, CONTEXT *ctx)
{
    void *frames[64];
    unsigned short n = CaptureStackBackTrace(0, 64, frames, NULL);
    HMODULE self = GetModuleHandle(NULL);
    fprintf(f, "\n调用栈 (共 %u 帧):\n", n);
    for (unsigned short i = 0; i < n; ++i) {
        uintptr_t addr = (uintptr_t)frames[i];
        char mod[MAX_PATH] = {0};
        bfResolveModule(addr, mod, sizeof(mod));
        fprintf(f, "  #%02u  VA=0x%llX  mod=%s", i,
                (unsigned long long)addr, mod);
        // 粗略判断是否属于本模块(BfPlayer)，是则附 RVA 便于 addr2line
        if (addr >= (uintptr_t)self && addr < (uintptr_t)self + 0x08000000) {
            fprintf(f, "  RVA=0x%llX", (unsigned long long)(addr - (uintptr_t)self));
        }
        fprintf(f, "\n");
    }
}

static void bfWriteCrash(const char *tag, EXCEPTION_POINTERS *ep)
{
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
        char *p = strrchr(path, '\\');
        if (p) { *(p + 1) = '\0'; strcat(path, "bfplayer_crash.txt"); }
        else   { strcpy(path, "bfplayer_crash.txt"); }
    } else {
        strcpy(path, "bfplayer_crash.txt");
    }
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "=== BfPlayer 崩溃现场 (%s) ===\n", tag);
    if (ep) {
        fprintf(f, "异常代码 : 0x%08lX\n", (unsigned long)ep->ExceptionRecord->ExceptionCode);
        fprintf(f, "异常地址 : 0x%p\n", ep->ExceptionRecord->ExceptionAddress);
        CONTEXT *ctx = ep->ContextRecord;
        fprintf(f, "RIP=0x%p  RSP=0x%p  RBP=0x%p\n",
                (void *)ctx->Rip, (void *)ctx->Rsp, (void *)ctx->Rbp);
        bfDumpStack(f, ctx);
    }
    fclose(f);
}

static LONG WINAPI bfExceptionFilter(EXCEPTION_POINTERS *ep)
{
    bfWriteCrash("SEH", ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void bfAbortHandler(int)
{
    bfWriteCrash("ABORT", NULL);
    _exit(3);
}

static void bfQtMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (type == QtFatalMsg) {
        char path[MAX_PATH];
        if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
            char *p = strrchr(path, '\\');
            if (p) { *(p + 1) = '\0'; strcat(path, "bfplayer_qfatal.txt"); }
        } else {
            strcpy(path, "bfplayer_qfatal.txt");
        }
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "=== Qt 断言失败 / Fatal ===\n");
            fprintf(f, "文件 : %s\n", ctx.file ? ctx.file : "?");
            fprintf(f, "行号 : %d\n", ctx.line);
            fprintf(f, "函数 : %s\n", ctx.function ? ctx.function : "?");
            fprintf(f, "消息 : %s\n", msg.toUtf8().constData());
            fclose(f);
        }
    }
    fprintf(stderr, "%s\n", msg.toUtf8().constData());
    if (type == QtFatalMsg) abort();
}
#endif

int main(int argc, char *argv[])
{
    // 初始化 FFmpeg 网络支持（必须在主线程中调用一次）
    avformat_network_init();

#ifdef Q_OS_WIN
    // 安装崩溃捕获器（诊断用，定位音频路径崩溃的精确行）
    SetUnhandledExceptionFilter(bfExceptionFilter);
    signal(SIGABRT, bfAbortHandler);
    qInstallMessageHandler(bfQtMessageHandler);
#endif

    // 启用高 DPI 支持（Qt5.14+ 已默认启用，保留以兼容旧版本）
    // QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    app.setApplicationName("BfPlayer");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("BfPlayer");
    app.setWindowIcon(QIcon(":/icon"));

    MainWindow w;
    w.setWindowIcon(QIcon(":/icon"));
    w.show();

    // 处理命令行参数：双击文件关联打开时自动播放
    QStringList args = app.arguments();
    if (args.size() > 1) {
        QString filePath = args.at(1);
        if (!filePath.isEmpty()) {
            w.openFileFromArgs(filePath);
        }
    }

    int result = app.exec();

    // 清理 FFmpeg 网络支持
    avformat_network_deinit();

    return result;
}
