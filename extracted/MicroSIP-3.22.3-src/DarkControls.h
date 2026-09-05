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
