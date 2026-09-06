#pragma once 

#include "const.h"

class StatusBar : public CStatusBar
{ 
	DECLARE_DYNAMIC(StatusBar) 
public: 
	StatusBar();
	virtual ~StatusBar();
	void RefreshPublisherLink();
	virtual BOOL PreTranslateMessage(MSG* message);
protected: 
	DECLARE_MESSAGE_MAP()
public: 
	afx_msg LRESULT OnIdleUpdateCmdUI(WPARAM wParam,LPARAM lParam);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnPaint();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
private:
	bool GetPublisherLinkRect(CRect& rect);
	CToolTipCtrl publisherTooltip;
	bool publisherTooltipAdded = false;
};
