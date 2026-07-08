#!/usr/bin/env bash

# Canonical project list for CrowPanel sketches. Keep paths relative to the
# repository root so scripts can source this file without guessing cwd.
crowpanel_projects() {
  cat <<'PROJECTS'
projects/01-fieldops-control-center
projects/02-vision-guard-inspection-kiosk
projects/03-badgeops-nfc-rfid-system
projects/04-relayops-wifi-control-hub
projects/05-cypherdrive-wireless-ops
projects/06-wiretap-benchops-console
projects/07-nfc-field-lab-badgeops-pro
projects/08-cypher-gamer-arcade
projects/09-cypher-tune-mpc
projects/10-litego-touch-coach
projects/11-cardrf-spectrum-console
projects/12-creatorops-board
projects/13-surveyops-wardriver-panel
projects/14-adsb-flight-tracker-radar
projects/15-pokedex-panel
PROJECTS
}

crowpanel_project_exists() {
  local needle="$1"
  local project
  while IFS= read -r project; do
    [[ "$project" == "$needle" || "$(basename "$project")" == "$needle" ]] && return 0
  done < <(crowpanel_projects)
  return 1
}
