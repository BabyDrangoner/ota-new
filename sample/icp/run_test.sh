#!/bin/bash
# 完整测试脚本 - 同时启动 ICP Server 和 Device Client
# 用于测试两端通信

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN_DIR="$PROJECT_ROOT/bin"

# 配置参数
PORT=${1:-9000}
TEST_DURATION=${2:-10}  # 测试持续时间(秒)
NUM_DEVICES=${3:-1}     # 设备数量

# 日志目录
LOG_DIR="$SCRIPT_DIR/logs"
mkdir -p "$LOG_DIR"

echo "========================================"
echo "      ICP 完整通信测试"
echo "========================================"
echo "项目根目录: $PROJECT_ROOT"
echo "监听端口: $PORT"
echo "测试时长: ${TEST_DURATION}s"
echo "设备数量: $NUM_DEVICES"
echo "日志目录: $LOG_DIR"
echo "========================================"

# 检查可执行文件
check_binaries() {
    local missing=0
    
    if [ ! -f "$BIN_DIR/icp_server" ]; then
        echo "❌ 缺少: $BIN_DIR/icp_server"
        missing=1
    else
        echo "✓ 找到: icp_server"
    fi
    
    if [ ! -f "$BIN_DIR/device_client" ]; then
        echo "❌ 缺少: $BIN_DIR/device_client"
        missing=1
    else
        echo "✓ 找到: device_client"
    fi
    
    if [ $missing -eq 1 ]; then
        echo ""
        echo "请先编译项目:"
        echo "  cd $PROJECT_ROOT/build && make"
        exit 1
    fi
}

# 清理函数
cleanup() {
    echo ""
    echo "正在清理进程..."
    
    # 终止所有子进程
    if [ ! -z "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
        echo "  - ICP Server (PID: $SERVER_PID) 已停止"
    fi
    
    for i in "${!DEVICE_PIDS[@]}"; do
        kill ${DEVICE_PIDS[$i]} 2>/dev/null
        wait ${DEVICE_PIDS[$i]} 2>/dev/null
        echo "  - Device Client $((i+1)) (PID: ${DEVICE_PIDS[$i]}) 已停止"
    done
    
    echo "清理完成"
}

# 设置信号处理
trap cleanup EXIT INT TERM

# 检查依赖
check_binaries
echo "----------------------------------------"

# 启动 ICP Server
echo "[$(date '+%H:%M:%S')] 启动 ICP Server..."
"$BIN_DIR/icp_server" $PORT > "$LOG_DIR/server.log" 2>&1 &
SERVER_PID=$!
echo "  - PID: $SERVER_PID"
echo "  - 日志: $LOG_DIR/server.log"

# 等待服务器启动
sleep 1

# 检查服务器是否启动成功
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "❌ ICP Server 启动失败"
    cat "$LOG_DIR/server.log"
    exit 1
fi
echo "  ✓ ICP Server 已启动"

echo "----------------------------------------"

# 启动设备客户端
DEVICE_PIDS=()
for i in $(seq 1 $NUM_DEVICES); do
    echo "[$(date '+%H:%M:%S')] 启动 Device Client $i (car_id=$i)..."
    "$BIN_DIR/device_client" 127.0.0.1 $PORT $i > "$LOG_DIR/device_$i.log" 2>&1 &
    DEVICE_PIDS+=($!)
    echo "  - PID: ${DEVICE_PIDS[$((i-1))]}"
    echo "  - 日志: $LOG_DIR/device_$i.log"
    sleep 0.5
done

echo "----------------------------------------"
echo "[$(date '+%H:%M:%S')] 测试运行中 (${TEST_DURATION}s)..."
echo ""

# 运行测试
for i in $(seq 1 $TEST_DURATION); do
    sleep 1
    
    # 每隔几秒显示状态
    if [ $((i % 3)) -eq 0 ]; then
        echo "[$(date '+%H:%M:%S')] 进度: $i/${TEST_DURATION}s"
        
        # 显示最新的日志行
        if [ -f "$LOG_DIR/server.log" ]; then
            tail -1 "$LOG_DIR/server.log" 2>/dev/null | sed 's/^/  Server: /'
        fi
        for j in $(seq 1 $NUM_DEVICES); do
            if [ -f "$LOG_DIR/device_$j.log" ]; then
                tail -1 "$LOG_DIR/device_$j.log" 2>/dev/null | sed "s/^/  Device$j: /"
            fi
        done
    fi
done

echo ""
echo "========================================"
echo "          测试完成!"
echo "========================================"

# 显示日志摘要
echo ""
echo "--- Server 日志摘要 ---"
if [ -f "$LOG_DIR/server.log" ]; then
    tail -20 "$LOG_DIR/server.log"
else
    echo "(无日志)"
fi

for i in $(seq 1 $NUM_DEVICES); do
    echo ""
    echo "--- Device $i 日志摘要 ---"
    if [ -f "$LOG_DIR/device_$i.log" ]; then
        tail -20 "$LOG_DIR/device_$i.log"
    else
        echo "(无日志)"
    fi
done

echo ""
echo "完整日志请查看: $LOG_DIR/"

# cleanup 会在退出时自动调用
