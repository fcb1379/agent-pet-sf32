#ifndef PET_IMU_H
#define PET_IMU_H

#include <stdbool.h>
#include <stdint.h>

/* PET_IMU_SAMPLE: 六轴传感器采样结果。
 * 成员说明：
 *   - sAccelX/Y/Z: 三轴加速度原始值，有符号16位补码
 *   - sGyroX/Y/Z: 三轴角速度原始值，有符号16位补码
 *   - sTemperature: 温度原始值，有符号16位补码
 *   - lAccelX/Y/ZMg: 三轴加速度，单位mg
 *   - lGyroX/Y/ZMdps: 三轴角速度，单位mdps
 *   - lTemperatureCentiC: 温度，单位0.01摄氏度
 */
typedef struct _PET_IMU_SAMPLE
{
    int16_t sAccelX;
    int16_t sAccelY;
    int16_t sAccelZ;
    int16_t sGyroX;
    int16_t sGyroY;
    int16_t sGyroZ;
    int16_t sTemperature;
    int32_t lAccelXMg;
    int32_t lAccelYMg;
    int32_t lAccelZMg;
    int32_t lGyroXMdps;
    int32_t lGyroYMdps;
    int32_t lGyroZMdps;
    int32_t lTemperatureCentiC;
} PET_IMU_SAMPLE;

int32_t PETIMU_Init(void);
int32_t PETIMU_Read(PET_IMU_SAMPLE *pSample);
int32_t PETIMU_SetEnabled(bool bEnabled);
bool PETIMU_IsReady(void);

#endif /* PET_IMU_H */
