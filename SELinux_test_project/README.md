# SELinux 不可变文件保护系统

这个项目实现了一个基于SELinux的不可变文件保护系统，确保文件具有不可变(immutable)属性，同时允许特定的授权进程执行修改和删除操作。

## 核心功能

1. **不可变文件保护**：文件对所有用户（包括root）都是只读的
2. **特权服务架构**：使用专用特权服务进程处理授权的修改和删除操作
3. **增量更新支持**：通过rsync实现对不可变文件的增量更新
4. **基于时间的删除限制**：为文件设置保留期，在期限内无法删除
5. **安全API认证机制**：验证对特权服务的请求

## 安全设计

1. **特权域隔离**：特权服务在系统启动时直接以特权域运行，禁止从其他域转换
2. **SELinux强制访问控制**：所有非特权域（包括root域）不能修改不可变文件
3. **systemd安全加固**：服务使用多种保护机制确保其安全性
4. **认证机制**：所有API请求需要进行认证

## 系统要求

- Linux系统（CentOS/RHEL 7+或Fedora/Ubuntu 18.04+）
- SELinux启用并处于强制模式
- 系统工具：gcc, make, rsync
- SELinux开发工具：checkpolicy, semodule-utils, libselinux-devel

## 安装

1. 克隆代码仓库：
   ```bash
   git clone https://github.com/Ameriux/SELinux_test_project.git
   cd SELinux_test_project
   ```

2. 运行安装脚本（需要root权限）：
   ```bash
   sudo ./install.sh
   ```

安装脚本会：
- 安装必要的依赖
- 编译代码
- 安装SELinux策略模块
- 设置系统服务
- 创建示例文件

## 使用方法

### 修改不可变文件

```bash
immutable_client modify /path/to/file "新内容"
```

### 增量更新文件

```bash
immutable_client rsync /path/to/source /path/to/destination
```

### 设置文件保留期

```bash
# 设置3600秒(1小时)的保留期
immutable_client setretention /path/to/file 3600
```

### 查询文件保留期

```bash
immutable_client getretention /path/to/file
```

### 删除文件（受保留期限制）

```bash
immutable_client delete /path/to/file
```

## 开发与集成

### 使用客户端库

```c
#include <immutable_client.h>

// 修改文件
modify_immutable_file("/path/to/file", "新内容", strlen("新内容"));

// 使用rsync增量更新
rsync_immutable_file("/path/to/source", "/path/to/destination");

// 设置保留期
set_immutable_retention("/path/to/file", 3600);  // 1小时

// 获取剩余保留时间
time_t remaining = get_immutable_retention("/path/to/file");

// 删除文件
delete_immutable_file("/path/to/file");
```

编译时链接库：
```bash
gcc your_program.c -o your_program -limmutable_client
```

## 项目结构

- `immutable_policy.te` - SELinux策略模块定义
- `immutable_service.c` - 特权服务实现
- `immutable_client.c` - 客户端工具实现
- `immutable_client.h` - 客户端库头文件
- `immutable_service.service` - systemd服务定义
- `Makefile` - 构建脚本
- `install.sh` - 安装脚本

## 安全考虑

1. 服务使用严格的SELinux上下文
2. Socket通信采用权限控制访问
3. 使用systemd安全功能限制服务行为
4. 在生产环境中应使用更强健的认证机制

## 故障排除

1. **SELinux权限问题**：检查审计日志 `ausearch -m avc -ts recent`
2. **服务无法启动**：检查服务日志 `journalctl -u immutable_service`
3. **客户端无法连接**：确保socket文件存在并有正确权限

## 许可证

MIT 
