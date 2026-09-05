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

IMPLEMENT_DYNAMIC(CPlaceholderEdit, CEdit)

BEGIN_MESSAGE_MAP(CPlaceholderEdit, CEdit)
	ON_WM_PAINT()
	ON_CONTROL_REFLECT(EN_CHANGE, OnChange)
END_MESSAGE_MAP()

CPlaceholderEdit::CPlaceholderEdit()
{
	m_darkMode = false;
	m_wasEmpty = true;
}

void CPlaceholderEdit::SetPlaceholder(LPCTSTR text)
{
	m_placeholder = text;
	if (::IsWindow(m_hWnd)) {
		Invalidate();
	}
}

void CPlaceholderEdit::SetDarkMode(bool enabled)
{
	m_darkMode = enabled;
	if (::IsWindow(m_hWnd)) {
		Invalidate();
	}
}

void CPlaceholderEdit::OnChange()
{
	bool empty = GetWindowTextLength() == 0;
	if (empty != m_wasEmpty) {
		m_wasEmpty = empty;
		Invalidate();
	}
}

void CPlaceholderEdit::OnPaint()
{
	Default();
	if (m_placeholder.IsEmpty() || GetWindowTextLength() > 0) {
		return;
	}
	CClientDC dc(this);
	CRect rect;
	GetRect(&rect);
	CFont* font = GetFont();
	CFont* oldFont = font ? dc.SelectObject(font) : NULL;
	int oldBkMode = dc.SetBkMode(TRANSPARENT);
	COLORREF oldColor = dc.SetTextColor(m_darkMode ? DarkPalette::SecondaryText() : GetSysColor(COLOR_GRAYTEXT));
	dc.DrawText(m_placeholder, rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
	dc.SetTextColor(oldColor);
	dc.SetBkMode(oldBkMode);
	if (oldFont) {
		dc.SelectObject(oldFont);
	}
}

IMPLEMENT_DYNAMIC(CDarkIconButton, CButton)

BEGIN_MESSAGE_MAP(CDarkIconButton, CButton)
END_MESSAGE_MAP()

CDarkIconButton::CDarkIconButton()
{
	m_icon = NULL;
	m_iconSize = 16;
	m_darkMode = false;
}

void CDarkIconButton::SetButtonIcon(HICON icon)
{
	m_icon = icon;
	ICONINFO info = { 0 };
	if (icon && ::GetIconInfo(icon, &info)) {
		BITMAP bitmap = { 0 };
		HBITMAP source = info.hbmColor ? info.hbmColor : info.hbmMask;
		if (source && ::GetObject(source, sizeof(bitmap), &bitmap) && bitmap.bmWidth > 0) {
			m_iconSize = bitmap.bmWidth;
		}
		if (info.hbmColor) {
			::DeleteObject(info.hbmColor);
		}
		if (info.hbmMask) {
			::DeleteObject(info.hbmMask);
		}
	}
	if (::IsWindow(m_hWnd)) {
		SetIcon(icon);
	}
}

void CDarkIconButton::SetDarkMode(bool enabled)
{
	m_darkMode = enabled;
	if (!::IsWindow(m_hWnd)) {
		return;
	}
	if (enabled) {
		ModifyStyle(BS_TYPEMASK, BS_OWNERDRAW);
	}
	else {
		ModifyStyle(BS_TYPEMASK, BS_PUSHBUTTON | BS_ICON);
		SetIcon(m_icon);
	}
	Invalidate();
}

void CDarkIconButton::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	CDC* dc = CDC::FromHandle(lpDIS->hDC);
	if (!dc) {
		return;
	}
	CRect rect(lpDIS->rcItem);
	bool pressed = (lpDIS->itemState & ODS_SELECTED) != 0;
	bool enabled = (lpDIS->itemState & ODS_DISABLED) == 0;
	dc->FillSolidRect(rect, pressed ? DarkPalette::Selected() : DarkPalette::Surface());
	CBrush border(DarkPalette::Border());
	dc->FrameRect(rect, &border);
	if (!m_icon) {
		return;
	}
	int x = rect.left + (rect.Width() - m_iconSize) / 2;
	int y = rect.top + (rect.Height() - m_iconSize) / 2;
	// DSS_MONO repaints the icon as a silhouette in the brush colour, so a dark glyph stays visible.
	CBrush glyph(enabled ? DarkPalette::Text() : DarkPalette::DisabledText());
	::DrawState(dc->m_hDC, (HBRUSH)glyph.GetSafeHandle(), NULL, (LPARAM)m_icon, 0,
		x, y, m_iconSize, m_iconSize, DST_ICON | DSS_MONO);
}
