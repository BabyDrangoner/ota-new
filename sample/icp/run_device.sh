#!/bin/bash
# 设备客户端启动脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN_DIR="$PROJECT_ROOT/bin"

# 默认参数
SERVER_IP=${1:-127.0.0.1}
SERVER_PORT=${2:-9000}
CAR_ID=${3:-1}

echo "========================================"
echo "        启动设备客户端"
echo "========================================"
echo "项目根目录: $PROJECT_ROOT"
echo "可执行文件: $BIN_DIR/device_client"
echo "服务器: $SERVER_IP:$SERVER_PORT"
echo "车辆ID: $CAR_ID"
echo "----------------------------------------"

# 检查可执行文件
if [ ! -f "$BIN_DIR/device_client" ]; then
    echo "错误: $BIN_DIR/device_client 不存在"
    echo "请先编译项目:"
    echo "  cd $PROJECT_ROOT/build && make"
    exit 1
fi

# 运行客户端
exec "$BIN_DIR/device_client" $SERVER_IP $SERVER_PORT $CAR_ID
