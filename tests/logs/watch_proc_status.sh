#!/bin/bash

# === 自动获取 PID ===
if [[ "$1" =~ ^[0-9]+$ ]]; then
    PID=$1
else
    PID=$(ps -ef | grep "./test_ota_server" | grep -v grep | awk '{print $2}' | head -n 1)
fi

PORT=${2:-8020}
IFACE=${3:-eth0}

if [ -z "$PID" ] || [ ! -d /proc/$PID ]; then
    echo "❌ Process './test_ota_server' not found or not running."
    exit 1
fi

# === 获取初始网卡字节数 ===
get_net_bytes() {
    cat /proc/net/dev | grep "$IFACE" | awk -F '[: ]+' '{print $2, $10}'
}

read RX0 TX0 < <(get_net_bytes)

echo "Monitoring PID=$PID, PORT=$PORT, IFACE=$IFACE..."
echo "TIME      FD  CONN  CPU(%)  RSS(KB)  NET_IN  NET_OUT"
echo "----------------------------------------------------"

while true; do
    TIME=$(date +%T)

    # 文件描述符数量
    FD_COUNT=$(ls /proc/$PID/fd 2>/dev/null | wc -l)

    # 活跃 TCP 连接数（该端口 ESTABLISHED）
    CONN_COUNT=$(ss -tan state established "( sport = :$PORT )" | wc -l)

    # CPU 占用
    CPU=$(ps -p $PID -o %cpu= | awk '{printf "%.1f", $1}')

    # 物理内存（单位：KB）
    RSS=$(ps -p $PID -o rss= | awk '{printf "%d", $1}')

     # === 网卡流量 ===
    read RX_NOW TX_NOW < <(awk -v iface="$IFACE" '$1 ~ iface":" {gsub(":", "", $1); print $2, $10}' /proc/net/dev)
    RX_DIFF=$((RX_NOW - RX_LAST))
    TX_DIFF=$((TX_NOW - TX_LAST))
    RX_LAST=$RX_NOW
    TX_LAST=$TX_NOW

    # === 自动换算单位函数 ===
    format_bytes() {
        local bytes=$1
        if [ "$bytes" -ge 1048576 ]; then
            awk "BEGIN{printf \"%.2fMB\", $bytes/1048576}"
        elif [ "$bytes" -ge 1024 ]; then
            awk "BEGIN{printf \"%.2fKB\", $bytes/1024}"
        else
            echo "${bytes}B"
        fi
    }

    RX_RATE=$(format_bytes $RX_DIFF)
    TX_RATE=$(format_bytes $TX_DIFF)

 
    # 打印
    printf "[%s]  %4d  %4d  %6s  %7s  %7s  %8s\n" "$TIME" "$FD_COUNT" "$CONN_COUNT" "$CPU" "$RSS" "$RX_RATE" "$TX_RATE"

    sleep 1
done
