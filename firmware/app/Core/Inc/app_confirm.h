#ifndef APP_CONFIRM_H
#define APP_CONFIRM_H

typedef enum {
    APP_CONFIRM_OK = 0,
    APP_CONFIRM_NOT_PENDING,
    APP_CONFIRM_META_FULL,
    APP_CONFIRM_FLASH_ERROR
} AppConfirmResult;

AppConfirmResult app_confirm_running_image(void);

#endif
