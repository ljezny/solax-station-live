#pragma once

// Minimal logging shim.
// Project historically uses ESP-IDF/Arduino log_* macros.
// New code should prefer LOGD/LOGW/LOGE per project conventions.

#ifndef LOGD
#define LOGD(...) log_d(__VA_ARGS__)
#endif

#ifndef LOGW
#define LOGW(...) log_w(__VA_ARGS__)
#endif

#ifndef LOGE
#define LOGE(...) log_e(__VA_ARGS__)
#endif
