#include "stdafx.h"

#include "CListCtrl_Sortable.h"
#include "Resource.h"
#include "mainDlg.h"
#include "DarkPalette.h"

#include <shlwapi.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

static void EnableWindowThemeDark(HWND hwnd, bool enabled)
{
	::SetWindowTheme(hwnd, enabled ? L"DarkMode_Explorer" : NULL, NULL);
}

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

BEGIN_MESSAGE_MAP(CListCtrl_Sortable, CListCtrl_LabelTip)
	ON_NOTIFY_REFLECT_EX(LVN_COLUMNCLICK, OnHeaderClick)	// Column Click
END_MESSAGE_MAP()

BEGIN_MESSAGE_MAP(CDarkHeaderCtrl, CHeaderCtrl)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
END_MESSAGE_MAP()

void CDarkHeaderCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMCUSTOMDRAW* draw = (NMCUSTOMDRAW*)pNMHDR;
	if (!m_darkMode) {
		*pResult = CDRF_DODEFAULT;
		return;
	}
	if (draw->dwDrawStage == CDDS_PREPAINT) {
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;
	}
	if (draw->dwDrawStage != CDDS_ITEMPREPAINT) {
		*pResult = CDRF_DODEFAULT;
		return;
	}
	CDC* dc = CDC::FromHandle(draw->hdc);
	if (!dc) {
		*pResult = CDRF_DODEFAULT;
		return;
	}
	CRect rect = draw->rc;
	dc->FillSolidRect(&rect, (draw->uItemState & CDIS_SELECTED) ? DarkPalette::Selected() : DarkPalette::Surface());
	dc->FillSolidRect(rect.right - 1, rect.top, 1, rect.Height(), DarkPalette::Border());
	dc->FillSolidRect(rect.left, rect.bottom - 1, rect.Width(), 1, DarkPalette::Separator());
	TCHAR text[256] = { 0 };
	HDITEM item = { 0 };
	item.mask = HDI_TEXT | HDI_FORMAT;
	item.pszText = text;
	item.cchTextMax = _countof(text);
	if (GetItem((int)draw->dwItemSpec, &item)) {
		int oldMode = dc->SetBkMode(TRANSPARENT);
		COLORREF oldColor = dc->SetTextColor(DarkPalette::Text());
		CFont* oldFont = dc->SelectObject(GetFont());
		rect.DeflateRect(4, 0);
		UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
		format |= (item.fmt & HDF_CENTER) ? DT_CENTER : ((item.fmt & HDF_RIGHT) ? DT_RIGHT : DT_LEFT);
		dc->DrawText(text, rect, format);
		dc->SelectObject(oldFont);
		dc->SetTextColor(oldColor);
		dc->SetBkMode(oldMode);
	}
	*pResult = CDRF_SKIPDEFAULT;
}

void CListCtrl_Sortable::SetDarkMode(bool enabled)
{
	if (!::IsWindow(m_hWnd)) {
		return;
	}
	SetBkColor(enabled ? DarkPalette::Window() : GetSysColor(COLOR_WINDOW));
	SetTextBkColor(enabled ? DarkPalette::Window() : GetSysColor(COLOR_WINDOW));
	SetTextColor(enabled ? DarkPalette::Text() : GetSysColor(COLOR_WINDOWTEXT));
	EnableWindowThemeDark(m_hWnd, enabled);
	CHeaderCtrl* header = GetHeaderCtrl();
	if (header && ::IsWindow(header->m_hWnd)) {
		if (!::IsWindow(m_DarkHeader.m_hWnd)) {
			m_DarkHeader.SubclassWindow(header->m_hWnd);
		}
		m_DarkHeader.m_darkMode = enabled;
		m_DarkHeader.Invalidate();
	}
	Invalidate();
}

namespace {
	bool IsThemeEnabled()
	{
		HMODULE hinstDll;
		bool XPStyle = false;
		bool (__stdcall *pIsAppThemed)();
		bool (__stdcall *pIsThemeActive)();

		// Test if operating system has themes enabled
		hinstDll = ::LoadLibraryEx(_T("UxTheme.dll"), 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (hinstDll)
		{
			(FARPROC&)pIsAppThemed = ::GetProcAddress(hinstDll, "IsAppThemed");
			(FARPROC&)pIsThemeActive = ::GetProcAddress(hinstDll, "IsThemeActive");
			if (pIsAppThemed != NULL && pIsThemeActive != NULL)
			{
				if (pIsAppThemed() && pIsThemeActive())
				{
					// Test if application has themes enabled by loading the proper DLL
					HMODULE hinstDll2 = ::LoadLibraryEx(_T("comctl32.dll"), 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
					if (hinstDll2)
					{
						DLLGETVERSIONPROC pDllGetVersion = (DLLGETVERSIONPROC)::GetProcAddress(hinstDll2, "DllGetVersion");
						if (pDllGetVersion != NULL)
						{
							DLLVERSIONINFO dvi;
							ZeroMemory(&dvi, sizeof(dvi));
							dvi.cbSize = sizeof(dvi);
							HRESULT hRes = pDllGetVersion((DLLVERSIONINFO *)&dvi);
							if (SUCCEEDED(hRes))
								XPStyle = dvi.dwMajorVersion >= 6;
						}
						::FreeLibrary(hinstDll2);
					}
				}
			}
			::FreeLibrary(hinstDll);
		}
		return XPStyle;
	}

	LRESULT EnableWindowTheme(HWND hwnd, LPCWSTR app, LPCWSTR idlist)
	{
		HMODULE hinstDll;
		HRESULT (__stdcall *pSetWindowTheme)(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);
		HANDLE (__stdcall *pOpenThemeData)(HWND hwnd, LPCWSTR pszClassList);
		HRESULT (__stdcall *pCloseThemeData)(HANDLE hTheme);

		hinstDll = ::LoadLibraryEx(_T("UxTheme.dll"), 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (hinstDll)
		{
			(FARPROC&)pOpenThemeData = ::GetProcAddress(hinstDll, "OpenThemeData");
			(FARPROC&)pCloseThemeData = ::GetProcAddress(hinstDll, "CloseThemeData");
			(FARPROC&)pSetWindowTheme = ::GetProcAddress(hinstDll, "SetWindowTheme");
			if (pSetWindowTheme && pOpenThemeData && pCloseThemeData)
			{
				HANDLE theme = pOpenThemeData(hwnd, L"ListView");
				if (theme != NULL)
				{
					VERIFY(pCloseThemeData(theme) == S_OK);
					return pSetWindowTheme(hwnd, app, idlist);
				}
			}
			::FreeLibrary(hinstDll);
		}
		return S_FALSE;
	}
}

BOOL CListCtrl_Sortable::OnHeaderClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* pLV = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	SetFocus();	// Ensure other controls gets kill-focus

	int colIndex = pLV->iSubItem;

	if (m_SortCol==colIndex)
	{
		m_Ascending = !m_Ascending;
	}
	else
	{
		m_SortCol = colIndex;
		m_Ascending = true;
	}

	if (SortColumn(m_SortCol, m_Ascending))
		SetSortArrow(m_SortCol, m_Ascending);

	return FALSE;	// Let parent-dialog get chance
}

void CListCtrl_Sortable::SetSortArrow(int colIndex, bool ascending)
{
	if (IsThemeEnabled())
	{
#if (_WIN32_WINNT >= 0x501)
		for(int i = 0; i < GetHeaderCtrl()->GetItemCount(); ++i)
		{
			HDITEM hditem = {0};
			hditem.mask = HDI_FORMAT;
			VERIFY( GetHeaderCtrl()->GetItem( i, &hditem ) );
			hditem.fmt &= ~(HDF_SORTDOWN|HDF_SORTUP);
			if (i == colIndex)
			{
				hditem.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
			}
			VERIFY( CListCtrl_LabelTip::GetHeaderCtrl()->SetItem( i, &hditem ) );
		}
#endif
	}
	else
	{
		UINT bitmapID = m_Ascending ? IDB_UPARROW : IDB_DOWNARROW; 
		for(int i = 0; i < GetHeaderCtrl()->GetItemCount(); ++i)
		{
			HDITEM hditem = {0};
			hditem.mask = HDI_BITMAP | HDI_FORMAT;
			VERIFY( GetHeaderCtrl()->GetItem( i, &hditem ) );
			if (hditem.fmt & HDF_BITMAP && hditem.fmt & HDF_BITMAP_ON_RIGHT)
			{
				if (hditem.hbm)
				{
					DeleteObject(hditem.hbm);
					hditem.hbm = NULL;
				}
				hditem.fmt &= ~(HDF_BITMAP|HDF_BITMAP_ON_RIGHT);
				VERIFY( CListCtrl_LabelTip::GetHeaderCtrl()->SetItem( i, &hditem ) );
			}
			if (i == colIndex)
			{
				hditem.fmt |= HDF_BITMAP|HDF_BITMAP_ON_RIGHT;
				hditem.hbm = (HBITMAP)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(bitmapID), IMAGE_BITMAP, 0,0, LR_LOADMAP3DCOLORS); 
				VERIFY( hditem.hbm!=NULL );
				VERIFY( CListCtrl_LabelTip::GetHeaderCtrl()->SetItem( i, &hditem ) );
			}
		}
	}
}

void CListCtrl_Sortable::PreSubclassWindow()
{
	CListCtrl_LabelTip::PreSubclassWindow();

	// Focus retangle is not painted properly without double-buffering
#if (_WIN32_WINNT >= 0x501)
	SetExtendedStyle(LVS_EX_DOUBLEBUFFER | GetExtendedStyle());
#endif
	SetExtendedStyle(GetExtendedStyle() | LVS_EX_FULLROWSELECT);
	SetExtendedStyle(GetExtendedStyle() | LVS_EX_HEADERDRAGDROP);

	EnableWindowTheme(GetSafeHwnd(), L"Explorer", NULL);
}

void CListCtrl_Sortable::ResetSortOrder()
{
	m_Ascending = true;
	m_SortCol = -1;
	SetSortArrow(m_SortCol, m_Ascending);
}

// The column version of GetItemData(), one can specify an unique
// identifier when using InsertColumn()
int CListCtrl_Sortable::GetColumnData(int col) const
{
	LVCOLUMN lvc = {0};
	lvc.mask = LVCF_SUBITEM;
	VERIFY( GetColumn(col, &lvc) );
	return lvc.iSubItem;
}

void CListCtrl_Sortable::SetSortColumn(int columnIndex, bool ascending)
{
	m_SortCol = columnIndex;
	m_Ascending = ascending;
	if (SortColumn(m_SortCol, m_Ascending)) {
		SetSortArrow(m_SortCol, m_Ascending);
	}
}
