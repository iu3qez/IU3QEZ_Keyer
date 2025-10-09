#ifndef USB_DEBUG_H
#define USB_DEBUG_H

#include "esp_err.h"

class TimelineBuffer;

esp_err_t usb_debug_init(TimelineBuffer *timeline);
void usb_debug_notify_new_timeline_data(void);

#endif // USB_DEBUG_H
