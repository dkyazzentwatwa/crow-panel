#ifndef BADGEOPS_PROJECT_CONFIG_H
#define BADGEOPS_PROJECT_CONFIG_H

#ifndef MOCK_MODE
#define MOCK_MODE 1
#endif

#ifndef USE_LVGL
#define USE_LVGL 0
#endif

#ifndef USE_WIFI
#define USE_WIFI 0
#endif

#ifndef USE_LORA_DRIVER
#define USE_LORA_DRIVER 0
#endif

#ifndef USE_CAMERA_DRIVER
#define USE_CAMERA_DRIVER 0
#endif

#ifndef USE_PN532_DRIVER
#define USE_PN532_DRIVER 0
#endif

#ifndef USE_MFRC522_DRIVER
#define USE_MFRC522_DRIVER 0
#endif

#ifndef USE_AUDIO
#define USE_AUDIO 0
#endif

#ifndef CROWPANEL_HARDWARE_PROFILE
#define CROWPANEL_HARDWARE_PROFILE 120
#endif

#define BADGEOPS_API_ENDPOINT "http://localhost:8787"

#include <AppConfig.h>

#endif
