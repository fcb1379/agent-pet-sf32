#ifndef RT_HW_H
#define RT_HW_H

typedef unsigned long rt_base_t;

static inline rt_base_t rt_hw_interrupt_disable(void)
{
    return 0UL;
}

static inline void rt_hw_interrupt_enable(rt_base_t level)
{
    (void)level;
}

#endif /* RT_HW_H */
