#include "user32_private.h"

BOOL WINAPI SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom) {
    if (!lprc) return FALSE;
    lprc->left = xLeft;
    lprc->top = yTop;
    lprc->right = xRight;
    lprc->bottom = yBottom;
    return TRUE;
}

BOOL WINAPI SetRectEmpty(LPRECT lprc) {
    return SetRect(lprc, 0, 0, 0, 0);
}

BOOL WINAPI CopyRect(LPRECT lprcDst, const RECT* lprcSrc) {
    if (!lprcDst || !lprcSrc) return FALSE;
    *lprcDst = *lprcSrc;
    return TRUE;
}

BOOL WINAPI IsRectEmpty(const RECT* lprc) {
    return !lprc || lprc->right <= lprc->left || lprc->bottom <= lprc->top;
}

BOOL WINAPI EqualRect(const RECT* a, const RECT* b) {
    return a && b && a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

BOOL WINAPI OffsetRect(LPRECT lprc, int dx, int dy) {
    if (!lprc) return FALSE;
    lprc->left += dx;
    lprc->right += dx;
    lprc->top += dy;
    lprc->bottom += dy;
    return TRUE;
}

BOOL WINAPI InflateRect(LPRECT lprc, int dx, int dy) {
    if (!lprc) return FALSE;
    lprc->left -= dx;
    lprc->right += dx;
    lprc->top -= dy;
    lprc->bottom += dy;
    return TRUE;
}

BOOL WINAPI PtInRect(const RECT* lprc, POINT pt) {
    return lprc && pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom;
}

BOOL WINAPI IntersectRect(LPRECT dst, const RECT* a, const RECT* b) {
    if (IsRectEmpty(a) || IsRectEmpty(b) || a->left >= b->right || b->left >= a->right || a->top >= b->bottom ||
        b->top >= a->bottom) {
        SetRectEmpty(dst);
        return FALSE;
    }
    dst->left = max(a->left, b->left);
    dst->top = max(a->top, b->top);
    dst->right = min(a->right, b->right);
    dst->bottom = min(a->bottom, b->bottom);
    return TRUE;
}

BOOL WINAPI UnionRect(LPRECT dst, const RECT* a, const RECT* b) {
    if (IsRectEmpty(a)) {
        if (IsRectEmpty(b)) {
            SetRectEmpty(dst);
            return FALSE;
        }
        *dst = *b;
        return TRUE;
    }
    if (IsRectEmpty(b)) {
        *dst = *a;
        return TRUE;
    }
    dst->left = min(a->left, b->left);
    dst->top = min(a->top, b->top);
    dst->right = max(a->right, b->right);
    dst->bottom = max(a->bottom, b->bottom);
    return TRUE;
}
