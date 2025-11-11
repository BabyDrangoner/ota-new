#ifndef _SHERRY_REDIS_UTIL_H__
#define _SHERRY_REDIS_UTIL_H__

#include <string>

namespace sherry{

/**
 * @brief 设置 Redis key-value，支持设置过期时间
 * @param pool_name Redis 连接池名称
 * @param key Redis key
 * @param value Redis value
 * @param expire_seconds 过期时间（秒），0 表示不过期
 * @return 1 成功，0 失败
 */
int redis_set_key_value(const std::string& pool_name, const std::string& key, const std::string& value, int expire_seconds = 0);

/**
 * @brief 重置 Redis key 的值（先删除再设置）
 * @param pool_name Redis 连接池名称
 * @param key Redis key
 * @param value Redis value
 * @param expire_seconds 过期时间（秒），0 表示不过期
 * @return 1 成功，0 失败
 */
int redis_reset_value(const std::string& pool_name, const std::string& key, const std::string& value, int expire_seconds = 0);

/**
 * @brief 删除 Redis key
 * @param pool_name Redis 连接池名称
 * @param key Redis key
 * @return 1 成功，0 失败
 */
int redis_del_key(const std::string& pool_name, const std::string& key);

/**
 * @brief 获取 Redis key 的值
 * @param pool_name Redis 连接池名称
 * @param key Redis key
 * @param value 输出参数，存储获取到的值
 * @return 1 成功，0 失败或 key 不存在
 */
int redis_get_key_value(const std::string& pool_name, const std::string& key, std::string& value);

/**
 * @brief 检查 Redis key 是否存在
 * @param pool_name Redis 连接池名称
 * @param key Redis key
 * @return 1 存在，0 不存在
 */
int redis_key_exists(const std::string& pool_name, const std::string& key);

/**
 * @brief 设置 Redis key 的过期时间
 * @param pool_name Redis 连接池名称
 * @param key Redis key
 * @param expire_seconds 过期时间（秒）
 * @return 1 成功，0 失败
 */
int redis_set_expire(const std::string& pool_name, const std::string& key, int expire_seconds);

} // namespace sherry

#endif