// dbz3 - Launcher UI localization.

#pragma once

#include <cstdint>

namespace dbz3::i18n {

// Sets the launcher UI language to follow the game's selected text language
// (dbz3_language, Xbox XGetLanguage id: 1=EN, 3=DE, 4=FR, 5=ES, 6=IT;
// anything else, e.g. 2=Japanese, falls back to English).
// Cheap; call once per launcher frame (the Language combo is hot-swappable).
void SetLanguage(int32_t xbox_language_id);

// Returns `es` when the launcher UI language is Spanish, the German/French/
// Italian translation when one of those is selected (table lookup keyed by the
// exact Spanish runtime string), and `en` otherwise. Every user-facing launcher
// string goes through this so the whole UI switches with the "Language"
// selector instead of being a hardcoded mix ("spanglish").
const char* T(const char* es, const char* en);

}  // namespace dbz3::i18n