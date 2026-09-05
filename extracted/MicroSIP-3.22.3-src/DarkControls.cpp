#include "StdAfx.h"
#include "DarkControls.h"
#include "DarkPalette.h"
#include <uxtheme.h>

IMPLEMENT_DYNAMIC(CDarkComboBox, CComboBox)

BEGIN_MESSAGE_MAP(CDarkComboBox, CComboBox)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
	ON_WM_CTLCOLOR_REFLECT()
END_MESSAGE_MAP()

CDarkComboBox::CDarkComboBox()
{
	m_darkMode = false;
	m_hover = false;
	m_tracking = false;
}

void CDarkComboBox::SetDarkMode(bool enabled)
{
	m_darkMode = enabled;
	if (::IsWindow(m_hWnd)) {
		// DarkMode_CFD is the only theme class that darkens the drop-down list window itself.
		SetWindowTheme(m_hWnd, enabled ? L"DarkMode_CFD" : NULL, NULL);
		Invalidate();
	}
}

void CDarkComboBox::DrawDarkChrome(CDC* dc)
{
	CRect client;
	GetClientRect(&client);
	dc->FillSolidRect(client, DarkPalette::Input());

	bool pressed = GetDroppedState() != FALSE;
	bool enabled = IsWindowEnabled() != FALSE;
	int buttonWidth = ::GetSystemMetrics(SM_CXVSCROLL);
	CRect button(client.right - buttonWidth - 1, client.top + 1, client.right - 1, client.bottom - 1);
	COLORREF buttonFace = !enabled ? DarkPalette::Surface()
		: (pressed ? DarkPalette::Selected() : (m_hover ? DarkPalette::Active() : DarkPalette::Surface()));
	dc->FillSolidRect(button, buttonFace);

	CRect textRect(client);
	textRect.DeflateRect(1, 1);
	textRect.right = button.left;
	if ((GetStyle() & 0x0003) == CBS_DROPDOWNLIST) {
		// Only a drop-down list paints its own text; an editable combo has a child edit control.
		CString text;
		GetWindowText(text);
		int oldBkMode = dc->SetBkMode(TRANSPARENT);
		COLORREF oldColor = dc->SetTextColor(enabled ? DarkPalette::Text() : DarkPalette::DisabledText());
		CFont* font = GetFont();
		CFont* oldFont = font ? dc->SelectObject(font) : NULL;
		CRect labelRect(textRect);
		labelRect.DeflateRect(::GetSystemMetrics(SM_CXEDGE) + 2, 0);
		dc->DrawText(text, labelRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
		if (oldFont) {
			dc->SelectObject(oldFont);
		}
		dc->SetTextColor(oldColor);
		dc->SetBkMode(oldBkMode);
	}

	CPoint centre(button.CenterPoint());
	int arrow = max(2, ::MulDiv(3, button.Width(), 16));
	POINT triangle[3] = {
		{ centre.x - arrow, centre.y - arrow / 2 },
		{ centre.x + arrow, centre.y - arrow / 2 },
		{ centre.x, centre.y + arrow - arrow / 2 }
	};
	CBrush arrowBrush(enabled ? DarkPalette::Text() : DarkPalette::DisabledText());
	CBrush* oldBrush = dc->SelectObject(&arrowBrush);
	CPen arrowPen(PS_SOLID, 1, enabled ? DarkPalette::Text() : DarkPalette::DisabledText());
	CPen* oldPen = dc->SelectObject(&arrowPen);
	dc->Polygon(triangle, 3);
	dc->SelectObject(oldPen);
	dc->SelectObject(oldBrush);

	CBrush borderBrush(DarkPalette::Border());
	dc->FrameRect(client, &borderBrush);
}

void CDarkComboBox::OnPaint()
{
	if (!m_darkMode) {
		Default();
		return;
	}
	CPaintDC dc(this);
	DrawDarkChrome(&dc);
}

BOOL CDarkComboBox::OnEraseBkgnd(CDC* dc)
{
	if (!m_darkMode) {
		return CComboBox::OnEraseBkgnd(dc);
	}
	CRect client;
	GetClientRect(&client);
	dc->FillSolidRect(client, DarkPalette::Input());
	return TRUE;
}

void CDarkComboBox::OnMouseMove(UINT flags, CPoint point)
{
	if (m_darkMode && !m_tracking) {
		TRACKMOUSEEVENT track = { sizeof(TRACKMOUSEEVENT) };
		track.dwFlags = TME_LEAVE;
		track.hwndTrack = m_hWnd;
		if (TrackMouseEvent(&track)) {
			m_tracking = true;
			m_hover = true;
			Invalidate();
		}
	}
	CComboBox::OnMouseMove(flags, point);
}

LRESULT CDarkComboBox::OnMouseLeave(WPARAM wParam, LPARAM lParam)
{
	m_tracking = false;
	if (m_hover) {
		m_hover = false;
		Invalidate();
	}
	return Default();
}

HBRUSH CDarkComboBox::CtlColor(CDC* dc, UINT controlColor)
{
	if (!m_darkMode) {
		return NULL;
	}
	static CBrush darkInputBrush(DarkPalette::Input());
	static CBrush darkListBrush(DarkPalette::Surface());
	dc->SetTextColor(DarkPalette::Text());
	if (controlColor == CTLCOLOR_LISTBOX) {
		dc->SetBkColor(DarkPalette::Surface());
		return darkListBrush;
	}
	dc->SetBkColor(DarkPalette::Input());
	return darkInputBrush;
}

IMPLEMENT_DYNAMIC(CDarkTabCtrl, CTabCtrl)

BEGIN_MESSAGE_MAP(CDarkTabCtrl, CTabCtrl)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CDarkTabCtrl::CDarkTabCtrl()
{
	m_darkMode = false;
}

void CDarkTabCtrl::SetDarkMode(bool enabled)
{
	m_darkMode = enabled;
	if (::IsWindow(m_hWnd)) {
		// Empty theme strings switch visual styles off so the native pane/border is never drawn.
		SetWindowTheme(m_hWnd, enabled ? L"" : NULL, enabled ? L"" : NULL);
		Invalidate();
	}
}

BOOL CDarkTabCtrl::OnEraseBkgnd(CDC* dc)
{
	if (!m_darkMode) {
		return CTabCtrl::OnEraseBkgnd(dc);
	}
	CRect client;
	GetClientRect(&client);
	dc->FillSolidRect(client, DarkPalette::Window());
	return TRUE;
}

void CDarkTabCtrl::OnPaint()
{
	if (!m_darkMode) {
		Default();
		return;
	}
	CPaintDC dc(this);
	CRect client;
	GetClientRect(&client);
	dc.FillSolidRect(client, DarkPalette::Window());

	CBrush borderBrush(DarkPalette::Border());
	CImageList* images = GetImageList();
	int selected = GetCurSel();
	int oldBkMode = dc.SetBkMode(TRANSPARENT);
	CFont* font = GetFont();
	CFont* oldFont = font ? dc.SelectObject(font) : NULL;
	for (int i = 0; i < GetItemCount(); i++) {
		CRect item;
		if (!GetItemRect(i, &item)) {
			continue;
		}
		bool active = i == selected;
		dc.FillSolidRect(item, active ? DarkPalette::Selected() : DarkPalette::Surface());
		dc.FrameRect(item, &borderBrush);

		TCHAR label[256] = { 0 };
		TCITEM tci = { 0 };
		tci.mask = TCIF_TEXT | TCIF_IMAGE;
		tci.pszText = label;
		tci.cchTextMax = _countof(label);
		if (!GetItem(i, &tci)) {
			continue;
		}
		CRect text(item);
		text.DeflateRect(2, 0);
		if (images && images->m_hImageList && tci.iImage >= 0) {
			IMAGEINFO info;
			if (images->GetImageInfo(tci.iImage, &info)) {
				int width = info.rcImage.right - info.rcImage.left;
				int height = info.rcImage.bottom - info.rcImage.top;
				images->Draw(&dc, tci.iImage, CPoint(text.left + 2, text.top + max(0, (text.Height() - height) / 2)), ILD_TRANSPARENT);
				text.left += width + 4;
			}
		}
		dc.SetTextColor(active ? DarkPalette::Text() : DarkPalette::SecondaryText());
		dc.DrawText(label, text, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	if (oldFont) {
		dc.SelectObject(oldFont);
	}
	dc.SetBkMode(oldBkMode);
}
