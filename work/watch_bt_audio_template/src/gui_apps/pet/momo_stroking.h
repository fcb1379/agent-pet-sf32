#ifndef MOMO_STROKING_H
#define MOMO_STROKING_H

#include <stdbool.h>
#include <stdint.h>

typedef enum _MOMO_STROKE_STATE
{
    MOMO_STROKE_STATE_IDLE = 0,
    MOMO_STROKE_STATE_CANDIDATE,
    MOMO_STROKE_STATE_STROKING,
    MOMO_STROKE_STATE_RELEASING
} MOMO_STROKE_STATE;

typedef enum _MOMO_STROKE_EVENT
{
    MOMO_STROKE_EVENT_NONE = 0,
    MOMO_STROKE_EVENT_STARTED,
    MOMO_STROKE_EVENT_ENDED,
    MOMO_STROKE_EVENT_CANCELLED
} MOMO_STROKE_EVENT;

/* MOMO_STROKE_CONFIG: 摸摸 Momo 判定参数，全部使用舞台局部像素和毫秒。
 * 成员说明：
 *   - usStageWidth/usStageHeight: 可交互舞台尺寸
 *   - usHeadLeft/usHeadRight/usHeadTop/usHeadBottom: 含边界头部热区
 *   - usSampleMs: 最小轨迹采样周期
 *   - usDwellMs: 停留识别时间
 *   - usGentleMs: 温和轨迹识别时间
 *   - usReleaseMs: 松手结束防抖时间
 *   - usLeaveMs: 离开头部热区的结束时间
 *   - usStationaryDistance: 停留路径上限
 *   - usGentleMinDistance/usGentleMaxDistance: 温和轨迹距离范围
 *   - usMaxStepDistance: 单采样窗口最大距离
 *   - ucInsidePercent: 识别所需热区采样占比
 */
typedef struct _MOMO_STROKE_CONFIG
{
    uint16_t usStageWidth;
    uint16_t usStageHeight;
    uint16_t usHeadLeft;
    uint16_t usHeadRight;
    uint16_t usHeadTop;
    uint16_t usHeadBottom;
    uint16_t usSampleMs;
    uint16_t usDwellMs;
    uint16_t usGentleMs;
    uint16_t usReleaseMs;
    uint16_t usLeaveMs;
    uint16_t usStationaryDistance;
    uint16_t usGentleMinDistance;
    uint16_t usGentleMaxDistance;
    uint16_t usMaxStepDistance;
    uint8_t ucInsidePercent;
} MOMO_STROKE_CONFIG;

/* MOMO_STROKE_CONTEXT: 固定内存轨迹摘要，不保存坐标数组或外部指针。
 * 成员说明：
 *   - eState: 当前判定状态
 *   - sLastX/sLastY: 上一个有效采样点
 *   - ulStartTick/ulSampleTick: 序列开始和最近采样时间
 *   - ulOutsideTick/ulReleaseTick: 离区和松手计时起点
 *   - ulTotalDistance: 饱和累计曼哈顿距离
 *   - usSampleCount/usInsideCount: 总采样和热区内采样计数
 *   - bConsumed: 当前触摸序列是否已被抚摸语义消费
 *   - bOutside: 当前是否位于头部热区外
 */
typedef struct _MOMO_STROKE_CONTEXT
{
    MOMO_STROKE_STATE eState;
    int16_t sLastX;
    int16_t sLastY;
    uint32_t ulStartTick;
    uint32_t ulSampleTick;
    uint32_t ulOutsideTick;
    uint32_t ulReleaseTick;
    uint32_t ulTotalDistance;
    uint16_t usSampleCount;
    uint16_t usInsideCount;
    bool bConsumed;
    bool bOutside;
} MOMO_STROKE_CONTEXT;

void MOMOSTROKE_GetDefaultConfig(MOMO_STROKE_CONFIG *pConfig);
void MOMOSTROKE_Init(MOMO_STROKE_CONTEXT *pContext);
bool MOMOSTROKE_Press(
    MOMO_STROKE_CONTEXT *pContext,
    const MOMO_STROKE_CONFIG *pConfig,
    int16_t sX,
    int16_t sY,
    uint32_t ulNow);
MOMO_STROKE_EVENT MOMOSTROKE_Sample(
    MOMO_STROKE_CONTEXT *pContext,
    const MOMO_STROKE_CONFIG *pConfig,
    int16_t sX,
    int16_t sY,
    uint32_t ulNow);
MOMO_STROKE_EVENT MOMOSTROKE_Release(
    MOMO_STROKE_CONTEXT *pContext,
    uint32_t ulNow);
MOMO_STROKE_EVENT MOMOSTROKE_Poll(
    MOMO_STROKE_CONTEXT *pContext,
    const MOMO_STROKE_CONFIG *pConfig,
    uint32_t ulNow);
MOMO_STROKE_EVENT MOMOSTROKE_Cancel(MOMO_STROKE_CONTEXT *pContext);
MOMO_STROKE_EVENT MOMOSTROKE_Preempt(MOMO_STROKE_CONTEXT *pContext);
MOMO_STROKE_STATE MOMOSTROKE_GetState(const MOMO_STROKE_CONTEXT *pContext);
bool MOMOSTROKE_IsConsumed(const MOMO_STROKE_CONTEXT *pContext);

#endif /* MOMO_STROKING_H */
