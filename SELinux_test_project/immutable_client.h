#ifndef IMMUTABLE_CLIENT_H
#define IMMUTABLE_CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * 修改不可变文件内容
 * 
 * @param path 文件路径
 * @param data 文件内容
 * @param data_len 内容长度
 * @return 成功返回 0，失败返回 -1
 */
int modify_immutable_file(const char *path, const char *data, size_t data_len);

/**
 * 删除不可变文件
 * 
 * @param path 文件路径
 * @return 成功返回 0，失败返回 -1
 */
int delete_immutable_file(const char *path);

/**
 * 使用rsync进行增量更新
 * 
 * @param src_path 源文件路径
 * @param dst_path 目标文件路径
 * @return 成功返回 0，失败返回 -1
 */
int rsync_immutable_file(const char *src_path, const char *dst_path);

/**
 * 设置文件保留期限
 * 
 * @param path 文件路径
 * @param retention_seconds 保留期限(秒)
 * @return 成功返回 0，失败返回 -1
 */
int set_immutable_retention(const char *path, time_t retention_seconds);

/**
 * 获取文件剩余保留时间
 * 
 * @param path 文件路径
 * @return 剩余秒数，失败返回 -1
 */
time_t get_immutable_retention(const char *path);

#endif /* IMMUTABLE_CLIENT_H */ 