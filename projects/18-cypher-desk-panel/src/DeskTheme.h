#ifndef CYPHER_DESK_PANEL_THEME_H
#define CYPHER_DESK_PANEL_THEME_H

#include "DeskTypes.h"

const DeskThemePalette &deskTheme(DeskThemeId id);
DeskThemeId nextDeskTheme(DeskThemeId id);
DeskThemeId deskThemeFromName(const String &name);

#endif
