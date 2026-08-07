#include "ranker_mfc_runtime.h"

#include <algorithm>

namespace ranker {

// Stateless CSize/CPoint/CRect compatibility operations and their archive
// shims live here; the stateful debug, window, and OLE runtimes remain in
// ranker_mfc_runtime.cpp.
#ifdef _WIN32
SIZE SizeConstructXY(LONG cx, LONG cy) {
    return SIZE{cx, cy};
}

SIZE SizeConstructFromSize(SIZE value) {
    return value;
}

SIZE SizeConstructFromPoint(POINT value) {
    return SIZE{value.x, value.y};
}

SIZE SizeConstructFromDWord(DWORD value) {
    return SIZE{static_cast<SHORT>(LOWORD(value)),
        static_cast<SHORT>(HIWORD(value))};
}

bool SizeEquals(SIZE left, SIZE right) {
    return left.cx == right.cx && left.cy == right.cy;
}

bool SizeNotEquals(SIZE left, SIZE right) {
    return !SizeEquals(left, right);
}

SIZE& SizeAddAssign(SIZE& value, SIZE delta) {
    value.cx += delta.cx;
    value.cy += delta.cy;
    return value;
}

SIZE& SizeSubtractAssign(SIZE& value, SIZE delta) {
    value.cx -= delta.cx;
    value.cy -= delta.cy;
    return value;
}

POINT PointConstructXY(LONG x, LONG y) {
    return POINT{x, y};
}

POINT PointConstructFromPoint(POINT value) {
    return value;
}

POINT PointConstructFromSize(SIZE value) {
    return POINT{value.cx, value.cy};
}

POINT PointConstructFromDWord(DWORD value) {
    return POINT{static_cast<SHORT>(LOWORD(value)),
        static_cast<SHORT>(HIWORD(value))};
}

void PointOffsetXY(POINT& point, LONG x, LONG y) {
    point.x += x;
    point.y += y;
}

void PointOffsetSize(POINT& point, SIZE size) {
    point.x += size.cx;
    point.y += size.cy;
}

bool PointEquals(POINT left, POINT right) {
    return left.x == right.x && left.y == right.y;
}

bool PointNotEquals(POINT left, POINT right) {
    return !PointEquals(left, right);
}

POINT& PointAddAssignSize(POINT& point, SIZE size) {
    point.x += size.cx;
    point.y += size.cy;
    return point;
}

POINT& PointSubtractAssignSize(POINT& point, SIZE size) {
    point.x -= size.cx;
    point.y -= size.cy;
    return point;
}

POINT& PointAddAssignPoint(POINT& point, POINT delta) {
    point.x += delta.x;
    point.y += delta.y;
    return point;
}

POINT& PointSubtractAssignPoint(POINT& point, POINT delta) {
    point.x -= delta.x;
    point.y -= delta.y;
    return point;
}

RECT& RectConstructDefault(RECT& rect) {
    return rect;
}

RECT& RectConstructLTRB(RECT& rect, LONG left, LONG top, LONG right,
    LONG bottom) {
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    return rect;
}

RECT& RectConstructFromRect(RECT& rect, const RECT& source) {
    ::CopyRect(&rect, &source);
    return rect;
}

RECT& RectConstructFromRectPtr(RECT& rect, const RECT* source) {
    if (source != nullptr) {
        ::CopyRect(&rect, source);
    }
    return rect;
}

RECT& RectConstructFromPointSize(RECT& rect, POINT point, SIZE size) {
    rect.left = point.x;
    rect.top = point.y;
    rect.right = point.x + size.cx;
    rect.bottom = point.y + size.cy;
    return rect;
}

RECT& RectConstructFromPoints(RECT& rect, POINT top_left, POINT bottom_right) {
    rect.left = top_left.x;
    rect.top = top_left.y;
    rect.right = bottom_right.x;
    rect.bottom = bottom_right.y;
    return rect;
}

LONG RectWidth(const RECT& rect) {
    return rect.right - rect.left;
}

LONG RectHeight(const RECT& rect) {
    return rect.bottom - rect.top;
}

void NormalizeRect(RECT& rect) {
    if (rect.right < rect.left) {
        std::swap(rect.left, rect.right);
    }
    if (rect.bottom < rect.top) {
        std::swap(rect.top, rect.bottom);
    }
}

SIZE RectSize(const RECT& rect) {
    return SizeConstructXY(RectWidth(rect), RectHeight(rect));
}

POINT& RectTopLeft(RECT& rect) {
    return *reinterpret_cast<POINT*>(&rect.left);
}

const POINT& RectTopLeftConst(const RECT& rect) {
    return *reinterpret_cast<const POINT*>(&rect.left);
}

POINT& RectBottomRight(RECT& rect) {
    return *reinterpret_cast<POINT*>(&rect.right);
}

const POINT& RectBottomRightConst(const RECT& rect) {
    return *reinterpret_cast<const POINT*>(&rect.right);
}

POINT RectCenterPoint(const RECT& rect) {
    return PointConstructXY((rect.left + rect.right) / 2,
        (rect.top + rect.bottom) / 2);
}

void RectSwapLeftRight(RECT& rect) {
    RectSwapLeftRightStatic(&rect);
}

void RectSwapLeftRightStatic(RECT* rect) {
    if (rect == nullptr) {
        return;
    }
    std::swap(rect->left, rect->right);
}

RECT* RectAsMutablePtr(RECT& rect) {
    return &rect;
}

const RECT* RectAsConstPtr(const RECT& rect) {
    return &rect;
}

RECT& RectSetLTRB(RECT& rect, LONG left, LONG top, LONG right, LONG bottom) {
    ::SetRect(&rect, left, top, right, bottom);
    return rect;
}

RECT& RectSetPoints(RECT& rect, POINT top_left, POINT bottom_right) {
    ::SetRect(&rect, top_left.x, top_left.y, bottom_right.x, bottom_right.y);
    return rect;
}

bool RectIsEmpty(const RECT& rect) {
    return ::IsRectEmpty(&rect) != FALSE;
}

bool RectIsNull(const RECT& rect) {
    return rect.left == 0 && rect.top == 0 && rect.right == 0 &&
        rect.bottom == 0;
}

bool RectPtInXY(const RECT& rect, LONG x, LONG y) {
    POINT point{x, y};
    return ::PtInRect(&rect, point) != FALSE;
}

bool RectPtInPoint(const RECT& rect, POINT point) {
    return ::PtInRect(&rect, point) != FALSE;
}

void RectSetEmpty(RECT& rect) {
    ::SetRectEmpty(&rect);
}

RECT& RectCopy(RECT& rect, const RECT& source) {
    ::CopyRect(&rect, &source);
    return rect;
}

bool RectEquals(const RECT& rect, const RECT& other) {
    return ::EqualRect(&rect, &other) != FALSE;
}

bool RectEqualsOperator(const RECT& rect, const RECT& other) {
    return RectEquals(rect, other);
}

bool RectNotEquals(const RECT& rect, const RECT& other) {
    return !RectEquals(rect, other);
}

void RectInflateXY(RECT& rect, LONG x, LONG y) {
    ::InflateRect(&rect, x, y);
}

void RectInflateSize(RECT& rect, SIZE size) {
    ::InflateRect(&rect, size.cx, size.cy);
}

void RectDeflateXY(RECT& rect, LONG x, LONG y) {
    ::InflateRect(&rect, -x, -y);
}

void RectDeflateSize(RECT& rect, SIZE size) {
    ::InflateRect(&rect, -size.cx, -size.cy);
}

void RectOffsetXY(RECT& rect, LONG x, LONG y) {
    ::OffsetRect(&rect, x, y);
}

void AfxAdjustRectangle(RECT& rect, POINT point) {
    LONG dx = 0;
    if (point.x < rect.left) {
        dx = point.x - rect.left;
    } else if (point.x > rect.right) {
        dx = point.x - rect.right;
    }

    LONG dy = 0;
    if (point.y < rect.top) {
        dy = point.y - rect.top;
    } else if (point.y > rect.bottom) {
        dy = point.y - rect.bottom;
    }
    RectOffsetXY(rect, dx, dy);
}

void RectOffsetPoint(RECT& rect, POINT point) {
    ::OffsetRect(&rect, point.x, point.y);
}

void RectOffsetSize(RECT& rect, SIZE size) {
    ::OffsetRect(&rect, size.cx, size.cy);
}

bool RectIntersect(RECT& rect, const RECT& left, const RECT& right) {
    return ::IntersectRect(&rect, &left, &right) != FALSE;
}

bool RectUnion(RECT& rect, const RECT& left, const RECT& right) {
    return ::UnionRect(&rect, &left, &right) != FALSE;
}

bool RectSubtract(RECT& rect, const RECT& left, const RECT& right) {
    return ::SubtractRect(&rect, &left, &right) != FALSE;
}

RECT& RectAssign(RECT& rect, const RECT& source) {
    return RectCopy(rect, source);
}

RECT& RectOffsetAssignSize(RECT& rect, SIZE size) {
    RectOffsetSize(rect, size);
    return rect;
}

RECT& RectOffsetAssignPoint(RECT& rect, POINT point) {
    RectOffsetPoint(rect, point);
    return rect;
}

RECT& RectOffsetSubtractAssignSize(RECT& rect, SIZE size) {
    RectOffsetXY(rect, -size.cx, -size.cy);
    return rect;
}

RECT& RectOffsetSubtractAssignPoint(RECT& rect, POINT point) {
    RectOffsetXY(rect, -point.x, -point.y);
    return rect;
}

RECT& RectInflateAssignRect(RECT& rect, const RECT& margins) {
    rect.left -= margins.left;
    rect.top -= margins.top;
    rect.right += margins.right;
    rect.bottom += margins.bottom;
    return rect;
}

RECT& RectDeflateAssignRect(RECT& rect, const RECT& margins) {
    rect.left += margins.left;
    rect.top += margins.top;
    rect.right -= margins.right;
    rect.bottom -= margins.bottom;
    return rect;
}

RECT& RectIntersectAssign(RECT& rect, const RECT& other) {
    RECT current = rect;
    RectIntersect(rect, current, other);
    return rect;
}

RECT& RectUnionAssign(RECT& rect, const RECT& other) {
    RECT current = rect;
    RectUnion(rect, current, other);
    return rect;
}

RECT RectOffsetPlusSize(const RECT& rect, SIZE size) {
    RECT result = rect;
    RectOffsetSize(result, size);
    return result;
}

RECT RectOffsetMinusSize(const RECT& rect, SIZE size) {
    RECT result = rect;
    RectOffsetXY(result, -size.cx, -size.cy);
    return result;
}

RECT RectOffsetPlusPoint(const RECT& rect, POINT point) {
    RECT result = rect;
    RectOffsetPoint(result, point);
    return result;
}

RECT RectOffsetMinusPoint(const RECT& rect, POINT point) {
    RECT result = rect;
    RectOffsetXY(result, -point.x, -point.y);
    return result;
}

RECT RectInflatedByRect(const RECT& rect, const RECT& margins) {
    RECT result = rect;
    RectInflateAssignRect(result, margins);
    return result;
}

RECT RectDeflatedByRect(const RECT& rect, const RECT& margins) {
    RECT result = rect;
    RectDeflateAssignRect(result, margins);
    return result;
}

RECT RectIntersectionValue(const RECT& left, const RECT& right) {
    RECT result{};
    RectIntersect(result, left, right);
    return result;
}

RECT RectUnionValue(const RECT& left, const RECT& right) {
    RECT result{};
    RectUnion(result, left, right);
    return result;
}

MfcArchiveCompat& ArchiveWriteSizeInline(MfcArchiveCompat& archive,
    const SIZE& size) {
    ArchiveWrite(archive, &size, sizeof(size));
    return archive;
}

MfcArchiveCompat& ArchiveWritePointInline(MfcArchiveCompat& archive,
    const POINT& point) {
    ArchiveWrite(archive, &point, sizeof(point));
    return archive;
}

MfcArchiveCompat& ArchiveWriteRectInline(MfcArchiveCompat& archive,
    const RECT& rect) {
    ArchiveWrite(archive, &rect, sizeof(rect));
    return archive;
}

MfcArchiveCompat& ArchiveReadSizeInline(MfcArchiveCompat& archive, SIZE& size) {
    ArchiveRead(archive, &size, sizeof(size));
    return archive;
}

MfcArchiveCompat& ArchiveReadPointInline(MfcArchiveCompat& archive,
    POINT& point) {
    ArchiveRead(archive, &point, sizeof(point));
    return archive;
}

MfcArchiveCompat& ArchiveReadRectInline(MfcArchiveCompat& archive, RECT& rect) {
    ArchiveRead(archive, &rect, sizeof(rect));
    return archive;
}
#endif

} // namespace ranker
