/*
 *  Header control
 *
 *  Copyright 1998 Eric Kohl
 *
 *  TODO:
 *   - Imagelist support (partially).
 *   - Callback items.
 *   - Owner draw support.
 *   - Order list support.
 *   - Control specific cursors (over dividers).
 *   - Hottrack support (partially).
 *   - Custom draw support (including Notifications).
 *   - Drag and Drop support (including Notifications).
 *   - Unicode support.
 *
 *  FIXME:
 *   - Replace DrawText32A by DrawTextEx32A(...|DT_ENDELLIPSIS) in
 *     HEADER_DrawItem.
 *   - Little flaw when drawing a bitmap on the right side of the text.
 */

#include "windows.h"
#include "commctrl.h"
#include "header.h"
#include "heap.h"
#include "win.h"
#include "debug.h"


#define __HDM_LAYOUT_HACK__


#define VERT_BORDER     4
#define DIVIDER_WIDTH  10

#define HEADER_GetInfoPtr(wndPtr) ((HEADER_INFO *)wndPtr->wExtra[0])


static INT32
HEADER_DrawItem (WND *wndPtr, HDC32 hdc, HEADER_ITEM *phdi, BOOL32 bHotTrack)
{
    RECT32 r;
    INT32  oldBkMode;

    r = phdi->rect;
    if (r.right - r.left == 0)
	return phdi->rect.right;

    if (wndPtr->dwStyle & HDS_BUTTONS) {
	if (phdi->bDown) {
	    DrawEdge32 (hdc, &r, BDR_RAISEDOUTER,
			BF_RECT | BF_FLAT | BF_MIDDLE | BF_ADJUST);
	    r.left += 2;
            r.top  += 2;
	}
	else
	    DrawEdge32 (hdc, &r, EDGE_RAISED,
			BF_RECT | BF_SOFT | BF_MIDDLE | BF_ADJUST);
    }
    else
        DrawEdge32 (hdc, &r, EDGE_ETCHED, BF_BOTTOM | BF_RIGHT | BF_ADJUST);

    if (phdi->fmt & HDF_OWNERDRAW) {
        /* FIXME: owner drawn items */
    }
    else {
        UINT32 uTextJustify = DT_LEFT;

        if ((phdi->fmt & HDF_JUSTIFYMASK) == HDF_CENTER)
            uTextJustify = DT_CENTER;
        else if ((phdi->fmt & HDF_JUSTIFYMASK) == HDF_RIGHT)
            uTextJustify = DT_RIGHT;

	if ((phdi->fmt & HDF_BITMAP) && (phdi->hbm)) {
	    BITMAP32 bmp;
	    HDC32    hdcBitmap;
	    INT32    yD, yS, cx, cy, rx, ry;

	    GetObject32A (phdi->hbm, sizeof(BITMAP32), (LPVOID)&bmp);

	    ry = r.bottom - r.top;
	    rx = r.right - r.left;

	    if (ry >= bmp.bmHeight) {
		cy = bmp.bmHeight;
		yD = r.top + (ry - bmp.bmHeight) / 2;
		yS = 0;
	    }
	    else {
		cy = ry;
		yD = r.top;
		yS = (bmp.bmHeight - ry) / 2;

	    }

	    if (rx >= bmp.bmWidth + 6) {
		cx = bmp.bmWidth;
	    }
	    else {
		cx = rx - 6;
	    }

	    hdcBitmap = CreateCompatibleDC32 (hdc);
	    SelectObject32 (hdcBitmap, phdi->hbm);
	    BitBlt32 (hdc, r.left + 3, yD, cx, cy, hdcBitmap, 0, yS, SRCCOPY);
	    DeleteDC32 (hdcBitmap);

	    r.left += (bmp.bmWidth + 3);
	}


	if ((phdi->fmt & HDF_BITMAP_ON_RIGHT) && (phdi->hbm)) {
	    BITMAP32 bmp;
	    HDC32    hdcBitmap;
	    INT32    xD, yD, yS, cx, cy, rx, ry, tx;
	    RECT32   textRect;

	    GetObject32A (phdi->hbm, sizeof(BITMAP32), (LPVOID)&bmp);

	    textRect = r;
            DrawText32A(hdc, phdi->pszText, lstrlen32A(phdi->pszText),
	   	  &textRect, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_CALCRECT);
	    tx = textRect.right - textRect.left;
	    ry = r.bottom - r.top;
	    rx = r.right - r.left;

	    if (ry >= bmp.bmHeight) {
		cy = bmp.bmHeight;
		yD = r.top + (ry - bmp.bmHeight) / 2;
		yS = 0;
	    }
	    else {
		cy = ry;
		yD = r.top;
		yS = (bmp.bmHeight - ry) / 2;

	    }

	    if (r.left + tx + bmp.bmWidth + 9 <= r.right) {
		cx = bmp.bmWidth;
		xD = r.left + tx + 6;
	    }
	    else {
		if (rx >= bmp.bmWidth + 6) {
		    cx = bmp.bmWidth;
		    xD = r.right - bmp.bmWidth - 3;
		    r.right = xD - 3;
		}
		else {
		    cx = rx - 3;
		    xD = r.left;
		    r.right = r.left;
		}
	    }

	    hdcBitmap = CreateCompatibleDC32 (hdc);
	    SelectObject32 (hdcBitmap, phdi->hbm);
	    BitBlt32 (hdc, xD, yD, cx, cy, hdcBitmap, 0, yS, SRCCOPY);
	    DeleteDC32 (hdcBitmap);
	}

	if (phdi->fmt & HDF_IMAGE) {
	    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);


//	    ImageList_Draw (infoPtr->himl, phdi->iImage,...);
	}

        if ((phdi->fmt & HDF_STRING) && (phdi->pszText)) {
            oldBkMode = SetBkMode32(hdc, TRANSPARENT);
            r.left += 3;
	    r.right -= 3;
	    SetTextColor32 (hdc, bHotTrack ? COLOR_HIGHLIGHT : COLOR_BTNTEXT);
            DrawText32A(hdc, phdi->pszText, lstrlen32A(phdi->pszText),
	   	  &r, uTextJustify|DT_VCENTER|DT_SINGLELINE);
            if (oldBkMode != TRANSPARENT)
                SetBkMode32(hdc, oldBkMode);
        }
    }
    return phdi->rect.right;
}


static void 
HEADER_Refresh (WND *wndPtr, HDC32 hdc)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HFONT32 hFont, hOldFont;
    RECT32 rect;
    HBRUSH32 hbrBk;
    INT32 i, x;

    /* get rect for the bar, adjusted for the border */
    GetClientRect32 (wndPtr->hwndSelf, &rect);

    hFont = infoPtr->hFont ? infoPtr->hFont : GetStockObject32 (SYSTEM_FONT);
    hOldFont = SelectObject32 (hdc, hFont);

    /* draw Background */
    hbrBk = GetSysColorBrush32(COLOR_3DFACE);
    FillRect32(hdc, &rect, hbrBk);

    x = rect.left;
    for (i = 0; i < infoPtr->uNumItem; i++) {
        x = HEADER_DrawItem (wndPtr, hdc, &infoPtr->items[i], FALSE);
    }

    if ((x <= rect.right) && (infoPtr->uNumItem > 0)) {
        rect.left = x;
        if (wndPtr->dwStyle & HDS_BUTTONS)
            DrawEdge32 (hdc, &rect, EDGE_RAISED, BF_TOP|BF_LEFT|BF_BOTTOM|BF_SOFT);
        else
            DrawEdge32 (hdc, &rect, EDGE_ETCHED, BF_BOTTOM);
    }

    SelectObject32 (hdc, hOldFont);
}


static void
HEADER_RefreshItem (WND *wndPtr, HDC32 hdc, HEADER_ITEM *phdi)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HFONT32 hFont, hOldFont;

    hFont = infoPtr->hFont ? infoPtr->hFont : GetStockObject32 (SYSTEM_FONT);
    hOldFont = SelectObject32 (hdc, hFont);
    HEADER_DrawItem (wndPtr, hdc, phdi, FALSE);
    SelectObject32 (hdc, hOldFont);
}


static void
HEADER_SetItemBounds (WND *wndPtr)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HEADER_ITEM *phdi;
    RECT32 rect;
    int i, x;

    if (infoPtr->uNumItem == 0)
        return;

    GetClientRect32 (wndPtr->hwndSelf, &rect);

    x = rect.left;
    for (i = 0; i < infoPtr->uNumItem; i++) {
        phdi = &infoPtr->items[i];
        phdi->rect.top = rect.top;
        phdi->rect.bottom = rect.bottom;
        phdi->rect.left = x;
        phdi->rect.right = phdi->rect.left + phdi->cxy;
        x = phdi->rect.right;
    }
}


static void
HEADER_ForceItemBounds (WND *wndPtr, INT32 cy)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HEADER_ITEM *phdi;
    int i, x;

    if (infoPtr->uNumItem == 0)
	return;

    x = 0;
    for (i = 0; i < infoPtr->uNumItem; i++) {
	phdi = &infoPtr->items[i];
	phdi->rect.top = 0;
	phdi->rect.bottom = cy;
	phdi->rect.left = x;
	phdi->rect.right = phdi->rect.left + phdi->cxy;
	x = phdi->rect.right;
    }
}


static void
HEADER_InternalHitTest (WND *wndPtr, LPPOINT32 lpPt, UINT32 *pFlags, INT32 *pItem)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    RECT32 rect, rcTest;
    INT32  iCount, width;
    BOOL32 bNoWidth;

    GetClientRect32 (wndPtr->hwndSelf, &rect);

    *pFlags = 0;
    bNoWidth = FALSE;
    if (PtInRect32 (&rect, *lpPt))
    {
	if (infoPtr->uNumItem == 0) {
	    *pFlags |= HHT_NOWHERE;
	    *pItem = 1;
	    TRACE (header, "NOWHERE\n");
	    return;
	}
	else {
	    /* somewhere inside */
	    for (iCount = 0; iCount < infoPtr->uNumItem; iCount++) {
		rect = infoPtr->items[iCount].rect;
		width = rect.right - rect.left;
		if (width == 0) {
		    bNoWidth = TRUE;
		    continue;
		}
		if (PtInRect32 (&rect, *lpPt)) {
		    if (width <= 2 * DIVIDER_WIDTH) {
			*pFlags |= HHT_ONHEADER;
			*pItem = iCount;
			TRACE (header, "ON HEADER %d\n", iCount);
			return;
		    }
		    if (iCount > 0) {
			rcTest = rect;
			rcTest.right = rcTest.left + DIVIDER_WIDTH;
			if (PtInRect32 (&rcTest, *lpPt)) {
			    if (bNoWidth) {
				*pFlags |= HHT_ONDIVOPEN;
				*pItem = iCount - 1;
				TRACE (header, "ON DIVOPEN %d\n", *pItem);
				return;
			    }
			    else {
				*pFlags |= HHT_ONDIVIDER;
				*pItem = iCount - 1;
				TRACE (header, "ON DIVIDER %d\n", *pItem);
				return;
			    }
			}
		    }
		    rcTest = rect;
		    rcTest.left = rcTest.right - DIVIDER_WIDTH;
		    if (PtInRect32 (&rcTest, *lpPt)) {
			*pFlags |= HHT_ONDIVIDER;
			*pItem = iCount;
			TRACE (header, "ON DIVIDER %d\n", *pItem);
			return;
		    }

		    *pFlags |= HHT_ONHEADER;
		    *pItem = iCount;
		    TRACE (header, "ON HEADER %d\n", iCount);
		    return;
		}
	    }

	    /* check for last divider part (on nowhere) */
	    rect = infoPtr->items[infoPtr->uNumItem-1].rect;
	    rect.left = rect.right;
	    rect.right += DIVIDER_WIDTH;
	    if (PtInRect32 (&rect, *lpPt)) {
		if (bNoWidth) {
		    *pFlags |= HHT_ONDIVOPEN;
		    *pItem = infoPtr->uNumItem - 1;
		    TRACE (header, "ON DIVOPEN %d\n", *pItem);
		    return;
		}
		else {
		    *pFlags |= HHT_ONDIVIDER;
		    *pItem = infoPtr->uNumItem-1;
		    TRACE (header, "ON DIVIDER %d\n", *pItem);
		    return;
		}
	    }

	    *pFlags |= HHT_NOWHERE;
	    *pItem = 1;
	    TRACE (header, "NOWHERE\n");
	    return;
	}
    }
    else {
	if (lpPt->x < rect.left) {
	   TRACE (header, "TO LEFT\n");
	   *pFlags |= HHT_TOLEFT;
	}
	else if (lpPt->x > rect.right) {
	    TRACE (header, "TO LEFT\n");
	    *pFlags |= HHT_TORIGHT;
	}

	if (lpPt->y < rect.top) {
	    TRACE (header, "ABOVE\n");
	    *pFlags |= HHT_ABOVE;
	}
	else if (lpPt->y > rect.bottom) {
	    TRACE (header, "BELOW\n");
	    *pFlags |= HHT_BELOW;
	}
    }

    *pItem = 1;
    TRACE (header, "flags=0x%X\n", *pFlags);
    return;
}


static void
HEADER_DrawTrackLine (WND *wndPtr, HDC32 hdc, INT32 x)
{
    RECT32 rect;
    HPEN32 hOldPen;
    INT32  oldRop;

    GetClientRect32 (wndPtr->hwndSelf, &rect);

    hOldPen = SelectObject32 (hdc, GetStockObject32 (BLACK_PEN));
    oldRop = SetROP232 (hdc, R2_XORPEN);
    MoveToEx32 (hdc, x, rect.top, NULL);
    LineTo32 (hdc, x, rect.bottom);
    SetROP232 (hdc, oldRop);
    SelectObject32 (hdc, hOldPen);
}


static BOOL32
HEADER_SendSimpleNotify (WND *wndPtr, UINT32 code)
{
    NMHDR nmhdr;

    nmhdr.hwndFrom = wndPtr->hwndSelf;
    nmhdr.idFrom   = wndPtr->wIDmenu;
    nmhdr.code     = code;

    return (BOOL32)SendMessage32A (GetParent32 (wndPtr->hwndSelf), WM_NOTIFY,
				   (WPARAM32)nmhdr.idFrom, (LPARAM)&nmhdr);
}


static BOOL32
HEADER_SendHeaderNotify (WND *wndPtr, UINT32 code, INT32 iItem)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);   
    NMHEADERA nmhdr;
    HD_ITEMA  nmitem;

    nmhdr.hdr.hwndFrom = wndPtr->hwndSelf;
    nmhdr.hdr.idFrom = wndPtr->wIDmenu;
    nmhdr.hdr.code = code;
    nmhdr.iItem = iItem;
    nmhdr.iButton = 0;
    nmhdr.pitem = &nmitem;
    nmitem.mask = infoPtr->items[iItem].mask;
    nmitem.cxy = infoPtr->items[iItem].cxy;
    nmitem.hbm = infoPtr->items[iItem].hbm;
    nmitem.pszText = infoPtr->items[iItem].pszText;
    nmitem.cchTextMax = infoPtr->items[iItem].cchTextMax;
    nmitem.fmt = infoPtr->items[iItem].fmt;
    nmitem.lParam = infoPtr->items[iItem].lParam;
    nmitem.iOrder = infoPtr->items[iItem].iOrder;
    nmitem.iImage = infoPtr->items[iItem].iImage;

    return (BOOL32)SendMessage32A (GetParent32 (wndPtr->hwndSelf), WM_NOTIFY,
				   (WPARAM32)wndPtr->wIDmenu, (LPARAM)&nmhdr);
}


static BOOL32
HEADER_SendClickNotify (WND *wndPtr, UINT32 code, INT32 iItem)
{
    NMHEADERA nmhdr;

    nmhdr.hdr.hwndFrom = wndPtr->hwndSelf;
    nmhdr.hdr.idFrom = wndPtr->wIDmenu;
    nmhdr.hdr.code = code;
    nmhdr.iItem = iItem;
    nmhdr.iButton = 0;
    nmhdr.pitem = NULL;

    return (BOOL32)SendMessage32A (GetParent32 (wndPtr->hwndSelf), WM_NOTIFY,
				   (WPARAM32)wndPtr->wIDmenu, (LPARAM)&nmhdr);
}


static LRESULT
HEADER_CreateDragImage (WND *wndPtr, WPARAM32 wParam)
{
    FIXME (header, "empty stub!\n");
    return 0;
}


static LRESULT
HEADER_DeleteItem (WND *wndPtr, WPARAM32 wParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HDC32 hdc;
    INT32 iItem;

    iItem = (INT32)wParam;

    TRACE(header, "[iItem=%d]\n", iItem);
    
    if ((iItem < 0) || (iItem > infoPtr->uNumItem - 1))
        return FALSE;

    if (infoPtr->uNumItem == 1) {
        TRACE(header, "Simple delete!\n");
        if (infoPtr->items[0].pszText)
            HeapFree (SystemHeap, 0, infoPtr->items[0].pszText);
        HeapFree (SystemHeap, 0, infoPtr->items);
        infoPtr->items = 0;
        infoPtr->uNumItem = 0;
    }
    else {
        HEADER_ITEM *oldItems = infoPtr->items;
        TRACE(header, "Complex delete! [iItem=%d]\n", iItem);

        if (infoPtr->items[iItem].pszText)
            HeapFree (SystemHeap, 0, infoPtr->items[iItem].pszText);

        infoPtr->uNumItem--;
        infoPtr->items = HeapAlloc (SystemHeap, HEAP_ZERO_MEMORY,
                                    sizeof (HEADER_ITEM) * infoPtr->uNumItem);
        /* pre delete copy */
        if (iItem > 0) {
            memcpy (&infoPtr->items[0], &oldItems[0],
                    iItem * sizeof(HEADER_ITEM));
        }

        /* post delete copy */
        if (iItem < infoPtr->uNumItem) {
            memcpy (&infoPtr->items[iItem], &oldItems[iItem+1],
                    (infoPtr->uNumItem - iItem) * sizeof(HEADER_ITEM));
        }

        HeapFree (SystemHeap, 0, oldItems);
    }

    HEADER_SetItemBounds (wndPtr);

    hdc = GetDC32 (wndPtr->hwndSelf);
    HEADER_Refresh (wndPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);
    
    return TRUE;
}


static LRESULT
HEADER_GetImageList (WND *wndPtr)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);

    return (LRESULT)infoPtr->himl;
}


static LRESULT
HEADER_GetItem32A (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HD_ITEMA *phdi;
    INT32    iItem;
    UINT32   uMask;

    phdi = (HD_ITEMA*)lParam;
    iItem = (INT32)wParam;

    if ((iItem < 0) || (iItem > infoPtr->uNumItem - 1))
        return FALSE;

    TRACE (header, "[iItem=%d]\n", iItem);

    uMask = phdi->mask;
    if (uMask == 0)
	return TRUE;
    phdi->mask = 0;

    if (uMask & infoPtr->items[iItem].mask & HDI_BITMAP) {
	phdi->hbm = infoPtr->items[iItem].hbm;
        phdi->mask |= HDI_BITMAP;
    }

    if (uMask & infoPtr->items[iItem].mask & HDI_FORMAT) {
	phdi->fmt = infoPtr->items[iItem].fmt;
        phdi->mask |= HDI_FORMAT;
    }

    if (uMask & infoPtr->items[iItem].mask & HDI_WIDTH) {
	phdi->cxy = infoPtr->items[iItem].cxy;
        phdi->mask |= HDI_WIDTH;
    }

    if (uMask & infoPtr->items[iItem].mask & HDI_LPARAM) {
	phdi->lParam = infoPtr->items[iItem].lParam;
        phdi->mask |= HDI_LPARAM;
    }

    if (uMask & infoPtr->items[iItem].mask & HDI_TEXT) {
	phdi->pszText = infoPtr->items[iItem].pszText;
	phdi->cchTextMax = infoPtr->items[iItem].cchTextMax;
        phdi->mask |= HDI_TEXT;
    }

    if (uMask & infoPtr->items[iItem].mask & HDI_IMAGE) {
	phdi->iImage = infoPtr->items[iItem].iImage;
        phdi->mask |= HDI_IMAGE;
    }

    if (uMask & infoPtr->items[iItem].mask & HDI_ORDER) {
	phdi->iOrder = infoPtr->items[iItem].iOrder;
        phdi->mask |= HDI_ORDER;
    }

    return TRUE;
}


static LRESULT
HEADER_GetItemCount (WND *wndPtr)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);

    return (infoPtr->uNumItem);
}


static LRESULT
HEADER_GetItemRect (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    INT32 iItem;
    LPRECT32 lpRect;

    iItem = (INT32)wParam;
    lpRect = (LPRECT32)lParam;

    if ((iItem < 0) || (iItem > infoPtr->uNumItem - 1))
        return FALSE;

    lpRect->left   = infoPtr->items[iItem].rect.left;
    lpRect->right  = infoPtr->items[iItem].rect.right;
    lpRect->top    = infoPtr->items[iItem].rect.top;
    lpRect->bottom = infoPtr->items[iItem].rect.bottom;

    return TRUE;
}


static LRESULT
HEADER_HitTest (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    LPHDHITTESTINFO phti = (LPHDHITTESTINFO)lParam;

    HEADER_InternalHitTest (wndPtr, &phti->pt, &phti->flags, &phti->iItem);

    return phti->flags;
}


static LRESULT
HEADER_InsertItem32A (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HD_ITEMA *phdi;
    HDC32    hdc;
    INT32    iItem, len;

    phdi = (HD_ITEMA*)lParam;
    iItem = (INT32)wParam;
    
    if (iItem < 0) return -1;
    if (iItem > infoPtr->uNumItem)
        iItem = infoPtr->uNumItem;

    if (infoPtr->uNumItem == 0) {
        infoPtr->items = HeapAlloc (SystemHeap, HEAP_ZERO_MEMORY,
                                    sizeof (HEADER_ITEM));
        infoPtr->uNumItem++;
    }
    else {
        HEADER_ITEM *oldItems = infoPtr->items;

        infoPtr->uNumItem++;
        infoPtr->items = HeapAlloc (SystemHeap, HEAP_ZERO_MEMORY,
                                    sizeof (HEADER_ITEM) * infoPtr->uNumItem);
        /* pre insert copy */
        if (iItem > 0) {
            memcpy (&infoPtr->items[0], &oldItems[0],
                    iItem * sizeof(HEADER_ITEM));
        }

        /* post insert copy */
        if (iItem < infoPtr->uNumItem - 1) {
            memcpy (&infoPtr->items[iItem+1], &oldItems[iItem],
                    (infoPtr->uNumItem - iItem) * sizeof(HEADER_ITEM));
        }

        HeapFree (SystemHeap, 0, oldItems);
    }

    infoPtr->items[iItem].bDown = FALSE;

    infoPtr->items[iItem].mask = phdi->mask;
    if (phdi->mask & HDI_WIDTH)
        infoPtr->items[iItem].cxy = phdi->cxy;

    if (phdi->mask & HDI_TEXT) {
        len = lstrlen32A (phdi->pszText);
        infoPtr->items[iItem].pszText =
            HeapAlloc (SystemHeap, HEAP_ZERO_MEMORY, len+1);
        lstrcpy32A (infoPtr->items[iItem].pszText, phdi->pszText);
        infoPtr->items[iItem].cchTextMax = phdi->cchTextMax;
    }

    if (phdi->mask & HDI_FORMAT)
        infoPtr->items[iItem].fmt = phdi->fmt;

    if (phdi->mask & HDI_BITMAP)
        infoPtr->items[iItem].hbm = phdi->hbm;

    if (phdi->mask & HDI_LPARAM)
        infoPtr->items[iItem].lParam = phdi->lParam;

    if (phdi->mask & HDI_IMAGE)
        infoPtr->items[iItem].iImage = phdi->iImage;

    if (phdi->mask & HDI_ORDER)
        infoPtr->items[iItem].iOrder = phdi->iOrder;

    HEADER_SetItemBounds (wndPtr);

    hdc = GetDC32 (wndPtr->hwndSelf);
    HEADER_Refresh (wndPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return iItem;
}


static LRESULT
HEADER_Layout (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    LPHDLAYOUT lpLayout = (LPHDLAYOUT)lParam;

    lpLayout->pwpos->hwnd = wndPtr->hwndSelf;
    lpLayout->pwpos->hwndInsertAfter = 0;
    lpLayout->pwpos->x = lpLayout->prc->left;
    lpLayout->pwpos->y = lpLayout->prc->top;
    lpLayout->pwpos->cx = lpLayout->prc->right - lpLayout->prc->left;
    if (wndPtr->dwStyle & HDS_HIDDEN)
        lpLayout->pwpos->cy = 0;
    else
        lpLayout->pwpos->cy = infoPtr->nHeight;
    lpLayout->pwpos->flags = SWP_NOACTIVATE|SWP_NOZORDER;

    TRACE (header, "Layout x=%d y=%d cx=%d cy=%d\n",
           lpLayout->pwpos->x, lpLayout->pwpos->y,
           lpLayout->pwpos->cx, lpLayout->pwpos->cy);

    HEADER_ForceItemBounds (wndPtr, lpLayout->pwpos->cy);

    /* hack */
#ifdef __HDM_LAYOUT_HACK__
    MoveWindow32 (lpLayout->pwpos->hwnd, lpLayout->pwpos->x, lpLayout->pwpos->y,
                  lpLayout->pwpos->cx, lpLayout->pwpos->cy, TRUE);
#endif

    return TRUE;
}


static LRESULT
HEADER_SetImageList (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HIMAGELIST himlOld;

    himlOld = infoPtr->himl;
    infoPtr->himl = (HIMAGELIST)lParam;

    /* FIXME: Refresh needed??? */

    return (LRESULT)himlOld;
}


static LRESULT
HEADER_SetItem32A (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    HD_ITEMA *phdi;
    INT32    iItem;
    HDC32    hdc;

    phdi = (HD_ITEMA*)lParam;
    iItem = (INT32)wParam;

    if ((iItem < 0) || (iItem > infoPtr->uNumItem - 1))
        return FALSE;

    TRACE (header, "[iItem=%d]\n", iItem);

    if (HEADER_SendHeaderNotify (wndPtr, HDN_ITEMCHANGING32A, iItem))
	return FALSE;

    if (phdi->mask & HDI_BITMAP) {
	infoPtr->items[iItem].hbm = phdi->hbm;
	infoPtr->items[iItem].mask  |= HDI_BITMAP;
    }

    if (phdi->mask & HDI_FORMAT) {
	infoPtr->items[iItem].fmt = phdi->fmt;
	infoPtr->items[iItem].mask  |= HDI_FORMAT;
    }

    if (phdi->mask & HDI_LPARAM) {
	infoPtr->items[iItem].lParam = phdi->lParam;
	infoPtr->items[iItem].mask  |= HDI_LPARAM;
    }

    if (phdi->mask & HDI_TEXT) {
        INT32 len = lstrlen32A (phdi->pszText);
        if (infoPtr->items[iItem].pszText)
	    HeapFree (SystemHeap, 0, infoPtr->items[iItem].pszText);
        infoPtr->items[iItem].pszText =
            HeapAlloc (SystemHeap, HEAP_ZERO_MEMORY, len+1);
        lstrcpy32A (infoPtr->items[iItem].pszText, phdi->pszText);
        infoPtr->items[iItem].cchTextMax = phdi->cchTextMax;
    }

    if (phdi->mask & HDI_WIDTH) {
	infoPtr->items[iItem].cxy = phdi->cxy;
	infoPtr->items[iItem].mask  |= HDI_WIDTH;
    }

    if (phdi->mask & HDI_IMAGE) {
	infoPtr->items[iItem].iImage = phdi->iImage;
	infoPtr->items[iItem].mask  |= HDI_IMAGE;
    }

    if (phdi->mask & HDI_ORDER) {
	infoPtr->items[iItem].iOrder = phdi->iOrder;
	infoPtr->items[iItem].mask  |= HDI_ORDER;
    }

    HEADER_SendHeaderNotify (wndPtr, HDN_ITEMCHANGED32A, iItem);

    HEADER_SetItemBounds (wndPtr);
    hdc = GetDC32 (wndPtr->hwndSelf);
    HEADER_Refresh (wndPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}


static LRESULT
HEADER_Create (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr;
    TEXTMETRIC32A tm;
    HFONT32 hOldFont;
    HDC32   hdc;

    infoPtr = (HEADER_INFO *)HeapAlloc( SystemHeap, HEAP_ZERO_MEMORY,
                                        sizeof(HEADER_INFO));
    wndPtr->wExtra[0] = (DWORD)infoPtr;

    infoPtr->uNumItem = 0;
    infoPtr->nHeight = 20;
    infoPtr->hFont = 0;
    infoPtr->items = 0;
    infoPtr->hcurArrow = LoadCursor32A (0, IDC_ARROW32A);
    infoPtr->hcurDivider = LoadCursor32A (0, IDC_SIZEWE32A);
    infoPtr->hcurDivopen = LoadCursor32A (0, IDC_SIZENS32A);
    infoPtr->bPressed  = FALSE;
    infoPtr->bTracking = FALSE;
    infoPtr->iMoveItem = 0;
    infoPtr->himl = 0;
    infoPtr->iHotItem = -1;

    hdc = GetDC32 (0);
    hOldFont = SelectObject32 (hdc, GetStockObject32 (SYSTEM_FONT));
    GetTextMetrics32A (hdc, &tm);
    infoPtr->nHeight = tm.tmHeight + VERT_BORDER;
    SelectObject32 (hdc, hOldFont);
    ReleaseDC32 (0, hdc);

    return 0;
}


static LRESULT
HEADER_Destroy (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    INT32 iItem;

    if (infoPtr->items) {
        for (iItem = 0; iItem < infoPtr->uNumItem; iItem++) {
            if (infoPtr->items[iItem].pszText)
                HeapFree (SystemHeap, 0, infoPtr->items[iItem].pszText);
        }
        HeapFree (SystemHeap, 0, infoPtr->items);
    }

    if (infoPtr->himl)
	ImageList_Destroy (infoPtr->himl);

    HeapFree (SystemHeap, 0, infoPtr);

    return 0;
}


static LRESULT
HEADER_GetFont (WND *wndPtr)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);

    return (LRESULT)infoPtr->hFont;
}


static LRESULT
HEADER_LButtonDblClk (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    POINT32 pt;
    UINT32  flags;
    INT32   iItem;

    pt.x = (INT32)LOWORD(lParam); 
    pt.y = (INT32)HIWORD(lParam);
    HEADER_InternalHitTest (wndPtr, &pt, &flags, &iItem);

    if ((wndPtr->dwStyle & HDS_BUTTONS) && (flags == HHT_ONHEADER))
	HEADER_SendHeaderNotify (wndPtr, HDN_ITEMDBLCLICK32A, iItem);
    else if ((flags == HHT_ONDIVIDER) || (flags == HHT_ONDIVOPEN))
	HEADER_SendHeaderNotify (wndPtr, HDN_DIVIDERDBLCLICK32A, iItem);

    return 0;
}


static LRESULT
HEADER_LButtonDown (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    POINT32 pt;
    UINT32  flags;
    INT32   iItem;
    HDC32   hdc;

    pt.x = (INT32)LOWORD(lParam); 
    pt.y = (INT32)HIWORD(lParam);
    HEADER_InternalHitTest (wndPtr, &pt, &flags, &iItem);

    if ((wndPtr->dwStyle & HDS_BUTTONS) && (flags == HHT_ONHEADER)) {
	SetCapture32 (wndPtr->hwndSelf);
	infoPtr->bCaptured = TRUE;   
	infoPtr->bPressed  = TRUE;
	infoPtr->iMoveItem = iItem;

	infoPtr->items[iItem].bDown = TRUE;

	/* Send WM_CUSTOMDRAW */
	hdc = GetDC32 (wndPtr->hwndSelf);
	HEADER_RefreshItem (wndPtr, hdc, &infoPtr->items[iItem]);
	ReleaseDC32 (wndPtr->hwndSelf, hdc);

	TRACE (header, "Pressed item %d!\n", iItem);
    } 
    else if ((flags == HHT_ONDIVIDER) || (flags == HHT_ONDIVOPEN)) {
	if (!(HEADER_SendHeaderNotify (wndPtr, HDN_BEGINTRACK32A, iItem))) {
	    SetCapture32 (wndPtr->hwndSelf);
	    infoPtr->bCaptured = TRUE;   
	    infoPtr->bTracking = TRUE;
	    infoPtr->iMoveItem = iItem;
	    infoPtr->nOldWidth = infoPtr->items[iItem].cxy;
	    infoPtr->xTrackOffset = infoPtr->items[iItem].rect.right - pt.x;

	    if (!(wndPtr->dwStyle & HDS_FULLDRAG)) {
		infoPtr->xOldTrack = infoPtr->items[iItem].rect.right;
		hdc = GetDC32 (wndPtr->hwndSelf);
		HEADER_DrawTrackLine (wndPtr, hdc, infoPtr->xOldTrack);
		ReleaseDC32 (wndPtr->hwndSelf, hdc);
	    }

	    TRACE (header, "Begin tracking item %d!\n", iItem);
	}
    }



    return 0;
}


static LRESULT
HEADER_LButtonUp (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    POINT32 pt;
    UINT32  flags;
    INT32   iItem, nWidth;
    HDC32   hdc;

    pt.x = (INT32)LOWORD(lParam);
    pt.y = (INT32)HIWORD(lParam);
    HEADER_InternalHitTest (wndPtr, &pt, &flags, &iItem);

    if (infoPtr->bPressed) {
	if ((iItem == infoPtr->iMoveItem) && (flags == HHT_ONHEADER)) {
	    infoPtr->items[infoPtr->iMoveItem].bDown = FALSE;
	    hdc = GetDC32 (wndPtr->hwndSelf);
	    HEADER_RefreshItem (wndPtr, hdc, &infoPtr->items[infoPtr->iMoveItem]);
	    ReleaseDC32 (wndPtr->hwndSelf, hdc);

	    HEADER_SendClickNotify (wndPtr, HDN_ITEMCLICK32A, infoPtr->iMoveItem);
	}
	TRACE (header, "Released item %d!\n", infoPtr->iMoveItem);
	infoPtr->bPressed = FALSE;
    }
    else if (infoPtr->bTracking) {
	TRACE (header, "End tracking item %d!\n", infoPtr->iMoveItem);
	infoPtr->bTracking = FALSE;

	HEADER_SendHeaderNotify (wndPtr, HDN_ENDTRACK32A, infoPtr->iMoveItem);

	if (!(wndPtr->dwStyle & HDS_FULLDRAG)) {
	    hdc = GetDC32 (wndPtr->hwndSelf);
	    HEADER_DrawTrackLine (wndPtr, hdc, infoPtr->xOldTrack);
	    ReleaseDC32 (wndPtr->hwndSelf, hdc);
	    if (HEADER_SendHeaderNotify (wndPtr, HDN_ITEMCHANGING32A, infoPtr->iMoveItem))
		infoPtr->items[infoPtr->iMoveItem].cxy = infoPtr->nOldWidth;
	    else {
		nWidth = pt.x - infoPtr->items[infoPtr->iMoveItem].rect.left +
		infoPtr->xTrackOffset;
		if (nWidth < 0)
		    nWidth = 0;
		infoPtr->items[infoPtr->iMoveItem].cxy = nWidth;
		HEADER_SendHeaderNotify (wndPtr, HDN_ITEMCHANGED32A, infoPtr->iMoveItem);
	    }

	    HEADER_SetItemBounds (wndPtr);
	    hdc = GetDC32 (wndPtr->hwndSelf);
	    HEADER_Refresh (wndPtr, hdc);
	    ReleaseDC32 (wndPtr->hwndSelf, hdc);
	}
    }

    if (infoPtr->bCaptured) {
	infoPtr->bCaptured = FALSE;
	ReleaseCapture ();
	HEADER_SendSimpleNotify (wndPtr, NM_RELEASEDCAPTURE);
    }

    return 0;
}


static LRESULT
HEADER_MouseMove (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    POINT32 pt;
    UINT32  flags;
    INT32   iItem, nWidth;
    HDC32   hdc;

    pt.x = (INT32)LOWORD(lParam);
    pt.y = (INT32)HIWORD(lParam);
    HEADER_InternalHitTest (wndPtr, &pt, &flags, &iItem);

    if ((wndPtr->dwStyle & HDS_BUTTONS) && (wndPtr->dwStyle & HDS_HOTTRACK)) {
	if (flags & (HHT_ONHEADER | HHT_ONDIVIDER | HHT_ONDIVOPEN))
	    infoPtr->iHotItem = iItem;
	else
	    infoPtr->iHotItem = -1;
	hdc = GetDC32 (wndPtr->hwndSelf);
	HEADER_Refresh (wndPtr, hdc);
	ReleaseDC32 (wndPtr->hwndSelf, hdc);
    }

    if (infoPtr->bCaptured) {
	if (infoPtr->bPressed) {
	    if ((iItem == infoPtr->iMoveItem) && (flags == HHT_ONHEADER))
		infoPtr->items[infoPtr->iMoveItem].bDown = TRUE;
	    else
		infoPtr->items[infoPtr->iMoveItem].bDown = FALSE;
	    hdc = GetDC32 (wndPtr->hwndSelf);
	    HEADER_RefreshItem (wndPtr, hdc, &infoPtr->items[infoPtr->iMoveItem]);
	    ReleaseDC32 (wndPtr->hwndSelf, hdc);

	    TRACE (header, "Moving pressed item %d!\n", infoPtr->iMoveItem);
	}
	else if (infoPtr->bTracking) {
	    if (wndPtr->dwStyle & HDS_FULLDRAG) {
		if (HEADER_SendHeaderNotify (wndPtr, HDN_ITEMCHANGING32A, infoPtr->iMoveItem))
		    infoPtr->items[infoPtr->iMoveItem].cxy = infoPtr->nOldWidth;
		else {
		    nWidth = pt.x - infoPtr->items[infoPtr->iMoveItem].rect.left +
		    infoPtr->xTrackOffset;
		    if (nWidth < 0)
			nWidth = 0;
		    infoPtr->items[infoPtr->iMoveItem].cxy = nWidth;
		    HEADER_SendHeaderNotify (wndPtr, HDN_ITEMCHANGED32A,
					     infoPtr->iMoveItem);
		}
		HEADER_SetItemBounds (wndPtr);
		hdc = GetDC32 (wndPtr->hwndSelf);
		HEADER_Refresh (wndPtr, hdc);
		ReleaseDC32 (wndPtr->hwndSelf, hdc);
	    }
	    else {
		hdc = GetDC32 (wndPtr->hwndSelf);
		HEADER_DrawTrackLine (wndPtr, hdc, infoPtr->xOldTrack);
		infoPtr->xOldTrack = pt.x + infoPtr->xTrackOffset;
		if (infoPtr->xOldTrack < infoPtr->items[infoPtr->iMoveItem].rect.left)
		    infoPtr->xOldTrack = infoPtr->items[infoPtr->iMoveItem].rect.left;
		infoPtr->items[infoPtr->iMoveItem].cxy = 
		    infoPtr->xOldTrack - infoPtr->items[infoPtr->iMoveItem].rect.left;
		HEADER_DrawTrackLine (wndPtr, hdc, infoPtr->xOldTrack);
		ReleaseDC32 (wndPtr->hwndSelf, hdc);
	    }

	    HEADER_SendHeaderNotify (wndPtr, HDN_TRACK32A, infoPtr->iMoveItem);
	    TRACE (header, "Tracking item %d!\n", infoPtr->iMoveItem);
	}
    }

    if ((wndPtr->dwStyle & HDS_BUTTONS) && (wndPtr->dwStyle & HDS_HOTTRACK)) {

    }

    return 0;
}


static LRESULT
HEADER_Paint (WND *wndPtr, WPARAM32 wParam)
{
    HDC32 hdc;
    PAINTSTRUCT32 ps;

    hdc = wParam==0 ? BeginPaint32 (wndPtr->hwndSelf, &ps) : (HDC32)wParam;
    HEADER_Refresh (wndPtr, hdc);
    if(!wParam)
	EndPaint32 (wndPtr->hwndSelf, &ps);
    return 0;
}


static LRESULT
HEADER_RButtonUp (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    return HEADER_SendSimpleNotify (wndPtr, NM_RCLICK);
}


static LRESULT
HEADER_SetCursor (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    POINT32 pt;
    UINT32  flags;
    INT32   iItem;

    TRACE (header, "code=0x%X  id=0x%X\n", LOWORD(lParam), HIWORD(lParam));

    GetCursorPos32 (&pt);
    ScreenToClient32 (wndPtr->hwndSelf, &pt);

    HEADER_InternalHitTest (wndPtr, &pt, &flags, &iItem);

    if (flags == HHT_ONDIVIDER)
        SetCursor32 (infoPtr->hcurDivider);
    else if (flags == HHT_ONDIVOPEN)
        SetCursor32 (infoPtr->hcurDivopen);
    else
        SetCursor32 (infoPtr->hcurArrow);

    return 0;
}


static LRESULT
HEADER_SetFont (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HEADER_INFO *infoPtr = HEADER_GetInfoPtr(wndPtr);
    TEXTMETRIC32A tm;
    HFONT32 hFont, hOldFont;
    HDC32 hdc;

    infoPtr->hFont = (HFONT32)wParam;

    hFont = infoPtr->hFont ? infoPtr->hFont : GetStockObject32 (SYSTEM_FONT);

    hdc = GetDC32 (0);
    hOldFont = SelectObject32 (hdc, hFont);
    GetTextMetrics32A (hdc, &tm);
    infoPtr->nHeight = tm.tmHeight + VERT_BORDER;
    SelectObject32 (hdc, hOldFont);
    ReleaseDC32 (0, hdc);

    if (lParam) {
        HEADER_ForceItemBounds (wndPtr, infoPtr->nHeight);
        hdc = GetDC32 (wndPtr->hwndSelf);
        HEADER_Refresh (wndPtr, hdc);
        ReleaseDC32 (wndPtr->hwndSelf, hdc);
    }

    return 0;
}


LRESULT WINAPI
HeaderWindowProc (HWND32 hwnd, UINT32 msg, WPARAM32 wParam, LPARAM lParam)
{
    WND *wndPtr = WIN_FindWndPtr(hwnd);

    switch (msg) {
	case HDM_CREATEDRAGIMAGE:
	    return HEADER_CreateDragImage (wndPtr, wParam);

	case HDM_DELETEITEM:
	    return HEADER_DeleteItem (wndPtr, wParam);

	case HDM_GETIMAGELIST:
	    return HEADER_GetImageList (wndPtr);

	case HDM_GETITEM32A:
	    return HEADER_GetItem32A (wndPtr, wParam, lParam);

	case HDM_GETITEMCOUNT:
	    return HEADER_GetItemCount (wndPtr);

	case HDM_GETITEMRECT:
	    return HEADER_GetItemRect (wndPtr, wParam, lParam);

	case HDM_HITTEST:
	    return HEADER_HitTest (wndPtr, wParam, lParam);

	case HDM_INSERTITEM32A:
	    return HEADER_InsertItem32A (wndPtr, wParam, lParam);

	case HDM_LAYOUT:
	    return HEADER_Layout (wndPtr, wParam, lParam);

	case HDM_SETIMAGELIST:
	    return HEADER_SetImageList (wndPtr, wParam, lParam);

	case HDM_SETITEM32A:
	    return HEADER_SetItem32A (wndPtr, wParam, lParam);


        case WM_CREATE:
            return HEADER_Create (wndPtr, wParam, lParam);

        case WM_DESTROY:
            return HEADER_Destroy (wndPtr, wParam, lParam);

        case WM_ERASEBKGND:
            return 1;

        case WM_GETDLGCODE:
            return DLGC_WANTTAB | DLGC_WANTARROWS;

        case WM_GETFONT:
            return HEADER_GetFont (wndPtr);

        case WM_LBUTTONDBLCLK:
            return HEADER_LButtonDblClk (wndPtr, wParam, lParam);

        case WM_LBUTTONDOWN:
            return HEADER_LButtonDown (wndPtr, wParam, lParam);

        case WM_LBUTTONUP:
            return HEADER_LButtonUp (wndPtr, wParam, lParam);

        case WM_MOUSEMOVE:
            return HEADER_MouseMove (wndPtr, wParam, lParam);

        case WM_PAINT:
            return HEADER_Paint (wndPtr, wParam);

        case WM_RBUTTONUP:
            return HEADER_RButtonUp (wndPtr, wParam, lParam);

        case WM_SETCURSOR:
            return HEADER_SetCursor (wndPtr, wParam, lParam);

        case WM_SETFONT:
            return HEADER_SetFont (wndPtr, wParam, lParam);

        default:
            if (msg >= WM_USER) 
		ERR (header, "unknown msg %04x wp=%04x lp=%08lx\n",
		     msg, wParam, lParam );
	    return DefWindowProc32A (hwnd, msg, wParam, lParam);
    }
    return 0;
}


void HEADER_Register( void )
{
    WNDCLASS32A wndClass;

    if (GlobalFindAtom32A (WC_HEADER32A)) return;

    ZeroMemory (&wndClass, sizeof(WNDCLASS32A));
    wndClass.style         = CS_GLOBALCLASS | CS_DBLCLKS;
    wndClass.lpfnWndProc   = (WNDPROC32)HeaderWindowProc;
    wndClass.cbClsExtra    = 0;
    wndClass.cbWndExtra    = sizeof(HEADER_INFO *);
    wndClass.hCursor       = LoadCursor32A (0, IDC_ARROW32A);
    wndClass.lpszClassName = WC_HEADER32A;
 
    RegisterClass32A (&wndClass);
}

