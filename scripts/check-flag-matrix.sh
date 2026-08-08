#!/usr/bin/env bash
set -euo pipefail

# Compiles every supported feature-flag combination for every project.
# A flag combination is only "supported" if it has a green row here.
#
# Flags are injected with --build-property "compiler.cpp.extra_flags=..."
# because -D defines win over the #ifndef defaults in ProjectConfig.h and
# also reach the shared library translation units. Do NOT switch this to
# build.extra_flags: on esp32p4 the platform owns build.extra_flags.esp32p4
# for USB defines, and overriding it clobbers them.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/project-registry.sh"
FQBN="${FQBN:-esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600}"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/_arduino-build}/flag-matrix"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Run scripts/install-cores.sh first." >&2
  exit 1
fi

CTAGS_ARGS=()
if [[ "${CTAGS_WORKAROUND:-0}" == "1" ]]; then
  echo "Using local ctags workaround: tools.ctags.cmd.path=/usr/bin/true"
  CTAGS_ARGS+=(--build-property "tools.ctags.cmd.path=/usr/bin/true")
fi

have_lib() {
  arduino-cli lib list "$1" 2>/dev/null | awk 'NR>1 {found=1} END {exit !found}'
}

P1="in-progress/01-fieldops-control-center"
P2="projects/02-cypher-vision-cam"
P3="in-progress/03-badgeops-nfc-rfid-system"
P4="projects/04-relayops-wifi-control-hub"
P5="projects/05-cypherdrive-wireless-ops"
P7="projects/07-nfc-field-lab-badgeops-pro"
P8="projects/08-cypher-gamer-arcade"
P9="projects/09-cypher-tune-mpc"
P10="projects/10-litego-touch-coach"
P11="in-progress/11-cardrf-spectrum-console"
P13="projects/13-surveyops-wardriver-panel"
P14="projects/14-adsb-flight-tracker-radar"
P15="projects/15-pokedex-panel"
P16="in-progress/16-cypher-flock-panel"
P17="projects/17-littlehakr-rf-lab"
P18="projects/18-cypher-desk-panel"
P19="in-progress/19-starbeam-console"
P20="projects/20-pipboy-terminal"
P21="projects/21-cypher-keys-hid-deck"
P22="projects/22-cypher-boy"
P24="in-progress/24-acid-glass-visualizer"

# Rows: "<project>|<tag>|<flags>|<required libs, comma-separated>"
ROWS=(
  "$P1|baseline||"
  "$P2|baseline||"
  "$P3|baseline||"
  "$P4|baseline||"
  "$P5|baseline||"
  "$P7|baseline||"
  "$P8|baseline||"
  "$P9|baseline||"
  "$P10|baseline||"
  "$P11|baseline||"
  "$P13|baseline||"
  "$P14|baseline||"
  "$P15|baseline||ArduinoJson"
  "$P16|baseline||ArduinoJson"
  "$P17|baseline||"
  "$P18|baseline||"
  "$P19|baseline||"
  "$P20|baseline||"
  "$P21|baseline||"
  "$P22|baseline||"
  "$P24|baseline||"
  "$P1|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P2|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P3|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P4|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P5|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P7|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P8|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P9|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P10|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P11|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P13|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P14|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P15|display|-DUSE_DISPLAY=1|ArduinoJson,GFX Library for Arduino,SensorLib,U8g2"
  "$P16|display|-DUSE_DISPLAY=1|ArduinoJson,GFX Library for Arduino,SensorLib"
  "$P17|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P18|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib,U8g2"
  "$P19|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P20|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P21|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P22|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P1|wifi|-DUSE_WIFI=1|"
  "$P2|wifi|-DUSE_WIFI=1|"
  "$P3|wifi|-DUSE_WIFI=1|"
  "$P4|wifi|-DUSE_WIFI=1|ArduinoJson"
  "$P4|kitchen-sink|-DUSE_DISPLAY=1 -DUSE_WIFI=1|ArduinoJson,GFX Library for Arduino,SensorLib"
  "$P14|wifi|-DUSE_WIFI=1|ArduinoJson"
  "$P14|kitchen-sink|-DUSE_DISPLAY=1 -DUSE_WIFI=1|ArduinoJson,GFX Library for Arduino,SensorLib"
  "$P1|lora|-DUSE_LORA_DRIVER=1|RadioLib"
  "$P1|espnow|-DUSE_ESPNOW=1|"
  "$P1|espnow-display|-DUSE_ESPNOW=1 -DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P1|kitchen-sink|-DUSE_LORA_DRIVER=1 -DUSE_DISPLAY=1 -DUSE_WIFI=1|RadioLib,GFX Library for Arduino,SensorLib"
  "$P2|camera|-DUSE_CAMERA_DRIVER=1|"
  "$P2|camera-display|-DUSE_CAMERA_DRIVER=1 -DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P2|sd|-DUSE_CAM_SD=1|"
  "$P2|kitchen-sink|-DUSE_CAMERA_DRIVER=1 -DUSE_DISPLAY=1 -DUSE_WIFI=1 -DUSE_CAM_SD=1|GFX Library for Arduino,SensorLib"
  "$P3|pn532|-DUSE_PN532_DRIVER=1|Adafruit PN532"
  "$P3|mfrc522|-DUSE_MFRC522_DRIVER=1|MFRC522"
  "$P3|both-readers|-DUSE_PN532_DRIVER=1 -DUSE_MFRC522_DRIVER=1|Adafruit PN532,MFRC522"
  "$P3|kitchen-sink|-DUSE_PN532_DRIVER=1 -DUSE_MFRC522_DRIVER=1 -DUSE_DISPLAY=1 -DUSE_WIFI=1|Adafruit PN532,MFRC522,GFX Library for Arduino,SensorLib"
  "$P5|wifi-active|-DUSE_DISPLAY=1 -DUSE_WIFI_ACTIVE=1|GFX Library for Arduino,SensorLib"
  "$P5|ble-c6|-DUSE_DISPLAY=1 -DUSE_BLE_C6=1|GFX Library for Arduino,SensorLib"
  "$P5|ble-hid|-DUSE_DISPLAY=1 -DUSE_BLE_HID=1|GFX Library for Arduino,SensorLib"
  "$P5|usb-hid-mock|-DUSE_DISPLAY=1 -DUSE_USB_HID=1|GFX Library for Arduino,SensorLib"
  "$P5|kitchen-sink|-DUSE_DISPLAY=1 -DUSE_WIFI_ACTIVE=1 -DUSE_BLE_C6=1 -DUSE_BLE_HID=1|GFX Library for Arduino,SensorLib"
  "$P5|sd|-DUSE_DISPLAY=1 -DUSE_CYPHERDRIVE_SD=1|GFX Library for Arduino,SensorLib"
  "$P5|full-active|-DUSE_DISPLAY=1 -DUSE_WIFI_ACTIVE=1 -DUSE_BLE_C6=1 -DUSE_CYPHERDRIVE_SD=1|GFX Library for Arduino,SensorLib"
  "$P7|pn532|-DUSE_PN532_DRIVER=1|Adafruit PN532"
  "$P7|mfrc522|-DUSE_MFRC522_DRIVER=1|MFRC522"
  "$P7|both-readers|-DUSE_PN532_DRIVER=1 -DUSE_MFRC522_DRIVER=1|Adafruit PN532,MFRC522"
  "$P8|sd-highscores|-DUSE_SD_HIGHSCORES=1|"
  "$P8|display-sd-highscores|-DUSE_DISPLAY=1 -DUSE_SD_HIGHSCORES=1|GFX Library for Arduino,SensorLib"
  "$P9|audio|-DUSE_AUDIO=1|"
  "$P9|audio-sd|-DUSE_AUDIO=1 -DUSE_MPC_SD=1|"
  "$P9|display-audio|-DUSE_DISPLAY=1 -DUSE_AUDIO=1|GFX Library for Arduino,SensorLib"
  "$P10|display-rules|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P11|rf-uart-bridge|-DUSE_RF_UART_BRIDGE=1|"
  "$P13|gps|-DUSE_GPS_DRIVER=1|TinyGPSPlus"
  "$P13|wifi-scan|-DUSE_WIFI_SCAN=1|"
  "$P13|sd-wigle|-DUSE_SD_WIGLE_LOG=1|"
  "$P13|survey-hardware-gated|-DUSE_GPS_DRIVER=1 -DUSE_WIFI_SCAN=1 -DUSE_SD_WIGLE_LOG=1|TinyGPSPlus"
  "$P15|sd-pokedex|-DUSE_SD_POKEDEX=1|ArduinoJson"
  "$P15|display-sd-pokedex|-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1|ArduinoJson,GFX Library for Arduino,SensorLib,U8g2"
  "$P16|flock-uart|-DUSE_FLOCK_UART_BRIDGE=1|ArduinoJson"
  "$P16|flock-persistence|-DUSE_FLOCK_PERSISTENCE=1|ArduinoJson"
  "$P16|flock-c6-witness|-DUSE_FLOCK_C6_WITNESS=1|ArduinoJson"
  "$P16|flock-c6-display|-DUSE_DISPLAY=1 -DUSE_FLOCK_C6_WITNESS=1|ArduinoJson,GFX Library for Arduino,SensorLib"
  "$P16|flock-full|-DUSE_DISPLAY=1 -DUSE_FLOCK_UART_BRIDGE=1 -DUSE_FLOCK_PERSISTENCE=1 -DUSE_FLOCK_C6_WITNESS=1|ArduinoJson,GFX Library for Arduino,SensorLib"
  "$P17|detector-locked|-DUSE_RF_LAB_DETECTOR=1|"
  "$P17|detector-persistence|-DUSE_RF_LAB_DETECTOR=1 -DUSE_RF_LAB_PERSISTENCE=1|"
  "$P17|c6-pages|-DUSE_DISPLAY=1 -DUSE_RF_LAB_C6_WIFI=1 -DUSE_RF_LAB_C6_BLE=1|GFX Library for Arduino,SensorLib"
  "$P19|radios|-DUSE_STARBEAM_RADIOS=1|RF24,SmartRC-CC1101-Driver-Lib"
  "$P19|coproc|-DUSE_STARBEAM_COPROC=1|"
  "$P19|full|-DUSE_DISPLAY=1 -DUSE_STARBEAM_RADIOS=1 -DUSE_STARBEAM_COPROC=1|RF24,SmartRC-CC1101-Driver-Lib,GFX Library for Arduino,SensorLib"
  "$P18|sd-workspace|-DUSE_CYPHER_DESK_SD=1|"
  "$P18|display-sd-workspace|-DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1|GFX Library for Arduino,SensorLib,U8g2"
  "$P18|display-sd-time|-DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_WIFI=1|GFX Library for Arduino,SensorLib,U8g2"
  "$P18|display-sd-audio|-DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_CYPHER_DESK_AUDIO=1|GFX Library for Arduino,SensorLib,U8g2"
  "$P18|recorder-guard|-DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_CYPHER_DESK_RECORDER=1|GFX Library for Arduino,SensorLib,U8g2"
  "$P18|display-sd-media|-DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_CYPHER_DESK_MEDIA=1 -DUSE_CYPHER_DESK_AUDIO=1|GFX Library for Arduino,SensorLib,U8g2"
  "$P18|display-sd-video|-DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_CYPHER_DESK_AUDIO=1 -DUSE_CYPHER_DESK_MEDIA=1 -DUSE_CYPHER_DESK_VIDEO=1|GFX Library for Arduino,SensorLib,U8g2"
  "$P18|full-os|-DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_WIFI=1 -DUSE_CYPHER_DESK_AUDIO=1 -DUSE_CYPHER_DESK_MEDIA=1 -DUSE_CYPHER_DESK_VIDEO=1 -DUSE_CYPHER_DESK_RECORDER=1|GFX Library for Arduino,SensorLib,U8g2"
  "$P20|sd|-DUSE_PIPBOY_SD=1|"
  "$P20|display-sd|-DUSE_DISPLAY=1 -DUSE_PIPBOY_SD=1|GFX Library for Arduino,SensorLib"
  "$P20|display-sd-audio|-DUSE_DISPLAY=1 -DUSE_PIPBOY_SD=1 -DUSE_PIPBOY_AUDIO=1|GFX Library for Arduino,SensorLib"
  "$P20|display-wifi|-DUSE_DISPLAY=1 -DUSE_WIFI=1|ArduinoJson,GFX Library for Arduino,SensorLib"
  "$P20|full|-DUSE_DISPLAY=1 -DUSE_PIPBOY_SD=1 -DUSE_PIPBOY_AUDIO=1 -DUSE_WIFI=1|ArduinoJson,GFX Library for Arduino,SensorLib"
  # Project 21 exercises the USB-HID flag here under the shared USBMode=hwcdc
  # FQBN, where the backend deliberately falls back to MOCK (native USB is
  # CDC/JTAG). The real USB-OTG HID device is a separate build documented in the
  # project's TECHNICAL.md - it needs USBMode=default and cannot run in this
  # single-FQBN matrix.
  "$P21|usb-hid-mock|-DUSE_DISPLAY=1 -DUSE_USB_HID=1|GFX Library for Arduino,SensorLib"
  # The Bluetooth path compiles here too (BLE does not need USB-OTG). The BLE lib
  # ships with the core (no install), so it is not listed as a required lib.
  "$P21|ble-hid-mock|-DUSE_DISPLAY=1 -DUSE_BLE_HID=1|GFX Library for Arduino,SensorLib"
  # Synthesized mechanical key clicks out of the NS4168 I2S amp (src/KeyAudio).
  # The amp path itself is proven by project 09; this row only proves the click
  # engine compiles and that it vanishes cleanly without the flag.
  "$P21|key-audio|-DUSE_DISPLAY=1 -DUSE_CYPHER_KEYS_AUDIO=1|GFX Library for Arduino,SensorLib"
  # Real recorded switch samples loaded off SD (src/KeySoundPacks). No audio is
  # vendored in the repo, so this row proves the loader compiles and that it
  # leaves the baseline/display builds alone - it cannot prove a card reads.
  "$P21|key-audio-sd|-DUSE_DISPLAY=1 -DUSE_CYPHER_KEYS_AUDIO=1 -DUSE_CYPHER_KEYS_SD=1|GFX Library for Arduino,SensorLib"
  # Project 22 loads ROMs and writes battery saves through SD_MMC's FAT VFS
  # (gnuboy does its own stdio file I/O). Verify SD_MMC actually appears under
  # <build-path>/libraries/ for these rows - a green build alone does not prove
  # the library linked. See src/gnuboy/VENDORED.md.
  "$P22|gb-sd|-DUSE_GB_SD=1|"
  "$P22|display-gb-sd|-DUSE_DISPLAY=1 -DUSE_GB_SD=1|GFX Library for Arduino,SensorLib"
  "$P22|gb-audio|-DUSE_GB_AUDIO=1|"
  "$P22|full|-DUSE_DISPLAY=1 -DUSE_GB_SD=1 -DUSE_GB_AUDIO=1|GFX Library for Arduino,SensorLib"
  # Genesis needs -Wno-incompatible-pointer-types for the vendored gwenesis core
  # (upstream sets the same suppression in its own CMakeLists; GCC 14 promoted
  # that warning to an error). EXTRA_C_FLAGS reaches only the C compiler.
  "$P22|genesis|-DUSE_GENESIS_CORE=1|"
  "$P22|genesis-full|-DUSE_DISPLAY=1 -DUSE_GB_SD=1 -DUSE_GB_AUDIO=1 -DUSE_GENESIS_CORE=1|GFX Library for Arduino,SensorLib"
  "$P22|nes|-DUSE_NES_CORE=1|"
  "$P22|nes-full|-DUSE_DISPLAY=1 -DUSE_GB_SD=1 -DUSE_GB_AUDIO=1 -DUSE_NES_CORE=1|GFX Library for Arduino,SensorLib"
  "$P22|all-systems|-DUSE_DISPLAY=1 -DUSE_GB_SD=1 -DUSE_GB_AUDIO=1 -DUSE_GENESIS_CORE=1 -DUSE_NES_CORE=1|GFX Library for Arduino,SensorLib"
  # Project 24 renders a 256x150 RGB565 surface and uses the P4 PPA for an
  # exact 4x scale into the panel framebuffer. SD audio and browser control are
  # separate rows so hardware bring-up can add one service at a time.
  "$P24|display-demo|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P24|display-ppa|-DUSE_DISPLAY=1 -DACID_GLASS_USE_PPA=1|GFX Library for Arduino,SensorLib"
  "$P24|display-touch-ppa|-DUSE_DISPLAY=1 -DACID_GLASS_TOUCH_ENABLED=1 -DACID_GLASS_USE_PPA=1|GFX Library for Arduino,SensorLib"
  "$P24|display-sd|-DUSE_DISPLAY=1 -DACID_GLASS_TOUCH_ENABLED=1 -DACID_GLASS_USE_PPA=1 -DUSE_ACID_GLASS_SD=1|GFX Library for Arduino,SensorLib"
  "$P24|sd-audio|-DUSE_ACID_GLASS_SD=1 -DUSE_ACID_GLASS_AUDIO=1|"
  "$P24|display-sd-audio|-DUSE_DISPLAY=1 -DACID_GLASS_TOUCH_ENABLED=1 -DACID_GLASS_USE_PPA=1 -DUSE_ACID_GLASS_SD=1 -DUSE_ACID_GLASS_AUDIO=1|GFX Library for Arduino,SensorLib"
  "$P24|display-remote|-DUSE_DISPLAY=1 -DUSE_WIFI=1 -DUSE_ACID_GLASS_REMOTE=1|GFX Library for Arduino,SensorLib"
  "$P24|full|-DUSE_DISPLAY=1 -DACID_GLASS_TOUCH_ENABLED=1 -DACID_GLASS_USE_PPA=1 -DUSE_ACID_GLASS_SD=1 -DUSE_ACID_GLASS_AUDIO=1 -DUSE_WIFI=1 -DUSE_ACID_GLASS_REMOTE=1 -DACID_GLASS_REMOTE_AUTOSTART=1|GFX Library for Arduino,SensorLib"
)

echo "Flag matrix: ${#ROWS[@]} rows on $FQBN"
echo

RESULTS=()
FAILED=0

for ROW in "${ROWS[@]}"; do
  IFS='|' read -r PROJECT TAG FLAGS LIBS <<<"$ROW"
  NAME="$(basename "$PROJECT")"
  LABEL="$NAME [$TAG]"

  MISSING=""
  if [[ -n "$LIBS" ]]; then
    IFS=',' read -ra LIB_LIST <<<"$LIBS"
    for LIB in "${LIB_LIST[@]}"; do
      if ! have_lib "$LIB"; then
        MISSING="$LIB"
        break
      fi
    done
  fi
  if [[ -n "$MISSING" ]]; then
    echo "==> $LABEL: SKIP (missing library: $MISSING - run scripts/install-libs.sh)"
    RESULTS+=("SKIP  $LABEL (missing: $MISSING)")
    continue
  fi

  # Rows whose vendored C needs upstream's own warning suppression. gwenesis
  # carries both a host and an embedded framebuffer path with mismatched
  # pointer types; upstream sets -Wno-incompatible-pointer-types in its
  # CMakeLists, and GCC 14 promoted that warning to an error.
  EXTRA_C_FLAGS=""
  if [[ "$FLAGS" == *"USE_GENESIS_CORE=1"* ]]; then
    EXTRA_C_FLAGS="-Wno-incompatible-pointer-types -Wno-error=incompatible-pointer-types"
  fi
  # nofrendo: upstream's own warning suppressions, plus NES6502_JUMPTABLE because
  # Arduino's P4 flags include -fno-jump-tables, which would otherwise degrade the
  # 256-case opcode switch to a compare tree.
  if [[ "$FLAGS" == *"USE_NES_CORE=1"* ]]; then
    EXTRA_C_FLAGS="$EXTRA_C_FLAGS -DNES6502_JUMPTABLE -Wno-array-bounds -Wno-error=format -Wno-format -Wno-incompatible-pointer-types -Wno-error=incompatible-pointer-types"
  fi

  FLAG_ARGS=()
  if [[ -n "$FLAGS" ]]; then
    # Pass the same -D defines to BOTH the C++ and the C compiler.
    # compiler.cpp.extra_flags does NOT reach .c files: a project with a
    # flag-gated .c (project 22's vendored gwenesis core) would compile every
    # such file to zero bytes and still report a green build - a silent no-op.
    # Same failure family as the __has_include linkage trap.
    FLAG_ARGS+=(--build-property "compiler.cpp.extra_flags=$FLAGS")
    FLAG_ARGS+=(--build-property "compiler.c.extra_flags=$FLAGS $EXTRA_C_FLAGS")
  fi

  echo "==> $LABEL"
  mkdir -p "$BUILD_ROOT/$NAME-$TAG"
  if arduino-cli compile \
    --fqbn "$FQBN" \
    --libraries "$ROOT/shared" \
    --build-path "$BUILD_ROOT/$NAME-$TAG" \
    ${FLAG_ARGS[@]+"${FLAG_ARGS[@]}"} \
    ${CTAGS_ARGS[@]+"${CTAGS_ARGS[@]}"} \
    "$ROOT/$PROJECT" >"$BUILD_ROOT/$NAME-$TAG.log" 2>&1; then
    RESULTS+=("PASS  $LABEL")
  else
    RESULTS+=("FAIL  $LABEL (log: ${BUILD_ROOT#$ROOT/}/$NAME-$TAG.log)")
    FAILED=1
    tail -20 "$BUILD_ROOT/$NAME-$TAG.log"
  fi
done

echo
echo "==== Flag matrix summary ===="
for LINE in "${RESULTS[@]}"; do
  echo "$LINE"
done

exit $FAILED
