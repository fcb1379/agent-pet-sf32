#ifndef NOISE_MONITOR_H
#define NOISE_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#include <rtthread.h>

/* NOISE_MONITOR_STATE: 噪声采集服务状态。
 * 成员说明：
 *   - NOISE_MONITOR_STATE_IDLE: 麦克风未采集。
 *   - NOISE_MONITOR_STATE_STARTING: 正在打开音频采集链路。
 *   - NOISE_MONITOR_STATE_RUNNING: 正常接收并计算 PCM 数据。
 *   - NOISE_MONITOR_STATE_SUSPENDED: 音频链路被更高优先级业务挂起。
 *   - NOISE_MONITOR_STATE_ERROR: 麦克风采集启动或运行失败。
 */
typedef enum _NOISE_MONITOR_STATE
{
    NOISE_MONITOR_STATE_IDLE = 0,
    NOISE_MONITOR_STATE_STARTING,
    NOISE_MONITOR_STATE_RUNNING,
    NOISE_MONITOR_STATE_SUSPENDED,
    NOISE_MONITOR_STATE_ERROR
} NOISE_MONITOR_STATE;

/* NOISE_MONITOR_SNAPSHOT: 噪声采集结果快照，供 UI 线程只读访问。
 * 成员说明：
 *   - bRunning: true 表示采集服务已请求运行。
 *   - bValid: true 表示 ucDb 含有最近一个统计窗口的有效值。
 *   - ucDb: 估算声压级，范围 30~120 dB。
 *   - eState: 当前采集服务状态。
 *   - lLastError: 最近错误码，0 表示无错误。
 *   - ulSampleCount: 最近统计窗口实际使用的 PCM 样本数。
 *   - ulGeneration: 有效测量结果更新代数，UI 可用它判断数据变化。
 */
typedef struct _NOISE_MONITOR_SNAPSHOT
{
    bool bRunning;
    bool bValid;
    uint8_t ucDb;
    NOISE_MONITOR_STATE eState;
    int32_t lLastError;
    uint32_t ulSampleCount;
    uint32_t ulGeneration;
} NOISE_MONITOR_SNAPSHOT;

rt_err_t NOISEMONITOR_Start(void);
rt_err_t NOISEMONITOR_Stop(void);
rt_err_t NOISEMONITOR_GetSnapshot(NOISE_MONITOR_SNAPSHOT *pSnapshot);

#endif /* NOISE_MONITOR_H */
