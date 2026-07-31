#ifndef CYPHER_DESK_PANEL_THEME_H
#define CYPHER_DESK_PANEL_THEME_H

#include "DeskTypes.h"

const DeskThemePalette &deskTheme(DeskThemeId id);
DeskThemeId nextDeskTheme(DeskThemeId id);
const char *deskThemeName(DeskThemeId id);
// Case-insensitive prefix match. Themes are stored by NAME rather than index so
// adding a palette never silently changes which one a user is on.
DeskThemeId deskThemeFromName(const String &name);

#endif
