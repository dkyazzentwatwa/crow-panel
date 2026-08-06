#!/usr/bin/env bash

# Canonical project lists for CrowPanel sketches. Keep paths relative to the
# repository root so scripts can source this file without guessing cwd.
#
# Two tiers, both built by compile-all.sh:
#   projects/     — release-facing, documented in README.md
#   in-progress/  — still being worked on, deliberately not featured up front
# Numbering is shared across both tiers, so a project keeps its number when it
# graduates; moving it is a `git mv` plus a line move between these two lists.

crowpanel_release_projects() {
  cat <<'PROJECTS'
projects/02-cypher-vision-cam
projects/04-relayops-wifi-control-hub
projects/05-cypherdrive-wireless-ops
projects/07-nfc-field-lab-badgeops-pro
projects/08-cypher-gamer-arcade
projects/09-cypher-tune-mpc
projects/10-litego-touch-coach
projects/13-surveyops-wardriver-panel
projects/14-adsb-flight-tracker-radar
projects/15-pokedex-panel
projects/17-littlehakr-rf-lab
projects/18-cypher-desk-panel
projects/20-pipboy-terminal
projects/21-cypher-keys-hid-deck
projects/22-cypher-boy
PROJECTS
}

crowpanel_inprogress_projects() {
  cat <<'PROJECTS'
in-progress/01-fieldops-control-center
in-progress/03-badgeops-nfc-rfid-system
in-progress/11-cardrf-spectrum-console
in-progress/16-cypher-flock-panel
in-progress/19-starbeam-console
in-progress/24-acid-glass-visualizer
in-progress/25-inkwell
PROJECTS
}

# Every project, both tiers. This is what the build scripts iterate.
crowpanel_projects() {
  crowpanel_release_projects
  crowpanel_inprogress_projects
}

crowpanel_project_exists() {
  local needle="$1"
  local project
  while IFS= read -r project; do
    [[ "$project" == "$needle" || "$(basename "$project")" == "$needle" ]] && return 0
  done < <(crowpanel_projects)
  return 1
}
