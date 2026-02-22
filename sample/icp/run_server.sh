#!/bin/bash
# ICP Server 启动脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN_DIR="$PROJECT_ROOT/bin"

# 默认端口
PORT=${1:-9000}

echo "========================================"
echo "        启动 ICP Server"
echo "========================================"
echo "项目根目录: $PROJECT_ROOT"
echo "可执行文件: $BIN_DIR/icp_server"
echo "监听端口: $PORT"
echo "----------------------------------------"

# 检查可执行文件
if [ ! -f "$BIN_DIR/icp_server" ]; then
    echo "错误: $BIN_DIR/icp_server 不存在"
    echo "请先编译项目:"
    echo "  cd $PROJECT_ROOT/build && make"
    exit 1
fi

# 运行服务器
exec "$BIN_DIR/icp_server" $PORT
