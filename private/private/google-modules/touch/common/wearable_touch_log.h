#ifndef _WEAR_TOUCH_LOG_H_
#define _WEAR_TOUCH_LOG_H_

#ifndef WTI_LOGE
#define WTI_LOGE(fmt, args...) pr_err(fmt, ##args)
#endif /*WTI_LOGE*/

#ifndef WTI_LOGW
#define WTI_LOGW(fmt, args...) pr_warn(fmt, ##args)
#endif /*WTI_LOGW*/

#ifndef WTI_LOGI
#define WTI_LOGI(fmt, args...) pr_info(fmt, ##args)
#endif /*WTI_LOGI*/

#ifndef WTI_LOGD
#define WTI_LOGD(fmt, args...) pr_debug(fmt, ##args)
#endif /*WTI_LOGD*/

#endif /*_WEAR_TOUCH_LOG_H_*/