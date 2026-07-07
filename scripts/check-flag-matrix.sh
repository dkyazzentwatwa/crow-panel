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

P1="projects/01-fieldops-control-center"
P2="projects/02-vision-guard-inspection-kiosk"
P3="projects/03-badgeops-nfc-rfid-system"
P4="projects/04-relayops-wifi-control-hub"
P5="projects/05-cypherdrive-wireless-ops"
P6="projects/06-wiretap-benchops-console"
P7="projects/07-nfc-field-lab-badgeops-pro"
P8="projects/08-cypher-gamer-arcade"
P9="projects/09-cypher-tune-mpc"
P10="projects/10-litego-touch-coach"
P11="projects/11-cardrf-spectrum-console"
P12="projects/12-creatorops-board"
P13="projects/13-surveyops-wardriver-panel"
P14="projects/14-adsb-flight-tracker-radar"

# Rows: "<project>|<tag>|<flags>|<required libs, comma-separated>"
ROWS=(
  "$P1|baseline||"
  "$P2|baseline||"
  "$P3|baseline||"
  "$P4|baseline||"
  "$P5|baseline||"
  "$P6|baseline||"
  "$P7|baseline||"
  "$P8|baseline||"
  "$P9|baseline||"
  "$P10|baseline||"
  "$P11|baseline||"
  "$P12|baseline||"
  "$P13|baseline||"
  "$P14|baseline||"
  "$P1|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P2|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P3|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P4|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P5|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P6|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P7|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P8|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P9|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P10|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P11|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P12|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P13|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P14|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
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
  "$P3|pn532|-DUSE_PN532_DRIVER=1|Adafruit PN532"
  "$P3|mfrc522|-DUSE_MFRC522_DRIVER=1|MFRC522"
  "$P3|both-readers|-DUSE_PN532_DRIVER=1 -DUSE_MFRC522_DRIVER=1|Adafruit PN532,MFRC522"
  "$P3|kitchen-sink|-DUSE_PN532_DRIVER=1 -DUSE_MFRC522_DRIVER=1 -DUSE_DISPLAY=1 -DUSE_WIFI=1|Adafruit PN532,MFRC522,GFX Library for Arduino,SensorLib"
  "$P5|wifi-scan|-DUSE_WIFI_SCAN=1|"
  "$P5|ble-bridge|-DUSE_BLE_UART_BRIDGE=1|"
  "$P5|qr-persist|-DUSE_QR_PERSISTENCE=1|"
  "$P5|hardware-gated|-DUSE_WIFI_SCAN=1 -DUSE_BLE_UART_BRIDGE=1 -DUSE_QR_PERSISTENCE=1|"
  "$P6|bench-probes|-DUSE_BENCH_PROBES=1|"
  "$P6|spi-id-clocked|-DUSE_BENCH_PROBES=1 -DWIRETAP_ALLOW_SPI_ID_CLOCKING=1|"
  "$P7|pn532|-DUSE_PN532_DRIVER=1|Adafruit PN532"
  "$P7|mfrc522|-DUSE_MFRC522_DRIVER=1|MFRC522"
  "$P7|both-readers|-DUSE_PN532_DRIVER=1 -DUSE_MFRC522_DRIVER=1|Adafruit PN532,MFRC522"
  "$P8|sd-highscores|-DUSE_SD_HIGHSCORES=1|"
  "$P8|display-sd-highscores|-DUSE_DISPLAY=1 -DUSE_SD_HIGHSCORES=1|GFX Library for Arduino,SensorLib"
  "$P9|audio|-DUSE_AUDIO=1|"
  "$P10|display-rules|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P11|rf-uart-bridge|-DUSE_RF_UART_BRIDGE=1|"
  "$P12|creatorops-api|-DUSE_CREATOROPS_API=1|"
  "$P13|gps|-DUSE_GPS_DRIVER=1|TinyGPSPlus"
  "$P13|wifi-scan|-DUSE_WIFI_SCAN=1|"
  "$P13|sd-wigle|-DUSE_SD_WIGLE_LOG=1|"
  "$P13|survey-hardware-gated|-DUSE_GPS_DRIVER=1 -DUSE_WIFI_SCAN=1 -DUSE_SD_WIGLE_LOG=1|TinyGPSPlus"
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

  FLAG_ARGS=()
  if [[ -n "$FLAGS" ]]; then
    FLAG_ARGS+=(--build-property "compiler.cpp.extra_flags=$FLAGS")
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
