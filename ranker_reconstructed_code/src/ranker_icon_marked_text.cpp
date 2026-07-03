#include "ranker_icon_marked_text.h"

#include "ranker_system_ui.h"

#include <cstring>

namespace ranker {

int FindInlineIconMarker(const char* text) {
    for (int offset = 0;; ++offset) {
        const unsigned char ch = static_cast<unsigned char>(text[offset]);
        if (ch == '\n' || ch == '\r' || ch == '\0') {
            return -1;
        }

        if (ch == '(' && text[offset + 2] == ')' &&
            static_cast<unsigned char>(text[offset + 1]) > static_cast<unsigned char>('`') &&
            static_cast<unsigned char>(text[offset + 1]) <= 0x7f) {
            return offset;
        }
    }
}

#ifdef _WIN32
int MeasureIconMarkedTextWidth(HDC dc, const char* text) {
    char segment[256];
    std::memset(segment, 0xcc, sizeof(segment));
    int width = 0;
    int offset = 0;
    for (;;) {
        const int marker = FindInlineIconMarker(text + offset);
        if (marker < 0) {
            width += MeasureGdiTextWidth(dc, text + offset);
            return width;
        }

        if (marker > 0) {
            // The original copies the pre-marker bytes but measures from the
            // source pointer, so the remaining string is counted here.
            std::strncpy(segment, text + offset, static_cast<std::size_t>(marker));
            width += MeasureGdiTextWidth(dc, text + offset);
            offset += marker;
        }
        width += kInlineIconMarkerWidth;
        offset += 3;
    }
}
#endif

}
