# ===== build stage =====
FROM docker.m.daocloud.io/library/ubuntu:22.04 AS build
ENV DEBIAN_FRONTEND=noninteractive

# 编译期依赖（含 Boost 头，解决 lexical_cast.hpp）
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git \
    libssl-dev libyaml-cpp-dev libboost-all-dev ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
# 拷项目（建议配合 .dockerignore 排除 build/、.git/ 等）
COPY . /app

# 安装你随仓库提供的 MQTT 动态库和 cmake 元数据
RUN mkdir -p /usr/local/lib64 \
 && cp -a /app/lib64/* /usr/local/lib64/ \
 && echo "/usr/local/lib64" > /etc/ld.so.conf.d/local-mqtt.conf \
 && ldconfig

# 统一用全新构建目录，避免宿主机残留的 CMakeCache 影响
RUN cmake -S /app -B /tmp/build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/usr/local \
      -DCMAKE_CXX_STANDARD=17 \
      -DCMAKE_VERBOSE_MAKEFILE=ON \
 && cmake --build /tmp/build -j

# ===== runtime stage =====
FROM docker.m.daocloud.io/library/ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive

# 运行期依赖（boost 仅头文件使用，不需安装）
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 libyaml-cpp0.7 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# 带上你提供的 paho 动态库
COPY --from=build /usr/local/lib64/ /usr/local/lib64/
RUN echo "/usr/local/lib64" > /etc/ld.so.conf.d/local-mqtt.conf && ldconfig

# 非 root 运行
RUN useradd -m -u 10001 app
USER app

# ★ 把下面的可执行文件路径改成你的真实产物：
#   - 若你的二进制是 /tmp/build/bin/test_server（常见于 sylar 工程），用这一行：
COPY --from=build /tmp/build/bin/test_server /usr/local/bin/ota_server
#   - 若你的目标名是 /tmp/build/ota_server，请把上一行改为：
# COPY --from=build /tmp/build/ota_server /usr/local/bin/ota_server

EXPOSE 8020
VOLUME ["/var/log/ota", "/var/lib/ota"]
ENTRYPOINT ["/usr/local/bin/ota_server"]
