#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <dirent.h>
#include <pthread.h>

#define SOCKET_PATH "/var/run/immutable_service.sock"
#define MAX_PATH_LEN 4096
#define MAX_CMD_LEN 8192
#define AUTH_TOKEN "test_token_change_me_in_production"  // 生产环境中应使用更安全的认证
#define METADATA_DIR "/var/lib/immutable_service"
#define RETENTION_FILE METADATA_DIR "/retention.db"

typedef enum {
    CMD_MODIFY = 1,     // 修改文件内容
    CMD_DELETE = 2,     // 删除文件
    CMD_RSYNC = 3,      // 使用rsync增量更新
    CMD_SET_RETENTION = 4,  // 设置保留期限
    CMD_GET_RETENTION = 5   // 获取保留期限
} command_type;

typedef struct {
    command_type cmd;
    char path[MAX_PATH_LEN];
    char token[128];
    char src_path[MAX_PATH_LEN]; // 用于rsync源路径
    time_t retention_time;       // 保留期限 (秒)
    size_t data_len;
} request_header;

typedef struct {
    char path[MAX_PATH_LEN];
    time_t creation_time;    // 创建时间
    time_t retention_time;   // 保留期限
} retention_entry;

// 全局变量
int server_fd = -1;
pthread_mutex_t retention_mutex = PTHREAD_MUTEX_INITIALIZER;

// 处理终止信号
void handle_signal(int sig) {
    syslog(LOG_NOTICE, "接收到信号 %d，关闭服务", sig);
    if (server_fd != -1) {
        close(server_fd);
        unlink(SOCKET_PATH);
    }
    closelog();
    exit(0);
}

// 设置SELinux上下文(模拟实现，实际需要libselinux)
int set_immutable_context(const char *path) {
    // 在真实实现中，使用libselinux中的函数
    // security_context_t current_context = NULL;
    // context_t context = context_new(current_context);
    // context_type_set(context, "immutable_file_t");
    // setfilecon(path, context_str(context));
    
    // 这里只是简单模拟，运行shell命令设置上下文
    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "chcon -t immutable_file_t '%s' 2>/dev/null", path);
    int ret = system(cmd);
    
    if (ret != 0) {
        syslog(LOG_ERR, "无法设置文件 %s 的SELinux上下文", path);
        return -1;
    }
    
    syslog(LOG_NOTICE, "已设置文件 %s 的SELinux上下文为 immutable_file_t", path);
    return 0;
}

// 验证请求
int authenticate_request(request_header *req) {
    // 验证token
    if (strcmp(req->token, AUTH_TOKEN) != 0) {
        syslog(LOG_WARNING, "认证失败: 错误的令牌");
        return 0;
    }
    
    // 验证路径
    if (strlen(req->path) == 0 || strlen(req->path) >= MAX_PATH_LEN) {
        syslog(LOG_WARNING, "认证失败: 无效的文件路径");
        return 0;
    }
    
    // 防止路径遍历攻击
    if (strstr(req->path, "..") != NULL) {
        syslog(LOG_WARNING, "认证失败: 路径中包含'..'");
        return 0;
    }
    
    return 1;
}

// 检查路径是否为目录
int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

// 确保目录存在
void ensure_directory_exists(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        return;  // 目录已存在
    }
    
    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
    system(cmd);
}

// 保存文件的保留期限
int save_retention_info(const char *path, time_t retention_time) {
    ensure_directory_exists(METADATA_DIR);
    
    pthread_mutex_lock(&retention_mutex);
    
    // 打开或创建保留信息文件
    FILE *f = fopen(RETENTION_FILE, "a+");
    if (!f) {
        syslog(LOG_ERR, "无法打开保留信息文件: %s", strerror(errno));
        pthread_mutex_unlock(&retention_mutex);
        return -1;
    }
    
    // 记录当前时间和保留期限
    time_t now = time(NULL);
    fprintf(f, "%s|%ld|%ld\n", path, now, retention_time);
    
    fclose(f);
    pthread_mutex_unlock(&retention_mutex);
    
    syslog(LOG_NOTICE, "已为 %s 设置保留期限: %ld秒", path, retention_time);
    return 0;
}

// 获取文件的保留期限
time_t get_retention_info(const char *path) {
    pthread_mutex_lock(&retention_mutex);
    
    FILE *f = fopen(RETENTION_FILE, "r");
    if (!f) {
        pthread_mutex_unlock(&retention_mutex);
        return 0;  // 默认无保留期限
    }
    
    char line[MAX_PATH_LEN + 100];
    time_t creation_time = 0;
    time_t retention_time = 0;
    
    // 遍历文件查找最新的保留记录
    while (fgets(line, sizeof(line), f)) {
        char file_path[MAX_PATH_LEN];
        time_t file_ctime, file_rtime;
        
        // 解析记录
        if (sscanf(line, "%[^|]|%ld|%ld", file_path, &file_ctime, &file_rtime) == 3) {
            if (strcmp(file_path, path) == 0) {
                // 找到匹配，更新记录
                creation_time = file_ctime;
                retention_time = file_rtime;
            }
        }
    }
    
    fclose(f);
    pthread_mutex_unlock(&retention_mutex);
    
    // 如果找到了记录，检查是否过期
    if (retention_time > 0) {
        time_t now = time(NULL);
        time_t expiry_time = creation_time + retention_time;
        
        // 返回剩余的保留时间
        if (now < expiry_time) {
            return expiry_time - now;
        }
    }
    
    return 0;  // 保留期已过或无保留期
}

// 检查文件是否可以删除
int can_delete_file(const char *path) {
    time_t remaining_time = get_retention_info(path);
    
    if (remaining_time > 0) {
        syslog(LOG_WARNING, "文件 %s 还在保留期内，剩余 %ld 秒", path, remaining_time);
        return 0;
    }
    
    return 1;
}

// 修改文件内容
int modify_file(const char *path, const char *data, size_t data_len) {
    // 确保目标目录存在
    char dir_path[MAX_PATH_LEN];
    strncpy(dir_path, path, sizeof(dir_path));
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        ensure_directory_exists(dir_path);
    }
    
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "无法打开文件 %s: %s", path, strerror(errno));
        return -1;
    }
    
    ssize_t written = write(fd, data, data_len);
    close(fd);
    
    if (written != data_len) {
        syslog(LOG_ERR, "写入文件 %s 时出错: %s", path, strerror(errno));
        return -1;
    }
    
    // 设置SELinux上下文
    set_immutable_context(path);
    
    syslog(LOG_NOTICE, "已成功修改文件: %s", path);
    return 0;
}

// 使用rsync进行增量更新
int rsync_update(const char *src, const char *dst) {
    if (!src || !dst || strlen(src) == 0 || strlen(dst) == 0) {
        syslog(LOG_ERR, "rsync更新的源或目标路径无效");
        return -1;
    }
    
    // 构建rsync命令
    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "rsync -a --checksum '%s' '%s'", src, dst);
    
    // 执行rsync
    int ret = system(cmd);
    if (ret != 0) {
        syslog(LOG_ERR, "rsync更新失败: %s -> %s, 返回码 %d", src, dst, ret);
        return -1;
    }
    
    // 设置目标文件的SELinux上下文
    set_immutable_context(dst);
    
    syslog(LOG_NOTICE, "已成功使用rsync更新文件: %s -> %s", src, dst);
    return 0;
}

// 删除文件
int delete_file(const char *path) {
    // 检查是否可以删除
    if (!can_delete_file(path)) {
        return -1;
    }
    
    // 检查是文件还是目录
    if (is_directory(path)) {
        // 删除目录
        char cmd[MAX_CMD_LEN];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
        if (system(cmd) != 0) {
            syslog(LOG_ERR, "无法删除目录 %s", path);
            return -1;
        }
    } else {
        // 删除文件
        if (unlink(path) == -1) {
            syslog(LOG_ERR, "无法删除文件 %s: %s", path, strerror(errno));
            return -1;
        }
    }
    
    syslog(LOG_NOTICE, "已成功删除: %s", path);
    return 0;
}

// 设置保留期限
int set_retention(const char *path, time_t retention_time) {
    // 检查文件是否存在
    struct stat st;
    if (stat(path, &st) != 0) {
        syslog(LOG_ERR, "要设置保留期的文件不存在: %s", path);
        return -1;
    }
    
    return save_retention_info(path, retention_time);
}

int main() {
    struct sockaddr_un server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
    request_header req;
    char *data_buffer = NULL;
    
    // 初始化日志系统
    openlog("immutable_service", LOG_PID, LOG_DAEMON);
    syslog(LOG_NOTICE, "不可变文件特权服务启动");
    
    // 设置信号处理
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // 创建socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "无法创建socket: %s", strerror(errno));
        return 1;
    }
    
    // 准备地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);
    
    // 删除可能存在的旧socket文件
    unlink(SOCKET_PATH);
    
    // 绑定地址
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "无法绑定socket: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    // 设置socket权限
    chmod(SOCKET_PATH, 0600);
    
    // 监听连接
    if (listen(server_fd, 5) == -1) {
        syslog(LOG_ERR, "无法监听socket: %s", strerror(errno));
        close(server_fd);
        unlink(SOCKET_PATH);
        return 1;
    }
    
    // 确保元数据目录存在
    ensure_directory_exists(METADATA_DIR);
    
    syslog(LOG_NOTICE, "等待连接在 %s", SOCKET_PATH);
    
    // 主循环
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            syslog(LOG_ERR, "接受连接失败: %s", strerror(errno));
            continue;
        }
        
        syslog(LOG_NOTICE, "接受新连接");
        
        // 接收请求头
        if (recv(client_fd, &req, sizeof(req), 0) != sizeof(req)) {
            syslog(LOG_ERR, "接收请求失败");
            close(client_fd);
            continue;
        }
        
        // 验证请求
        if (!authenticate_request(&req)) {
            const char *msg = "认证失败";
            send(client_fd, msg, strlen(msg), 0);
            close(client_fd);
            continue;
        }
        
        int result = -1;
        char response[4096] = {0};
        
        // 处理命令
        switch(req.cmd) {
            case CMD_MODIFY:
                if (req.data_len > 0) {
                    data_buffer = malloc(req.data_len);
                    if (data_buffer) {
                        if (recv(client_fd, data_buffer, req.data_len, 0) == req.data_len) {
                            result = modify_file(req.path, data_buffer, req.data_len);
                        }
                        free(data_buffer);
                    }
                }
                break;
                
            case CMD_DELETE:
                result = delete_file(req.path);
                break;
                
            case CMD_RSYNC:
                result = rsync_update(req.src_path, req.path);
                break;
                
            case CMD_SET_RETENTION:
                result = set_retention(req.path, req.retention_time);
                break;
                
            case CMD_GET_RETENTION:
                {
                    time_t remain = get_retention_info(req.path);
                    snprintf(response, sizeof(response), 
                             "文件 %s 的剩余保留时间: %ld 秒", req.path, remain);
                    result = 0;
                }
                break;
                
            default:
                syslog(LOG_WARNING, "未知命令: %d", req.cmd);
                break;
        }
        
        // 返回结果
        if (response[0] == '\0') {
            snprintf(response, sizeof(response), 
                     "%s: %s", (result == 0) ? "操作成功" : "操作失败", req.path);
        }
        
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
    }
    
    // 清理(实际上不会执行到这里，除非循环被中断)
    close(server_fd);
    unlink(SOCKET_PATH);
    closelog();
    
    return 0;
} 