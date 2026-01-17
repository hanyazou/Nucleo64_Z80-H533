#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void msc_test_rtos_init(void);
void msc_test_wait(void);
void msc_test_notify(void*dev);
void msc_test(void);
void msc_test_thread_entry(unsigned long arg);

#ifdef __cplusplus
}
#endif
