#!/bin/bash
set -e

# 检查是否为root用户运行
if [ "$(id -u)" -ne 0 ]; then
    echo "此脚本必须以root用户身份运行"
    exit 1
fi

# 检查SELinux是否启用
if ! command -v selinuxenabled >/dev/null 2>&1 || ! selinuxenabled; then
    echo "SELinux未启用或未安装，请先启用SELinux"
    exit 1
fi

# 检查并安装依赖
echo "正在检查依赖..."
DEPS="gcc make rsync libselinux-devel checkpolicy semodule-utils"

if command -v apt-get >/dev/null 2>&1; then
    # Debian/Ubuntu
    apt-get update
    apt-get install -y build-essential rsync libselinux1-dev policycoreutils selinux-utils
elif command -v dnf >/dev/null 2>&1; then
    # Fedora/RHEL/CentOS
    dnf install -y $DEPS
elif command -v yum >/dev/null 2>&1; then
    # 旧版CentOS/RHEL
    yum install -y $DEPS
else
    echo "不支持的系统，请手动安装依赖: $DEPS"
    exit 1
fi

# 创建目录
echo "创建必要的目录..."
mkdir -p /var/lib/immutable_service
mkdir -p /usr/local/bin
mkdir -p /usr/local/lib
mkdir -p /usr/local/include

# 编译代码
echo "编译代码..."
make clean
make
make libimmutable_client.so

# 安装二进制文件
echo "安装二进制文件..."
install -m 755 immutable_service /usr/local/bin/
install -m 755 immutable_client /usr/local/bin/
install -m 644 libimmutable_client.so /usr/local/lib/
install -m 644 immutable_client.h /usr/local/include/
ldconfig

# 编译和安装SELinux策略
echo "编译和安装SELinux策略..."
checkmodule -M -m -o immutable_policy.mod immutable_policy.te
semodule_package -o immutable_policy.pp -m immutable_policy.mod
semodule -i immutable_policy.pp

# 创建服务文件
echo "创建服务文件..."
install -m 644 immutable_service.service /etc/systemd/system/

# 重新加载 systemd
echo "重新加载 systemd 配置..."
systemctl daemon-reload

# 启动并启用服务
echo "启动服务..."
systemctl enable immutable_service.service
systemctl start immutable_service.service

# 创建测试目录和示例
echo "创建测试目录和示例..."
mkdir -p /opt/immutable_examples
echo "这是一个测试文件，它具有不可变属性，只能通过特权服务修改。" > /tmp/test.txt
immutable_client modify /opt/immutable_examples/test.txt "$(cat /tmp/test.txt)"
rm /tmp/test.txt

# 设置24小时的保留期（可以修改）
immutable_client setretention /opt/immutable_examples/test.txt 86400

echo "测试rsync增量更新功能..."
echo "这是一个用于演示rsync增量更新的源文件" > /tmp/rsync_source.txt
immutable_client rsync /tmp/rsync_source.txt /opt/immutable_examples/rsync_example.txt
rm /tmp/rsync_source.txt

echo "========================================================"
echo "安装完成！"
echo "以下是系统中的示例文件："
echo "- /opt/immutable_examples/test.txt (设置了24小时保留期)"
echo "- /opt/immutable_examples/rsync_example.txt (rsync增量更新示例)"
echo ""
echo "使用以下命令测试功能："
echo "查看文件内容:     cat /opt/immutable_examples/test.txt"
echo "尝试直接修改(将被拒绝): echo 'test' > /opt/immutable_examples/test.txt"
echo "使用特权客户端修改:    immutable_client modify /opt/immutable_examples/test.txt \"新内容\""
echo "设置保留期:         immutable_client setretention /path/to/file 3600"
echo "查询保留期:         immutable_client getretention /path/to/file"
echo "使用rsync增量更新:    immutable_client rsync /path/to/source /path/to/dest"
echo "删除文件(受保留期限制): immutable_client delete /path/to/file"
echo "========================================================" 