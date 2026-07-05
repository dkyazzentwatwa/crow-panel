#ifndef CROW_PANEL_APP_CONFIG_H
#define CROW_PANEL_APP_CONFIG_H

#define CROWPANEL_P4_7IN_V1_0 100
#define CROWPANEL_P4_7IN_V1_1 110
#define CROWPANEL_P4_7IN_V1_2 120

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
#define CROWPANEL_HARDWARE_PROFILE CROWPANEL_P4_7IN_V1_2
#endif

#endif
