/*
 * Toolbar control
 *
 * Copyright 1998 Eric Kohl
 *
 * NOTES
 *   PLEASE don't try to improve or change this code right now. Many
 *   features are still missing, but I'm working on it. I want to avoid
 *   any confusion. This note will be removed as soon as most of the
 *   features are implemented.
 *     Eric <ekohl@abo.rhein-zeitung.de>
 *
 * TODO:
 *   - Many messages.
 *   - All notifications.
 *   - Tooltip support.
 *   - Unicode suppport.
 *   - Internal COMMCTL32 bitmaps.
 *   - Customize dialog.
 *
 * Testing:
 *   - Run tests using Waite Group Windows95 API Bible Volume 2.
 *     The second cdrom contains executables addstr.exe, btncount.exe,
 *     btnstate.exe, butstrsz.exe, chkbtn.exe, chngbmp.exe, customiz.exe,
 *     enablebtn.exe, getbmp.exe, getbtn.exe, getflags.exe, hidebtn.exe,
 *     indetbtn.exe, insbtn.exe, pressbtn.exe, setbtnsz.exe, setcmdid.exe,
 *     setparnt.exe, setrows.exe, toolwnd.exe.
 *   - additional features.
 */

#include "windows.h"
#include "commctrl.h"
#include "toolbar.h"
#include "heap.h"
#include "win.h"
#include "debug.h"


#define SEPARATOR_WIDTH  12





#define TOOLBAR_GetInfoPtr(wndPtr) ((TOOLBAR_INFO *)wndPtr->wExtra[0])


static void
TOOLBAR_DrawButton (WND *wndPtr, TBUTTON_INFO *btnPtr, HDC32 hdc)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    RECT32 rc;

    if (btnPtr->fsState & TBSTATE_HIDDEN) return;

    rc = btnPtr->rect;
    if (btnPtr->fsStyle & TBSTYLE_SEP) {

    }
    else {
	if (!(btnPtr->fsState & TBSTATE_ENABLED)) {
	    /* button is disabled */
	    DrawEdge32 (hdc, &rc, EDGE_RAISED,
			BF_SOFT | BF_RECT | BF_MIDDLE | BF_ADJUST);
	    ImageList_Draw (infoPtr->himlDis, btnPtr->iBitmap, hdc,
			    rc.left+2, rc.top+2, ILD_NORMAL);
	    return;
	}

	/* TBSTYLE_BUTTON */
	if (btnPtr->fsState & TBSTATE_PRESSED) {
	    DrawEdge32 (hdc, &rc, EDGE_SUNKEN,
			BF_RECT | BF_MIDDLE | BF_ADJUST);
	    ImageList_Draw (infoPtr->himlDef, btnPtr->iBitmap, hdc,
			    rc.left+3, rc.top+3, ILD_NORMAL);
	    return;
	}
	else {
	    DrawEdge32 (hdc, &rc, EDGE_RAISED,
			BF_SOFT | BF_RECT | BF_MIDDLE | BF_ADJUST);
	    ImageList_Draw (infoPtr->himlDef, btnPtr->iBitmap, hdc,
			    rc.left+2, rc.top+2, ILD_NORMAL);
	}
    }
}



static void
TOOLBAR_Refresh (WND *wndPtr, HDC32 hdc)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    INT32 i;


    /* draw buttons */

    btnPtr = infoPtr->buttons;
    for (i = 0; i < infoPtr->nNumButtons; i++) {
	TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
	btnPtr++;
    }
}


static void
TOOLBAR_CalcToolbar (WND *wndPtr)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    RECT32 rect;
    INT32 i;

    rect.left   = infoPtr->nIndent;
    rect.top    = infoPtr->nButtonTop;
//    rect.right  = rect.left + infoPtr->nButtonWidth;
    rect.bottom = rect.top + infoPtr->nButtonHeight;

    btnPtr = infoPtr->buttons;
    for (i = 0; i < infoPtr->nNumButtons; i++, btnPtr++) {
	if (btnPtr->fsState & TBSTATE_HIDDEN) {
	    btnPtr->rect.left = 0;
	    btnPtr->rect.right = 0;
	    btnPtr->rect.top = 0;
	    btnPtr->rect.bottom = 0;
	    continue;
	}

	btnPtr->rect = rect;
	if (btnPtr->fsStyle & TBSTYLE_SEP)
	    btnPtr->rect.right = btnPtr->rect.left + SEPARATOR_WIDTH;
	else
	    btnPtr->rect.right = btnPtr->rect.left + infoPtr->nButtonWidth;
	rect.left = btnPtr->rect.right;
    }
}


static INT32
TOOLBAR_InternalHitTest (WND *wndPtr, LPPOINT32 lpPt)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    INT32 i;
    

    btnPtr = infoPtr->buttons;
    for (i = 0; i < infoPtr->nNumButtons; i++) {
	if (btnPtr->fsStyle & TBSTYLE_SEP) {
	    if (PtInRect32 (&btnPtr->rect, *lpPt)) {
//		TRACE (toolbar, " ON SEPARATOR %d!\n", i);
		return -i;
	    }
	}
	else {
	    if (PtInRect32 (&btnPtr->rect, *lpPt)) {
//		TRACE (toolbar, " ON BUTTON %d!\n", i);
		return i;
	    }

	}

	btnPtr++;
    }

//    TRACE (toolbar, " NOWHERE!\n");
    return -1;
}


static INT32
TOOLBAR_GetButtonIndex (TOOLBAR_INFO *infoPtr, INT32 idCommand)
{
    TBUTTON_INFO *btnPtr;
    INT32 i;

    btnPtr = infoPtr->buttons;
    for (i = 0; i < infoPtr->nNumButtons; i++) {
	if (btnPtr->idCommand == idCommand)
	    return i;
	btnPtr++;
    }
    return -1;
}


static LRESULT
TOOLBAR_AddBitmap (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    LPTBADDBITMAP lpAddBmp = (LPTBADDBITMAP)lParam;

    if ((!lpAddBmp) || ((INT32)wParam <= 0))
	return -1;

    TRACE (toolbar, "adding %d bitmaps!\n", wParam);

    if (!(infoPtr->himlDef)) {
	/* create new default image list */
	TRACE (toolbar, "creating default image list!\n");
	infoPtr->himlDef =
	    ImageList_Create (infoPtr->nBitmapWidth, 
			      infoPtr->nBitmapHeight, ILC_COLOR | ILC_MASK,
			      (INT32)wParam, 2);
    }

    if (!(infoPtr->himlDis)) {
	/* create new disabled image list */
	TRACE (toolbar, "creating disabled image list!\n");
	infoPtr->himlDis =
	    ImageList_Create (infoPtr->nBitmapWidth, 
			      infoPtr->nBitmapHeight, ILC_COLOR | ILC_MASK,
			      (INT32)wParam, 2);
    }


    /* Add bitmaps to the default image list */
    if (lpAddBmp->hInst == (HINSTANCE32)0) {

	ImageList_Add (infoPtr->himlDef, (HBITMAP32)lpAddBmp->nID, 0);
    }
    else if (lpAddBmp->hInst == HINST_COMMCTRL) {
	/* add internal bitmaps */
	FIXME (toolbar, "internal bitmaps not supported!\n");

	/* Hack to "add" some reserved images within the image list 
	   to get the right image indices */
	ImageList_SetImageCount (infoPtr->himlDef,
	    ImageList_GetImageCount (infoPtr->himlDef) + (INT32)wParam);
    }
    else {
	HBITMAP32 hBmp =
	    LoadBitmap32A (lpAddBmp->hInst, (LPSTR)lpAddBmp->nID);

	ImageList_Add (infoPtr->himlDef, hBmp, (HBITMAP32)0);

	DeleteObject32 (hBmp); 
    }


    /* Add bitmaps to the disabled image list */


    return 0;
}


static LRESULT
TOOLBAR_AddButtons32A (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    LPTBBUTTON lpTbb = (LPTBBUTTON)lParam;
    INT32 nOldButtons, nNewButtons, nAddButtons, nCount;
    HDC32 hdc;

    TRACE (toolbar, "adding %d buttons!\n", wParam);

    nAddButtons = (UINT32)wParam;
    nOldButtons = infoPtr->nNumButtons;
    nNewButtons = nOldButtons + nAddButtons;

    if (infoPtr->nNumButtons == 0) {
	infoPtr->buttons =
	    HeapAlloc (SystemHeap, HEAP_ZERO_MEMORY,
		       sizeof (TBUTTON_INFO) * nNewButtons);
    }
    else {
	TBUTTON_INFO *oldButtons = infoPtr->buttons;
	infoPtr->buttons =
	    HeapAlloc (SystemHeap, HEAP_ZERO_MEMORY,
		       sizeof (TBUTTON_INFO) * nNewButtons);
	memcpy (&infoPtr->buttons[0], &oldButtons[0],
		nOldButtons * sizeof(TBUTTON_INFO));
        HeapFree (SystemHeap, 0, oldButtons);
    }

    infoPtr->nNumButtons = nNewButtons;

    /* insert new button data (bad implementation)*/
    for (nCount = 0; nCount < nAddButtons; nCount++) {
	infoPtr->buttons[nOldButtons+nCount].iBitmap   = lpTbb[nCount].iBitmap;
	infoPtr->buttons[nOldButtons+nCount].idCommand = lpTbb[nCount].idCommand;
	infoPtr->buttons[nOldButtons+nCount].fsState   = lpTbb[nCount].fsState;
	infoPtr->buttons[nOldButtons+nCount].fsStyle   = lpTbb[nCount].fsStyle;
	infoPtr->buttons[nOldButtons+nCount].dwData    = lpTbb[nCount].dwData;
	infoPtr->buttons[nOldButtons+nCount].iString   = lpTbb[nCount].iString;
    }

    TOOLBAR_CalcToolbar (wndPtr);

    hdc = GetDC32 (wndPtr->hwndSelf);
    TOOLBAR_Refresh (wndPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}


// << TOOLBAR_AddString32A >>
// << TOOLBAR_AutoSize >>


static LRESULT
TOOLBAR_ButtonCount (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);

    return infoPtr->nNumButtons;
}


static LRESULT
TOOLBAR_ButtonStructSize (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);

    infoPtr->dwStructSize = (DWORD)wParam;

    return 0;
}


static LRESULT
TOOLBAR_ChangeBitmap (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    HDC32 hdc;
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    btnPtr = &infoPtr->buttons[nIndex];
    btnPtr->iBitmap = LOWORD(lParam);

    hdc = GetDC32 (wndPtr->hwndSelf);
    TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}


/*
static LRESULT
TOOLBAR_CheckButton (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    HDC32 hdc;
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    btnPtr = &infoPtr->buttons[nIndex];
    if (LOWORD(lParam) == FALSE)
	btnPtr->fsState &= ~TBSTATE_CHECKED;
    else
	btnPtr->fsState |= TBSTATE_CHECKED;

    hdc = GetDC32 (wndPtr->hwndSelf);
    TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}
*/


static LRESULT
TOOLBAR_CommandToIndex (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);

    return TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
}


// << TOOLBAR_Customize >>


static LRESULT
TOOLBAR_DeleteButton (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 iIndex = (INT32)wParam;

    if ((iIndex < 0) || (iIndex >= infoPtr->nNumButtons))
	return FALSE;

    if (infoPtr->nNumButtons == 1) {
	TRACE (toolbar, " simple delete!\n");
	HeapFree (SystemHeap, 0, infoPtr->buttons);
	infoPtr->buttons = NULL;
	infoPtr->nNumButtons = 0;
    }
    else {

        TRACE(header, "complex delete! [iIndex=%d]\n", iIndex);

    }

    TOOLBAR_CalcToolbar (wndPtr);

    InvalidateRect32 (wndPtr->hwndSelf, NULL, TRUE);
    UpdateWindow32 (wndPtr->hwndSelf);

    return TRUE;
}


static LRESULT
TOOLBAR_EnableButton (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    HDC32 hdc;
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    btnPtr = &infoPtr->buttons[nIndex];
    if (LOWORD(lParam) == FALSE)
	btnPtr->fsState &= ~TBSTATE_ENABLED;
    else
	btnPtr->fsState |= TBSTATE_ENABLED;

    hdc = GetDC32 (wndPtr->hwndSelf);
    TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}


// << TOOLBAR_GetAnchorHighlight >>


static LRESULT
TOOLBAR_GetBitmap (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return 0;

    return infoPtr->buttons[nIndex].iBitmap;
}


// << TOOLBAR_GetBitmapFlags >>
// << TOOLBAR_GetButton >>
// << ... >>


static LRESULT
TOOLBAR_HideButton (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    btnPtr = &infoPtr->buttons[nIndex];
    if (LOWORD(lParam) == FALSE)
	btnPtr->fsState &= ~TBSTATE_HIDDEN;
    else
	btnPtr->fsState |= TBSTATE_HIDDEN;

    TOOLBAR_CalcToolbar (wndPtr);

    InvalidateRect32 (wndPtr->hwndSelf, NULL, TRUE);
    UpdateWindow32 (wndPtr->hwndSelf);

    return TRUE;
}


static LRESULT
TOOLBAR_HitTest (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    return TOOLBAR_InternalHitTest (wndPtr, (LPPOINT32)lParam);
}


static LRESULT
TOOLBAR_IsButtonChecked (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    return (infoPtr->buttons[nIndex].fsState & TBSTATE_CHECKED);
}


static LRESULT
TOOLBAR_IsButtonEnabled (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    return (infoPtr->buttons[nIndex].fsState & TBSTATE_ENABLED);
}


static LRESULT
TOOLBAR_IsButtonHidden (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    return (infoPtr->buttons[nIndex].fsState & TBSTATE_HIDDEN);
}


static LRESULT
TOOLBAR_IsButtonHighlighted (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    return (infoPtr->buttons[nIndex].fsState & TBSTATE_MARKED);
}


static LRESULT
TOOLBAR_IsButtonIndeterminate (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    return (infoPtr->buttons[nIndex].fsState & TBSTATE_INDETERMINATE);
}


static LRESULT
TOOLBAR_IsButtonPressed (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    return (infoPtr->buttons[nIndex].fsState & TBSTATE_PRESSED);
}


// << TOOLBAR_LoadImages >>
// << TOOLBAR_MapAccelerator >>
// << TOOLBAR_MarkButton >>
// << TOOLBAR_MoveButton >>


static LRESULT
TOOLBAR_PressButton (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    HDC32 hdc;
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    btnPtr = &infoPtr->buttons[nIndex];
    if (LOWORD(lParam) == FALSE)
	btnPtr->fsState &= ~TBSTATE_PRESSED;
    else
	btnPtr->fsState |= TBSTATE_PRESSED;

    hdc = GetDC32 (wndPtr->hwndSelf);
    TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}


// << TOOLBAR_ReplaceBitmap >>
// << TOOLBAR_SaveRestore >>
// << TOOLBAR_SetAnchorHighlight >>

static LRESULT
TOOLBAR_SetBitmapSize (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);

    if ((LOWORD(lParam) <= 0) || (HIWORD(lParam)<=0))
	return FALSE;

    infoPtr->nBitmapWidth = (INT32)LOWORD(lParam);
    infoPtr->nBitmapHeight = (INT32)HIWORD(lParam);

    return TRUE;
}


// << TOOLBAR_SetButtonInfo >>


static LRESULT
TOOLBAR_SetButtonSize (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);

    if ((LOWORD(lParam) <= 0) || (HIWORD(lParam)<=0))
	return FALSE;

    infoPtr->nButtonWidth = (INT32)LOWORD(lParam);
    infoPtr->nButtonHeight = (INT32)HIWORD(lParam);

    return TRUE;
}


// << TOOLBAR_SetButtonWidth >>


static LRESULT
TOOLBAR_SetCmdId (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    INT32 nIndex = (INT32)wParam;

    if ((nIndex < 0) || (nIndex >= infoPtr->nNumButtons))
	return FALSE;

    infoPtr->buttons[nIndex].idCommand = (INT32)lParam;

    return TRUE;
}



static LRESULT
TOOLBAR_SetIndent (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    HDC32 hdc;

    infoPtr->nIndent = (INT32)wParam;
    TOOLBAR_CalcToolbar (wndPtr);
    hdc = GetDC32 (wndPtr->hwndSelf);
    TOOLBAR_Refresh (wndPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}



static LRESULT
TOOLBAR_SetState (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    HDC32 hdc;
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    btnPtr = &infoPtr->buttons[nIndex];
    btnPtr->fsState = LOWORD(lParam);

    hdc = GetDC32 (wndPtr->hwndSelf);
    TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}


static LRESULT
TOOLBAR_SetStyle (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    HDC32 hdc;
    INT32 nIndex;

    nIndex = TOOLBAR_GetButtonIndex (infoPtr, (INT32)wParam);
    if (nIndex == -1)
	return FALSE;

    btnPtr = &infoPtr->buttons[nIndex];
    btnPtr->fsStyle = LOWORD(lParam);

    hdc = GetDC32 (wndPtr->hwndSelf);
    TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
    ReleaseDC32 (wndPtr->hwndSelf, hdc);

    return TRUE;
}


// << TOOLBAR_SetToolTips >>
// << TOOLBAR_SetUnicodeFormat >>


static LRESULT
TOOLBAR_Create (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr;

    /* allocate memory for info structure */
    infoPtr = (TOOLBAR_INFO *)HeapAlloc (SystemHeap, HEAP_ZERO_MEMORY,
                                   sizeof(TOOLBAR_INFO));
    wndPtr->wExtra[0] = (DWORD)infoPtr;

    infoPtr->nButtonHeight = 22;
    infoPtr->nButtonWidth = 24;
    infoPtr->nButtonTop = 2;
    infoPtr->nBitmapHeight = 15;
    infoPtr->nBitmapWidth = 16;

    infoPtr->nHeight = infoPtr->nButtonHeight + 6;

    infoPtr->bCaptured = 0;
    infoPtr->nButtonDown = -1;
    infoPtr->nOldHit = -1;

    return 0;
}


static LRESULT
TOOLBAR_Destroy (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);

    /* delete button data */
    if (infoPtr->buttons)
	HeapFree (SystemHeap, 0, infoPtr->buttons);

    /* destroy default image list */
    if (infoPtr->himlDef)
	ImageList_Destroy (infoPtr->himlDef);

    /* destroy disabled image list */
    if (infoPtr->himlDis)
	ImageList_Destroy (infoPtr->himlDis);

    /* destroy hot image list */
    if (infoPtr->himlHot)
	ImageList_Destroy (infoPtr->himlHot);

    /* free toolbar info data */
    HeapFree (SystemHeap, 0, infoPtr);

    return 0;
}


static LRESULT
TOOLBAR_LButtonDblClk (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    POINT32 pt;
    INT32   nHit;
    HDC32   hdc;

    pt.x = (INT32)LOWORD(lParam);
    pt.y = (INT32)HIWORD(lParam);
    nHit = TOOLBAR_InternalHitTest (wndPtr, &pt);

    if (nHit >= 0) {
	btnPtr = &infoPtr->buttons[nHit];
	if (!(btnPtr->fsState & TBSTATE_ENABLED))
	    return 0;
	SetCapture32 (wndPtr->hwndSelf);
	infoPtr->bCaptured = TRUE;
	infoPtr->nButtonDown = nHit;

	btnPtr->fsState |= TBSTATE_PRESSED;

	hdc = GetDC32 (wndPtr->hwndSelf);
	TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
	ReleaseDC32 (wndPtr->hwndSelf, hdc);
    }
    else if (wndPtr->dwStyle & CCS_ADJUSTABLE) {
	/* customize */

	FIXME (toolbar, "customization not implemented!\n");
    }

    return 0;
}


static LRESULT
TOOLBAR_LButtonDown (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    POINT32 pt;
    INT32   nHit;
    HDC32   hdc;

    pt.x = (INT32)LOWORD(lParam);
    pt.y = (INT32)HIWORD(lParam);
    nHit = TOOLBAR_InternalHitTest (wndPtr, &pt);

    if (nHit >= 0) {
	btnPtr = &infoPtr->buttons[nHit];
	if (!(btnPtr->fsState & TBSTATE_ENABLED))
	    return 0;

	SetCapture32 (wndPtr->hwndSelf);
	infoPtr->bCaptured = TRUE;
	infoPtr->nButtonDown = nHit;
	infoPtr->nOldHit = nHit;

	btnPtr->fsState |= TBSTATE_PRESSED;

	hdc = GetDC32 (wndPtr->hwndSelf);
	TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
	ReleaseDC32 (wndPtr->hwndSelf, hdc);
    }
    

    return 0;
}


static LRESULT
TOOLBAR_LButtonUp (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    POINT32 pt;
    INT32   nHit;
    HDC32   hdc;

    pt.x = (INT32)LOWORD(lParam);
    pt.y = (INT32)HIWORD(lParam);
    nHit = TOOLBAR_InternalHitTest (wndPtr, &pt);

    if ((infoPtr->bCaptured) && (infoPtr->nButtonDown >= 0)) {

	btnPtr = &infoPtr->buttons[infoPtr->nButtonDown];




	btnPtr->fsState &= ~TBSTATE_PRESSED;
	infoPtr->nButtonDown = -1;
	infoPtr->nOldHit = -1;

	infoPtr->bCaptured = FALSE;
	ReleaseCapture ();

	hdc = GetDC32 (wndPtr->hwndSelf);
	TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
	ReleaseDC32 (wndPtr->hwndSelf, hdc);

	SendMessage32A (GetParent32 (wndPtr->hwndSelf), WM_COMMAND,
			MAKEWPARAM(btnPtr->idCommand, 0),
			(LPARAM)wndPtr->hwndSelf);
    }

    return 0;
}


static LRESULT
TOOLBAR_MouseMove (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    TBUTTON_INFO *btnPtr;
    POINT32 pt;
    INT32   nHit;
    HDC32   hdc;

    pt.x = (INT32)LOWORD(lParam);
    pt.y = (INT32)HIWORD(lParam);
    nHit = TOOLBAR_InternalHitTest (wndPtr, &pt);

    if (infoPtr->bCaptured) {
	if (infoPtr->nOldHit != nHit) {
	    btnPtr = &infoPtr->buttons[infoPtr->nButtonDown];
	    if (infoPtr->nOldHit == infoPtr->nButtonDown) {
		btnPtr->fsState &= ~TBSTATE_PRESSED;
		hdc = GetDC32 (wndPtr->hwndSelf);
		TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
		ReleaseDC32 (wndPtr->hwndSelf, hdc);
	    }
	    else if (nHit == infoPtr->nButtonDown) {
		btnPtr->fsState |= TBSTATE_PRESSED;
		hdc = GetDC32 (wndPtr->hwndSelf);
		TOOLBAR_DrawButton (wndPtr, btnPtr, hdc);
		ReleaseDC32 (wndPtr->hwndSelf, hdc);
	    }
	}
	infoPtr->nOldHit = nHit;
    }

    return 0;
}





static LRESULT
TOOLBAR_NCCalcSize (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    RECT32 tmpRect = {0, 0, 0, 0};
    LPRECT32 winRect;

//    DefWindowProc32A (wndPtr->hwndSelf, WM_NCCALCSIZE, wParam, lParam);

    winRect = (LPRECT32)lParam;

//    if (wndPtr->dwStyle & WS_BORDER)
//	InflateRect32 (&tmpRect, 1, 1);

    if (!(wndPtr->dwStyle & CCS_NODIVIDER)) {
	tmpRect.top -= 2;
	tmpRect.bottom -= 2;
    }

    winRect->left   -= tmpRect.left;   
    winRect->top    -= tmpRect.top;   
    winRect->right  -= tmpRect.right;   
    winRect->bottom -= tmpRect.bottom;   

    return DefWindowProc32A (wndPtr->hwndSelf, WM_NCCALCSIZE, wParam, lParam);
//    return 0;
}


static LRESULT
TOOLBAR_NCPaint (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    HDC32 hdc;
    RECT32 rect;
    HWND32 hwnd = wndPtr->hwndSelf;

    if ( wndPtr->dwStyle & WS_MINIMIZE ||
	!WIN_IsWindowDrawable( wndPtr, 0 )) return 0; /* Nothing to do */

    DefWindowProc32A (hwnd, WM_NCPAINT, wParam, lParam);

    if (!(hdc = GetDCEx32( hwnd, 0, DCX_USESTYLE | DCX_WINDOW ))) return 0;

    if (ExcludeVisRect( hdc, wndPtr->rectClient.left-wndPtr->rectWindow.left,
		        wndPtr->rectClient.top-wndPtr->rectWindow.top,
		        wndPtr->rectClient.right-wndPtr->rectWindow.left,
		        wndPtr->rectClient.bottom-wndPtr->rectWindow.top )
	== NULLREGION)
    {
	ReleaseDC32( hwnd, hdc );
	return 0;
    }

    if (!(wndPtr->flags & WIN_MANAGED)) {
	if (!(wndPtr->dwStyle & CCS_NODIVIDER)) {
	    rect.left = wndPtr->rectClient.left;
	    rect.top = wndPtr->rectClient.top - 2;
	    rect.right = wndPtr->rectClient.right;

	    SelectObject32 ( hdc, GetSysColorPen32 (COLOR_3DSHADOW));
	    MoveToEx32 (hdc, rect.left, rect.top, NULL);
	    LineTo32 (hdc, rect.right, rect.top);
	    rect.top++;
	    SelectObject32 ( hdc, GetSysColorPen32 (COLOR_3DHILIGHT));
	    MoveToEx32 (hdc, rect.left, rect.top, NULL);
	    LineTo32 (hdc, rect.right, rect.top);
	}

    }

    ReleaseDC32( hwnd, hdc );

    return 0;
}


static LRESULT
TOOLBAR_Paint (WND *wndPtr, WPARAM32 wParam)
{
    HDC32 hdc;
    PAINTSTRUCT32 ps;

    hdc = wParam==0 ? BeginPaint32 (wndPtr->hwndSelf, &ps) : (HDC32)wParam;
    TOOLBAR_Refresh (wndPtr, hdc);
    if (!wParam)
	EndPaint32 (wndPtr->hwndSelf, &ps);
    return 0;
}


static LRESULT
TOOLBAR_Size (WND *wndPtr, WPARAM32 wParam, LPARAM lParam)
{
    TOOLBAR_INFO *infoPtr = TOOLBAR_GetInfoPtr(wndPtr);
    RECT32 parent_rect;
    HWND32 parent;
    INT32  flags;

    flags = (INT32) wParam;

    /* FIXME for flags =
     * SIZE_MAXIMIZED, SIZE_MAXSHOW, SIZE_MINIMIZED, SIZE_RESTORED
     */

//    if (flags == SIZE_RESTORED) {
	/* width and height don't apply */
	parent = GetParent32 (wndPtr->hwndSelf);
	GetClientRect32(parent, &parent_rect);
        infoPtr->nWidth = parent_rect.right - parent_rect.left;
	MoveWindow32(wndPtr->hwndSelf, parent_rect.left, parent_rect.top, //0, 0,
		     infoPtr->nWidth, infoPtr->nHeight, TRUE);
//    }
    return 0;
}







LRESULT WINAPI
ToolbarWindowProc (HWND32 hwnd, UINT32 uMsg, WPARAM32 wParam, LPARAM lParam)
{
    WND *wndPtr = WIN_FindWndPtr(hwnd);

    switch (uMsg)
    {
	case TB_ADDBITMAP:
	    return TOOLBAR_AddBitmap (wndPtr, wParam, lParam);

	case TB_ADDBUTTONS32A:
	    return TOOLBAR_AddButtons32A (wndPtr, wParam, lParam);

//	case TB_ADDSTRING32A:
//	    return TOOLBAR_AddString32A (wndPtr, wParam, lParam);

//	case TB_AUTOSIZE:
//	    return TOOLBAR_AutoSize (wndPtr, wParam, lParam);

	case TB_BUTTONCOUNT:
	    return TOOLBAR_ButtonCount (wndPtr, wParam, lParam);

	case TB_BUTTONSTRUCTSIZE:
	    return TOOLBAR_ButtonStructSize (wndPtr, wParam, lParam);

	case TB_CHANGEBITMAP:
	    return TOOLBAR_ChangeBitmap (wndPtr, wParam, lParam);

//	case TB_CHECKBUTTON:
//	    return TOOLBAR_CheckButton (wndPtr, wParam, lParam);

	case TB_COMMANDTOINDEX:
	    return TOOLBAR_CommandToIndex (wndPtr, wParam, lParam);

//	case TB_CUSTOMIZE:

	case TB_DELETEBUTTON:
	    return TOOLBAR_DeleteButton (wndPtr, wParam, lParam);

	case TB_ENABLEBUTTON:
	    return TOOLBAR_EnableButton (wndPtr, wParam, lParam);

//	case TB_GETANCHORHIGHLIGHT:		/* 4.71 */

	case TB_GETBITMAP:
	    return TOOLBAR_GetBitmap (wndPtr, wParam, lParam);

//	case TB_GETBITMAPFLAGS:
//	case TB_GETDISABLEDIMAGELIST:		/* 4.70 */
//	case TB_GETEXTENDEDSTYLE:		/* 4.71 */
//	case TB_GETHOTIMAGELIST:		/* 4.70 */
//	case TB_GETHOTITEM:			/* 4.71 */
//	case TB_GETIMAGELIST:			/* 4.70 */
//	case TB_GETINSERTMARK:			/* 4.71 */
//	case TB_GETINSERTMARKCOLOR:		/* 4.71 */
//	case TB_GETITEMRECT:
//	case TB_GETMAXSIZE:			/* 4.71 */
//	case TB_GETOBJECT:			/* 4.71 */
//	case TB_GETPADDING:			/* 4.71 */
//	case TB_GETRECT:			/* 4.70 */
//	case TB_GETROWS:
//	case TB_GETSTATE:
//	case TB_GETSTYLE:			/* 4.70 */
//	case TB_GETTEXTROWS:			/* 4.70 */
//	case TB_GETTOOLTIPS:
//	case TB_GETUNICODEFORMAT:

	case TB_HIDEBUTTON:
	    return TOOLBAR_HideButton (wndPtr, wParam, lParam);

	case TB_HITTEST:
	    return TOOLBAR_HitTest (wndPtr, wParam, lParam);

//	case TB_INDETERMINATE:
//	    return TOOLBAR_Indeterminate (wndPtr, wParam, lParam);

//	case TB_INSERTBUTTON:
//	    return TOOLBAR_InsertButton (wndPtr, wParam, lParam);

//	case TB_INSERTMARKHITTEST:		/* 4.71 */

	case TB_ISBUTTONCHECKED:
	    return TOOLBAR_IsButtonChecked (wndPtr, wParam, lParam);

	case TB_ISBUTTONENABLED:
	    return TOOLBAR_IsButtonEnabled (wndPtr, wParam, lParam);

	case TB_ISBUTTONHIDDEN:
	    return TOOLBAR_IsButtonHidden (wndPtr, wParam, lParam);

	case TB_ISBUTTONHIGHLIGHTED:
	    return TOOLBAR_IsButtonHighlighted (wndPtr, wParam, lParam);

	case TB_ISBUTTONINDETERMINATE:
	    return TOOLBAR_IsButtonIndeterminate (wndPtr, wParam, lParam);

	case TB_ISBUTTONPRESSED:
	    return TOOLBAR_IsButtonPressed (wndPtr, wParam, lParam);

//	case TB_LOADIMAGES:			/* 4.70 */
//	case TB_MAPACCELERATOR:			/* 4.71 */

//	case TB_MARKBUTTON:			/* 4.71 */
//	    return TOOLBAR_MarkButton (wndPtr, wParam, lParam);

//	case TB_MOVEBUTTON:			/* 4.71 */

	case TB_PRESSBUTTON:
	    return TOOLBAR_PressButton (wndPtr, wParam, lParam);

//	case TB_REPLACEBITMAP:
//	case TB_SAVERESTORE:
//	case TB_SETANCHORHIGHLIGHT:		/* 4.71 */

	case TB_SETBITMAPSIZE:
	    return TOOLBAR_SetBitmapSize (wndPtr, wParam, lParam);

//	case TB_SETBUTTONINFO:			/* 4.71 */

	case TB_SETBUTTONSIZE:
	    return TOOLBAR_SetButtonSize (wndPtr, wParam, lParam);

//	case TB_SETBUTTONWIDTH:			/* 4.70 */

	case TB_SETCMDID:
	    return TOOLBAR_SetCmdId (wndPtr, wParam, lParam);

//	case TB_SETCOLORSCHEME:			/* 4.71 */
//	case TB_SETDISABLEDIMAGELIST:		/* 4.70 */
//	case TB_SETDRAWTEXTFLAGS:		/* 4.71 */
//	case TB_SETEXTENDEDSTYLE:		/* 4.71 */
//	case TB_SETHOTIMAGELIST:		/* 4.70 */
//	case TB_SETHOTITEM:			/* 4.71 */
//	case TB_SETIMAGELIST:			/* 4.70 */

	case TB_SETINDENT:
	    return TOOLBAR_SetIndent (wndPtr, wParam, lParam);

//	case TB_SETINSERTMARK:			/* 4.71 */
//	case TB_SETINSERTMARKCOLOR:		/* 4.71 */
//	case TB_SETMAXTEXTROWS:			/* 4.70 */
//	case TB_SETPADDING:			/* 4.71 */
//	case TB_SETPARENT:
//	case TB_SETROWS:

	case TB_SETSTATE:
	    return TOOLBAR_SetState (wndPtr, wParam, lParam);

	case TB_SETSTYLE:
	    return TOOLBAR_SetStyle (wndPtr, wParam, lParam);

//	case TB_SETTOOLTIPS:
//	case TB_SETUNICODEFORMAT:

	case WM_CREATE:
	    return TOOLBAR_Create (wndPtr, wParam, lParam);

	case WM_DESTROY:
	    return TOOLBAR_Destroy (wndPtr, wParam, lParam);

	case WM_LBUTTONDBLCLK:
	    return TOOLBAR_LButtonDblClk (wndPtr, wParam, lParam);

	case WM_LBUTTONDOWN:
	    return TOOLBAR_LButtonDown (wndPtr, wParam, lParam);

	case WM_LBUTTONUP:
	    return TOOLBAR_LButtonUp (wndPtr, wParam, lParam);

	case WM_MOUSEMOVE:
	    return TOOLBAR_MouseMove (wndPtr, wParam, lParam);

//	case WM_NCACTIVATE:
//	    return TOOLBAR_NCActivate (wndPtr, wParam, lParam);

	case WM_NCCALCSIZE:
	    return TOOLBAR_NCCalcSize (wndPtr, wParam, lParam);

	case WM_NCPAINT:
	    return TOOLBAR_NCPaint (wndPtr, wParam, lParam);

//	case WM_NOTIFY:

	case WM_PAINT:
	    return TOOLBAR_Paint (wndPtr, wParam);

	case WM_SIZE:
	    return TOOLBAR_Size (wndPtr, wParam, lParam);

//	case WM_SYSCOLORCHANGE:

//	case WM_WININICHANGE:

	case WM_CHARTOITEM:
	case WM_COMMAND:
	case WM_DRAWITEM:
	case WM_MEASUREITEM:
	case WM_VKEYTOITEM:
	    return SendMessage32A (GetParent32 (hwnd), uMsg, wParam, lParam);

	default:
	    if (uMsg >= WM_USER)
		ERR (toolbar, "unknown msg %04x wp=%08x lp=%08lx\n",
		     uMsg, wParam, lParam);
	    return DefWindowProc32A (hwnd, uMsg, wParam, lParam);
    }
    return 0;
}


void
TOOLBAR_Register (void)
{
    WNDCLASS32A wndClass;

    if (GlobalFindAtom32A (TOOLBARCLASSNAME32A)) return;

    ZeroMemory (&wndClass, sizeof(WNDCLASS32A));
    wndClass.style         = CS_GLOBALCLASS | CS_DBLCLKS;
    wndClass.lpfnWndProc   = (WNDPROC32)ToolbarWindowProc;
    wndClass.cbClsExtra    = 0;
    wndClass.cbWndExtra    = sizeof(TOOLBAR_INFO *);
    wndClass.hCursor       = LoadCursor32A (0, IDC_ARROW32A);
    wndClass.hbrBackground = (HBRUSH32)(COLOR_3DFACE + 1);
    wndClass.lpszClassName = TOOLBARCLASSNAME32A;
 
    RegisterClass32A (&wndClass);
}

