#pragma once

// Takes ownership of the combo box chrome in dark mode only; light mode keeps the native control.
class CDarkComboBox : public CComboBox
{
	DECLARE_DYNAMIC(CDarkComboBox)

public:
	CDarkComboBox();
	void SetDarkMode(bool enabled);

protected:
	bool m_darkMode;
	bool m_hover;
	bool m_tracking;

	void DrawDarkChrome(CDC* dc);

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	afx_msg void OnMouseMove(UINT flags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM wParam, LPARAM lParam);
	afx_msg HBRUSH CtlColor(CDC* dc, UINT controlColor);
	DECLARE_MESSAGE_MAP()
};

// Paints the whole tab control, strip included, so no native chrome survives in dark mode.
class CDarkTabCtrl : public CTabCtrl
{
	DECLARE_DYNAMIC(CDarkTabCtrl)

public:
	CDarkTabCtrl();
	void SetDarkMode(bool enabled);

protected:
	bool m_darkMode;

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	DECLARE_MESSAGE_MAP()
};

// Multiline edit with a painted-only hint; the hint never becomes control text.
class CPlaceholderEdit : public CEdit
{
	DECLARE_DYNAMIC(CPlaceholderEdit)

public:
	CPlaceholderEdit();
	void SetPlaceholder(LPCTSTR text);
	void SetDarkMode(bool enabled);

protected:
	CString m_placeholder;
	bool m_darkMode;
	bool m_wasEmpty;

	afx_msg void OnPaint();
	afx_msg BOOL OnChange();
	DECLARE_MESSAGE_MAP()
};

// Icon push button that takes over drawing only in dark mode; light mode stays fully native.
class CDarkIconButton : public CButton
{
	DECLARE_DYNAMIC(CDarkIconButton)

public:
	CDarkIconButton();
	void SetButtonIcon(HICON icon);
	void SetDarkMode(bool enabled);

protected:
	HICON m_icon;
	int m_iconSize;
	bool m_darkMode;

	virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);
	DECLARE_MESSAGE_MAP()
};
