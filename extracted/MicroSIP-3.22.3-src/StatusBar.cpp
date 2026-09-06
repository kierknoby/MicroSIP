/*
 * Copyright (C) 2011-2024 MicroSIP (http://www.microsip.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "StdAfx.h"   
#include "StatusBar.h"   
#include "global.h"
#include "settings.h"
#include "DarkPalette.h"
#include <shellapi.h>

 // StatusBar   
IMPLEMENT_DYNAMIC(StatusBar, CStatusBar)
StatusBar::StatusBar()
{
	CStatusBar::CStatusBar();
}

StatusBar::~StatusBar()
{
}

BEGIN_MESSAGE_MAP(StatusBar, CStatusBar)
	//{{AFX_MSG_MAP(StatusBar)   
	ON_MESSAGE(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_PAINT()
	ON_WM_SETCURSOR()
	//}}AFX_MSG_MAP   
END_MESSAGE_MAP()

// Dark mode only; the light path stays on the default status bar rendering.
void StatusBar::OnPaint()
{
	if (!accountSettings.darkMode) {
		Default();
		return;
	}
	CPaintDC dc(this);
	CRect client;
	GetClientRect(&client);
	dc.FillSolidRect(client, DarkPalette::Window());
	dc.FillSolidRect(client.left, client.top, client.Width(), 1, DarkPalette::Separator());
	int oldMode = dc.SetBkMode(TRANSPARENT);
	COLORREF oldColor = dc.SetTextColor(DarkPalette::Text());
	CFont* oldFont = dc.SelectObject(GetFont());
	CStatusBarCtrl& control = GetStatusBarCtrl();
	for (int i = 0; i < m_nCount; i++) {
		CRect rect;
		GetItemRect(i, &rect);
		if (rect.IsRectEmpty()) {
			continue;
		}
		rect.DeflateRect(2, 0);
		if (i) dc.FillSolidRect(rect.left - 2, rect.top, 1, rect.Height(), DarkPalette::Separator());
		HICON icon = control.GetIcon(i);
		if (icon) {
			int size = GetSystemMetrics(SM_CXSMICON);
			int iconTop = rect.top + max(0, (rect.Height() - size) / 2);
			::DrawIconEx(dc.m_hDC, rect.left, iconTop, icon, size, size, 0, NULL, DI_NORMAL);
			rect.left += size + 2;
		}
		CString text = GetPaneText(i);
		if (!text.IsEmpty()) {
			dc.DrawText(text, rect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
		}
	}
	dc.SelectObject(oldFont);
	dc.SetTextColor(oldColor);
	dc.SetBkMode(oldMode);
}

LRESULT StatusBar::OnIdleUpdateCmdUI(WPARAM wParam, LPARAM lParam)
{
	if (IsWindowVisible())
	{
		CFrameWnd* pParent = (CFrameWnd*)GetParent();
		if (pParent)
			OnUpdateCmdUI(pParent, (BOOL)wParam);
	}
	return 0L;
}

void StatusBar::OnLButtonUp(UINT nFlags, CPoint point)
{
	CRect publisher;
	if (GetPublisherLinkRect(publisher) && publisher.PtInRect(point)) {
		ShellExecute(NULL, _T("open"), _T("https://egyptianeyes.com"), NULL, NULL, SW_SHOWNORMAL);
	}
	CStatusBar::OnLButtonUp(nFlags, point);
}

void StatusBar::OnMouseMove(UINT nFlags, CPoint point)
{
	CStatusBar::OnMouseMove(nFlags, point);
}

bool StatusBar::GetPublisherLinkRect(CRect& rect)
{
	rect.SetRectEmpty();
	CString expected;
	expected.Format(_T("%s by Egyptian Eyes"), _T(_DIALTONE_VERSION));
	if (GetPaneText(0) != expected) {
		return false;
	}
	CRect pane;
	GetItemRect(0, &pane);
	pane.DeflateRect(2, 0);
	if (GetStatusBarCtrl().GetIcon(0)) {
		pane.left += GetSystemMetrics(SM_CXSMICON) + 2;
	}
	CString prefix;
	prefix.Format(_T("%s by "), _T(_DIALTONE_VERSION));
	CDC* dc = GetDC();
	if (!dc) {
		return false;
	}
	CFont* oldFont = dc->SelectObject(GetFont());
	int prefixWidth = dc->GetTextExtent(prefix).cx;
	int publisherWidth = dc->GetTextExtent(_T("Egyptian Eyes")).cx;
	dc->SelectObject(oldFont);
	ReleaseDC(dc);
	rect.SetRect(pane.left + prefixWidth, pane.top,
		min(pane.right, pane.left + prefixWidth + publisherWidth), pane.bottom);
	return !rect.IsRectEmpty();
}

BOOL StatusBar::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	CPoint point;
	GetCursorPos(&point);
	ScreenToClient(&point);
	CRect publisher;
	if (GetPublisherLinkRect(publisher) && publisher.PtInRect(point)) {
		SetCursor(LoadCursor(NULL, IDC_HAND));
		return TRUE;
	}
	return CStatusBar::OnSetCursor(pWnd, nHitTest, message);
}
