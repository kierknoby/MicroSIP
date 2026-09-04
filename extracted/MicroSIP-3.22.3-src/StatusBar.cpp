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
	dc.FillSolidRect(client, RGB(30, 34, 38));
	dc.FillSolidRect(client.left, client.top, client.Width(), 1, RGB(71, 78, 86));
	int oldMode = dc.SetBkMode(TRANSPARENT);
	COLORREF oldColor = dc.SetTextColor(RGB(235, 238, 241));
	CFont* oldFont = dc.SelectObject(GetFont());
	CStatusBarCtrl& control = GetStatusBarCtrl();
	for (int i = 0; i < m_nCount; i++) {
		CRect rect;
		GetItemRect(i, &rect);
		if (rect.IsRectEmpty()) {
			continue;
		}
		rect.DeflateRect(2, 0);
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
}

void StatusBar::OnMouseMove(UINT nFlags, CPoint point)
{
}
