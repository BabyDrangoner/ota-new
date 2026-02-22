#ifndef __SHERRY_ICP_H__
#define __SHERRY_ICP_H__

/**
 * @file icp.h
 * @brief ICP (Image Control Plane) 模块统一头文件
 * 
 * ICP 模块实现了从设备端接收图像数据并与vLLM进行推理交互的完整流程。
 * 
 * 核心特性:
 * - Latest-Only: 同一车辆的旧消息/旧推理可被立即作废
 * - 单buffer覆盖: 每车仅维护一个RxSlot, 不积压旧消息
 * - Early Drop: 从header判断过期后立即丢弃整条消息
 * - 异步vLLM: submit/abort/stream接收都不阻塞控制线程
 * 
 * 模块组成:
 * - icp_protocol: 消息协议定义与解析
 * - icp_rx_slot: 每车单槽覆盖缓存
 * - icp_car_state: 车辆状态管理
 * - icp_config: 配置管理
 * - icp_metrics: 可观测性指标
 * - icp_vllm_client: vLLM HTTP客户端
 * - icp_controller: 控制平面
 * - icp_server: TCP服务器
 * 
 * 使用示例:
 * @code
 * #include "sherry/icp/icp.h"
 * 
 * // 加载配置
 * auto config = sherry::icp::IcpConfig::loadFromFile("icp.yaml");
 * if (!config) {
 *     config = sherry::icp::IcpConfig::getDefault();
 * }
 * 
 * // 创建并启动服务
 * auto service = std::make_shared<sherry::icp::IcpService>(config);
 * if (!service->init()) {
 *     return -1;
 * }
 * 
 * service->start();
 * 
 * // ... 运行 ...
 * 
 * service->stop();
 * @endcode
 */

#include "icp_protocol.h"
#include "icp_rx_slot.h"
#include "icp_car_state.h"
#include "icp_config.h"
#include "icp_metrics.h"
#include "icp_vllm_client.h"
#include "icp_controller.h"
#include "icp_server.h"

#endif // __SHERRY_ICP_H__
