#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#define SOCKET_PATH "/var/run/immutable_service.sock"
#define MAX_PATH_LEN 4096
#define AUTH_TOKEN "test_token_change_me_in_production"  // 需与服务端一致

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

// 连接到服务
int connect_to_service() {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("无法创建socket");
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("无法连接到服务");
        close(sock_fd);
        return -1;
    }
    
    return sock_fd;
}

// 修改不可变文件
int modify_immutable_file(const char *path, const char *data, size_t data_len) {
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return -1;
    }
    
    // 准备请求头
    request_header req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_MODIFY;
    strncpy(req.path, path, MAX_PATH_LEN - 1);
    strncpy(req.token, AUTH_TOKEN, sizeof(req.token) - 1);
    req.data_len = data_len;
    
    // 发送请求头
    if (send(sock_fd, &req, sizeof(req), 0) != sizeof(req)) {
        perror("发送请求失败");
        close(sock_fd);
        return -1;
    }
    
    // 发送数据
    if (send(sock_fd, data, data_len, 0) != data_len) {
        perror("发送数据失败");
        close(sock_fd);
        return -1;
    }
    
    // 接收回应
    char response[4096];
    ssize_t recv_len = recv(sock_fd, response, sizeof(response) - 1, 0);
    if (recv_len > 0) {
        response[recv_len] = '\0';
        printf("%s\n", response);
    } else {
        perror("接收回应失败");
    }
    
    close(sock_fd);
    return (recv_len > 0 && strstr(response, "成功") != NULL) ? 0 : -1;
}

// 使用rsync进行增量更新
int rsync_immutable_file(const char *src_path, const char *dst_path) {
    // 检查源文件是否存在
    struct stat st;
    if (stat(src_path, &st) != 0) {
        fprintf(stderr, "源文件不存在: %s\n", src_path);
        return -1;
    }
    
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return -1;
    }
    
    // 准备请求头
    request_header req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_RSYNC;
    strncpy(req.path, dst_path, MAX_PATH_LEN - 1);
    strncpy(req.src_path, src_path, MAX_PATH_LEN - 1);
    strncpy(req.token, AUTH_TOKEN, sizeof(req.token) - 1);
    req.data_len = 0;
    
    // 发送请求头
    if (send(sock_fd, &req, sizeof(req), 0) != sizeof(req)) {
        perror("发送请求失败");
        close(sock_fd);
        return -1;
    }
    
    // 接收回应
    char response[4096];
    ssize_t recv_len = recv(sock_fd, response, sizeof(response) - 1, 0);
    if (recv_len > 0) {
        response[recv_len] = '\0';
        printf("%s\n", response);
    } else {
        perror("接收回应失败");
    }
    
    close(sock_fd);
    return (recv_len > 0 && strstr(response, "成功") != NULL) ? 0 : -1;
}

// 删除不可变文件
int delete_immutable_file(const char *path) {
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return -1;
    }
    
    // 准备请求头
    request_header req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_DELETE;
    strncpy(req.path, path, MAX_PATH_LEN - 1);
    strncpy(req.token, AUTH_TOKEN, sizeof(req.token) - 1);
    req.data_len = 0;
    
    // 发送请求头
    if (send(sock_fd, &req, sizeof(req), 0) != sizeof(req)) {
        perror("发送请求失败");
        close(sock_fd);
        return -1;
    }
    
    // 接收回应
    char response[4096];
    ssize_t recv_len = recv(sock_fd, response, sizeof(response) - 1, 0);
    if (recv_len > 0) {
        response[recv_len] = '\0';
        printf("%s\n", response);
    } else {
        perror("接收回应失败");
    }
    
    close(sock_fd);
    return (recv_len > 0 && strstr(response, "成功") != NULL) ? 0 : -1;
}

// 设置文件保留期限
int set_immutable_retention(const char *path, time_t retention_seconds) {
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return -1;
    }
    
    // 准备请求头
    request_header req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_SET_RETENTION;
    strncpy(req.path, path, MAX_PATH_LEN - 1);
    strncpy(req.token, AUTH_TOKEN, sizeof(req.token) - 1);
    req.retention_time = retention_seconds;
    req.data_len = 0;
    
    // 发送请求头
    if (send(sock_fd, &req, sizeof(req), 0) != sizeof(req)) {
        perror("发送请求失败");
        close(sock_fd);
        return -1;
    }
    
    // 接收回应
    char response[4096];
    ssize_t recv_len = recv(sock_fd, response, sizeof(response) - 1, 0);
    if (recv_len > 0) {
        response[recv_len] = '\0';
        printf("%s\n", response);
    } else {
        perror("接收回应失败");
    }
    
    close(sock_fd);
    return (recv_len > 0 && strstr(response, "成功") != NULL) ? 0 : -1;
}

// 获取文件剩余保留时间
time_t get_immutable_retention(const char *path) {
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return -1;
    }
    
    // 准备请求头
    request_header req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_GET_RETENTION;
    strncpy(req.path, path, MAX_PATH_LEN - 1);
    strncpy(req.token, AUTH_TOKEN, sizeof(req.token) - 1);
    req.data_len = 0;
    
    // 发送请求头
    if (send(sock_fd, &req, sizeof(req), 0) != sizeof(req)) {
        perror("发送请求失败");
        close(sock_fd);
        return -1;
    }
    
    // 接收回应
    char response[4096];
    ssize_t recv_len = recv(sock_fd, response, sizeof(response) - 1, 0);
    time_t remaining = 0;
    
    if (recv_len > 0) {
        response[recv_len] = '\0';
        printf("%s\n", response);
        
        // 尝试解析剩余时间
        const char *time_str = strstr(response, "剩余保留时间:");
        if (time_str) {
            time_str += strlen("剩余保留时间:");
            sscanf(time_str, "%ld", &remaining);
        }
    } else {
        perror("接收回应失败");
    }
    
    close(sock_fd);
    return remaining;
}

// 使用示例主函数
#ifdef EXAMPLE_MAIN
int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("用法:\n");
        printf("  修改文件:   %s modify <文件路径> <内容>\n", argv[0]);
        printf("  删除文件:   %s delete <文件路径>\n", argv[0]);
        printf("  增量更新:   %s rsync <源文件> <目标文件>\n", argv[0]);
        printf("  设置保留期: %s setretention <文件路径> <保留秒数>\n", argv[0]);
        printf("  查询保留期: %s getretention <文件路径>\n", argv[0]);
        return 1;
    }
    
    const char *cmd = argv[1];
    const char *path = argv[2];
    
    if (strcmp(cmd, "modify") == 0) {
        if (argc < 4) {
            printf("修改命令需要提供文件内容\n");
            return 1;
        }
        const char *content = argv[3];
        return modify_immutable_file(path, content, strlen(content));
    } 
    else if (strcmp(cmd, "delete") == 0) {
        return delete_immutable_file(path);
    }
    else if (strcmp(cmd, "rsync") == 0) {
        if (argc < 4) {
            printf("rsync命令需要提供源文件和目标文件\n");
            return 1;
        }
        const char *dst_path = argv[3];
        return rsync_immutable_file(path, dst_path);
    }
    else if (strcmp(cmd, "setretention") == 0) {
        if (argc < 4) {
            printf("设置保留期命令需要提供保留秒数\n");
            return 1;
        }
        time_t seconds = atol(argv[3]);
        return set_immutable_retention(path, seconds);
    }
    else if (strcmp(cmd, "getretention") == 0) {
        time_t remaining = get_immutable_retention(path);
        if (remaining >= 0) {
            printf("文件 %s 的剩余保留时间: %ld 秒\n", path, remaining);
            return 0;
        }
        return 1;
    } 
    else {
        printf("未知命令: %s\n", cmd);
        return 1;
    }
}
#endif 