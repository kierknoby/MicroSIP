/*
 * Copyright (C) 2011-2025 MicroSIP (http://www.microsip.org)
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
#include "Dialer.h"
#include <uxtheme.h>
#include "global.h"
#include "settings.h"
#include "mainDlg.h"
#include "microsip.h"
#include "Strsafe.h"
#include "langpack.h"
#include "Hid.h"
#include "ButtonSafe.h"
#include "DarkPalette.h"

static CString digitsDTMFDelayed;

class Dialer::CAccountSidecar : public CWnd
{
public:
	CAccountSidecar(Dialer* owner) : owner(owner), darkMode(false) {}
	~CAccountSidecar()
	{
		for (int i = 0; i < buttons.GetCount(); i++) {
			delete buttons[i];
		}
	}

	void ShowAccounts()
	{
		for (int i = 0; i < buttons.GetCount(); i++) {
			delete buttons[i];
		}
		buttons.RemoveAll();
		accountIds.RemoveAll();
		if (::IsWindow(emptyState.m_hWnd)) {
			emptyState.DestroyWindow();
		}

		int width = MulDiv(190, dpiY, 96);
		int buttonHeight = MulDiv(34, dpiY, 96);
		int accountId = 1;
		Account account;
		while (accountSettings.AccountLoad(accountId, &account)) {
			CString label = account.username;
			if (!account.displayName.IsEmpty()) {
				label.Append(_T(" \x00b7 ") + account.displayName);
			}
			if (accountSettings.accountId == accountId) {
				label = _T("\x2022 ") + label;
			}
			CButton* button = new CButton();
			button->Create(label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
				CRect(0, (accountId - 1) * buttonHeight, width, accountId * buttonHeight), this, 5000 + accountId);
			button->SetFont(owner->GetFont());
			buttons.Add(button);
			accountIds.Add(accountId);
			accountId++;
		}

		int height = buttons.GetCount() * buttonHeight;
		if (!buttons.GetCount()) {
			emptyState.Create(Translate(_T("No accounts configured")), WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
				CRect(0, 0, width, buttonHeight), this);
			emptyState.SetFont(owner->GetFont());
			height = buttonHeight;
		}
		CRect identityRect;
		CWnd* identity = owner->GetDlgItem(IDC_DIALER_ACCOUNT_IDENTITY);
		identity->GetWindowRect(&identityRect);
		CWnd* mainWindow = owner->GetParent();
		CRect mainRect;
		mainWindow->GetWindowRect(&mainRect);
		MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
		GetMonitorInfo(MonitorFromWindow(mainWindow->m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
		int x = mainRect.left - width - MulDiv(4, dpiY, 96);
		int y = identityRect.top;
		if (x < monitorInfo.rcWork.left) {
			x = mainRect.right + MulDiv(4, dpiY, 96);
		}
		x = max(monitorInfo.rcWork.left, min(x, monitorInfo.rcWork.right - width));
		y = max(monitorInfo.rcWork.top, min(y, monitorInfo.rcWork.bottom - height));
		SetWindowPos(&CWnd::wndTop, x, y, width, height, SWP_SHOWWINDOW);
		if (buttons.GetCount()) {
			buttons[0]->SetFocus();
		}
		SetDarkMode(accountSettings.darkMode);
	}

	void SetDarkMode(bool enabled)
	{
		darkMode = enabled;
		LPCWSTR theme = enabled ? L"DarkMode_Explorer" : NULL;
		for (int i = 0; i < buttons.GetCount(); i++) {
			SetWindowTheme(buttons[i]->m_hWnd, theme, NULL);
			buttons[i]->Invalidate();
		}
		Invalidate();
	}

protected:
	afx_msg BOOL OnEraseBkgnd(CDC* dc)
	{
		if (darkMode) {
			CRect rect;
			GetClientRect(&rect);
			dc->FillSolidRect(rect, RGB(30, 34, 38));
			return TRUE;
		}
		return CWnd::OnEraseBkgnd(dc);
	}
	afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* child, UINT controlColor)
	{
		if (darkMode && (controlColor == CTLCOLOR_STATIC || controlColor == CTLCOLOR_DLG)) {
			static CBrush darkBrush(RGB(30, 34, 38));
			dc->SetTextColor(RGB(235, 238, 241));
			dc->SetBkColor(RGB(30, 34, 38));
			return darkBrush;
		}
		return CWnd::OnCtlColor(dc, child, controlColor);
	}
	afx_msg void OnKillFocus(CWnd* newWindow)
	{
		if (newWindow && (newWindow == owner || owner->IsChild(newWindow) || IsChild(newWindow))) {
			return;
		}
		owner->CloseAccountSidecar();
	}
	afx_msg void OnActivate(UINT state, CWnd*, BOOL)
	{
		if (state == WA_INACTIVE) {
			owner->CloseAccountSidecar();
		}
	}
	afx_msg void OnKeyDown(UINT key, UINT repeat, UINT flags)
	{
		if (key == VK_ESCAPE) {
			owner->CloseAccountSidecar();
			return;
		}
		CWnd::OnKeyDown(key, repeat, flags);
	}
	BOOL PreTranslateMessage(MSG* message) override
	{
		if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE) {
			owner->CloseAccountSidecar();
			return TRUE;
		}
		return CWnd::PreTranslateMessage(message);
	}
	afx_msg void OnAccount(UINT commandId)
	{
		int index = commandId - 5000 - 1;
		if (index >= 0 && index < accountIds.GetCount()) {
			int accountId = accountIds[index];
			owner->CloseAccountSidecar();
			mainDlg->OnMenuAccountChange(ID_ACCOUNT_CHANGE_RANGE + accountId - 1);
		}
	}
	DECLARE_MESSAGE_MAP()

private:
	Dialer* owner;
	bool darkMode;
	CArray<CButton*, CButton*> buttons;
	CArray<int, int> accountIds;
	CStatic emptyState;
};

BEGIN_MESSAGE_MAP(Dialer::CAccountSidecar, CWnd)
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_KILLFOCUS()
	ON_WM_ACTIVATE()
	ON_WM_KEYDOWN()
	ON_COMMAND_RANGE(5001, 5099, OnAccount)
END_MESSAGE_MAP()

static UINT_PTR blinkTimer = NULL;
static bool blinkState = false;

Dialer::Dialer(CWnd* pParent /*=NULL*/)
	: CBaseDialog(Dialer::IDD, pParent)
{
	m_accountSidecar = NULL;
	delayedDTMF = false;
	m_hasVoicemail = false;
	m_dialToneSessionActive = false;
	m_isButtonVoicemailVisible = false;
	m_rebuildingButtons = false;
	Create(IDD, pParent);
}

Dialer::~Dialer(void)
{
	CloseAccountSidecar();
}

void Dialer::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DIALER_VOICEMAIL, m_ButtonVoicemail);
	DDX_Control(pDX, IDC_DIALER_VOICEMAIL_DISABLED, m_ButtonVoicemailDisabled);
	DDX_Control(pDX, IDC_VOLUME_INPUT, m_SliderCtrlInput);
	DDX_Control(pDX, IDC_VOLUME_OUTPUT, m_SliderCtrlOutput);
	DDX_Control(pDX, IDC_BUTTON_MINUS_INPUT, m_ButtonMinusInput);
	DDX_Control(pDX, IDC_BUTTON_MINUS_OUTPUT, m_ButtonMinusOutput);
	DDX_Control(pDX, IDC_BUTTON_PLUS_INPUT, m_ButtonPlusInput);
	DDX_Control(pDX, IDC_BUTTON_PLUS_OUTPUT, m_ButtonPlusOutput);

	DDX_Control(pDX, IDC_KEY_1, m_ButtonDialer1);
	DDX_Control(pDX, IDC_KEY_2, m_ButtonDialer2);
	DDX_Control(pDX, IDC_KEY_3, m_ButtonDialer3);
	DDX_Control(pDX, IDC_KEY_4, m_ButtonDialer4);
	DDX_Control(pDX, IDC_KEY_5, m_ButtonDialer5);
	DDX_Control(pDX, IDC_KEY_6, m_ButtonDialer6);
	DDX_Control(pDX, IDC_KEY_7, m_ButtonDialer7);
	DDX_Control(pDX, IDC_KEY_8, m_ButtonDialer8);
	DDX_Control(pDX, IDC_KEY_9, m_ButtonDialer9);
	DDX_Control(pDX, IDC_KEY_0, m_ButtonDialer0);
	DDX_Control(pDX, IDC_KEY_STAR, m_ButtonDialerStar);
	DDX_Control(pDX, IDC_KEY_GRATE, m_ButtonDialerGrate);
	DDX_Control(pDX, IDC_REDIAL, m_ButtonDialerRedial);
	DDX_Control(pDX, IDC_DELETE, m_ButtonDialerDelete);
	DDX_Control(pDX, IDC_KEY_PLUS, m_ButtonDialerPlus);
	DDX_Control(pDX, IDC_CLEAR, m_ButtonDialerClear);
	DDX_Control(pDX, IDC_CALL, m_ButtonCall);
	DDX_Control(pDX, IDC_END, m_ButtonEnd);
	DDX_Control(pDX, IDC_NUMBER, m_ComboNumber);
#ifdef _GLOBAL_VIDEO
	DDX_Control(pDX, IDC_VIDEO_CALL, m_ButtonVideoCall);
#endif
	DDX_Control(pDX, IDC_MESSAGE, m_ButtonDialTone);
}

void Dialer::RebuildShortcutsRestart()
{
	if (accountSettings.enableShortcuts || mainDlg->shortcutsEnabled) {
		if (mainDlg->shortcutsEnabled != accountSettings.enableShortcuts
			||
			mainDlg->shortcutsBottom != accountSettings.shortcutsBottom
			||
			(!accountSettings.shortcutsBottom && mainDlg->shortcutsCount <= 12 && shortcuts.GetCount() > 12)
			|| (accountSettings.shortcutsBottom && shortcuts.GetCount() > mainDlg->shortcutsCount)
			) {
			if (accountSettings.enableShortcuts != mainDlg->shortcutsEnabled || accountSettings.shortcutsBottom != mainDlg->shortcutsBottom) {
				accountSettings.SettingsSave();
			}
			mainDlg->PostMessage(UM_RESTART, 0, 0);
		}
		else {
			RebuildShortcuts();
		}
	}
}
void Dialer::RebuildShortcuts(bool init)
{
	if (!init) {
		POSITION pos = shortcutButtons.GetHeadPosition();
		while (pos) {
			POSITION posKey = pos;
			CButton* button = shortcutButtons.GetNext(pos);
			AutoUnmove(button->m_hWnd);
			delete button;
			shortcutButtons.RemoveAt(posKey);
		};
	}
	if (!mainDlg->shortcutsEnabled) {
		return;
	}
	CRect windowRect;
	if (!init) {
		GetWindowRect(windowRect);
		SetWindowPos(NULL, 0, 0, windowSize.x, windowSize.y, SWP_NOZORDER | SWP_NOMOVE);
	}
	if (shortcuts.GetCount()) {
		CRect shortcutsRect;
		GetWindowRect(shortcutsRect);
		ScreenToClient(shortcutsRect);
		CRect rectLast;
		GetDlgItem(IDC_DIALER_LAST)->GetWindowRect(&rectLast);
		ScreenToClient(rectLast);
		CRect mapRect;
		int rowsMax = 12;
		int buttonHeight;
		int moveFactor;
		int moveFix;
		if (mainDlg->shortcutsBottom) {
			mapRect.left = 4;
			MapDialogRect(&mapRect);
			shortcutsRect.top = rectLast.bottom;
			shortcutsRect.left += mapRect.left;
			shortcutsRect.right -= mapRect.left;
			buttonHeight = MulDiv(25, dpiY, 96);
			moveFactor = 0;
			rowsMax = _GLOBAL_SHORTCUTS_QTY / 2;
		}
		else {
			mapRect.left = 4;
			mapRect.top = 2;
			mapRect.bottom = 1;
			MapDialogRect(&mapRect);
			shortcutsRect.top += mapRect.top;
			shortcutsRect.bottom -= mapRect.bottom;
			shortcutsRect.left = shortcutsRect.right - mainDlg->widthAdd;
			shortcutsRect.right -= mapRect.left;
			moveFactor = 100;
			if (shortcuts.GetCount() > rowsMax) {
				int count = shortcuts.GetCount() / 2 + shortcuts.GetCount() % 2;
				buttonHeight = shortcutsRect.Height() / count;
				shortcutsRect.top = shortcutsRect.top + (shortcutsRect.Height() - buttonHeight * count) / 2;
				moveFactor = moveFactor / count;
			}
			else {
				buttonHeight = shortcutsRect.Height() / shortcuts.GetCount();
				shortcutsRect.top = shortcutsRect.top + (shortcutsRect.Height() - buttonHeight * shortcuts.GetCount()) / 2;
				moveFactor = moveFactor / shortcuts.GetCount();
			}
		}
		for (int i = 0; i < shortcuts.GetCount(); i++) {
			Shortcut shortcut = shortcuts.GetAt(i);
			CButtonSafe* button = new CButtonSafe();
			//CButton* button = new CButton();
			int style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | DS_SETFONT;
			if (!shortcut.number2.IsEmpty()) {
				style |= BS_AUTOCHECKBOX;
			}
			else {
				//style |= BS_CHECKBOX;
			}
			if (mainDlg->shortcutsBottom) {
				CRect buttonRect;
				if (shortcuts.GetCount() > rowsMax) {
					int row = i % 2;
					buttonRect = CRect(shortcutsRect.left + row * shortcutsRect.Width() / 2, shortcutsRect.top, shortcutsRect.right - (1 - row) * shortcutsRect.Width() / 2, shortcutsRect.top + buttonHeight);
					button->Create(Translate(shortcut.label.GetBuffer()), style, buttonRect, this, IDC_SHORTCUT_RANGE + i);
					if (!row) {
						AutoMove(button->m_hWnd, 0, 100, 50, 0);
					}
					else {
						AutoMove(button->m_hWnd, 50, 100, 50, 0);
						shortcutsRect.top += buttonHeight;
					}
				}
				else {
					buttonRect = CRect(shortcutsRect.left, shortcutsRect.top, shortcutsRect.right, shortcutsRect.top + buttonHeight);
					button->Create(Translate(shortcut.label.GetBuffer()), style, buttonRect, this, IDC_SHORTCUT_RANGE + i);
					AutoMove(button->m_hWnd, 0, 100, 100, 0);

					shortcutsRect.top += buttonHeight;
				}
			}
			else {
				CRect buttonRect;
				if (shortcuts.GetCount() > rowsMax) {
					int row = i % 2;
					buttonRect = CRect(shortcutsRect.left + (row * MulDiv(97, dpiY, 96)), shortcutsRect.top, shortcutsRect.right - (1 - row) * MulDiv(97, dpiY, 96), shortcutsRect.top + buttonHeight);
					button->Create(Translate(shortcut.label.GetBuffer()), style, buttonRect, this, IDC_SHORTCUT_RANGE + i);
					AutoMove(button->m_hWnd, 100, i / 2 * moveFactor, 0, moveFactor);
					if (row) {
						shortcutsRect.top += buttonHeight;
					}
				}
				else {
					buttonRect = CRect(shortcutsRect.left, shortcutsRect.top, shortcutsRect.right, shortcutsRect.top + buttonHeight);
					button->Create(Translate(shortcut.label.GetBuffer()), style, buttonRect, this, IDC_SHORTCUT_RANGE + i);
					AutoMove(button->m_hWnd, 100, i * moveFactor, 0, moveFactor);
					shortcutsRect.top += buttonHeight;
				}
			}
			if (shortcut.presence) {
				shortcut.image = MSIP_CONTACT_ICON_DEFAULT;
				button->SetIcon(
					mainDlg->imageListStatus->ExtractIcon(shortcut.image)
				);
			}
			button->SetFont(&m_font_shortcuts);
			shortcutButtons.AddTail(button);
		}
		for (int i = 0; i < shortcuts.GetCount(); i++) {
			Shortcut* shortcut = &shortcuts.GetAt(i);
			if (shortcut->presence) {
				mainDlg->SubsribeNumber(&shortcut->number);
			}
		}
	}
	if (!init) {
		SetWindowPos(NULL, 0, 0, windowRect.Width(), windowRect.Height(), SWP_NOZORDER | SWP_NOMOVE);
	}
}

void Dialer::PresenceSubscribe()
{
	if (shortcuts.GetCount() == shortcutButtons.GetCount()) {
		for (int i = 0; i < shortcuts.GetCount(); i++) {
			Shortcut* shortcut = &shortcuts.GetAt(i);
			if (shortcut->presence) {
				mainDlg->SubsribeNumber(&shortcut->number);
			}
		}
	}
}

void Dialer::PresenceReset()
{
	if (!::IsWindow(this->m_hWnd)) {
		return;
	}
	if (shortcuts.GetCount() == shortcutButtons.GetCount()) {
		for (int i = 0; i < shortcuts.GetCount(); i++) {
			Shortcut* shortcut = &shortcuts.GetAt(i);
			if (shortcut->presence) {
				shortcut->image = MSIP_CONTACT_ICON_DEFAULT;
				shortcut->ringing = false;
				POSITION pos = shortcutButtons.FindIndex(i);
				CButton* button = shortcutButtons.GetAt(pos);
				if (::IsWindow(button->m_hWnd)) {
					button->SetIcon(mainDlg->imageListStatus->ExtractIcon(shortcut->image));
					//button->RedrawWindow();!!
					button->Invalidate();
				}
			}
		}
	}
}

void Dialer::PresenceReceived(CString* buddyNumber, int image, bool ringing, bool fromUsersDirectory)
{
	if (shortcuts.GetCount() == shortcutButtons.GetCount()) {
		bool blink = false;
		for (int i = 0; i < shortcuts.GetCount(); i++) {
			Shortcut* shortcut = &shortcuts.GetAt(i);
			if (shortcut->presence || fromUsersDirectory) {
				CString numberFormated;
				if (fromUsersDirectory) {
					numberFormated = shortcut->number;
				}
				else {
					CString commands;
					numberFormated = FormatNumber(shortcut->number, &commands, true);
				}
				if (*buddyNumber == numberFormated) {
					if (ringing) {
						blink = true;
					}
					shortcut->image = image;
					shortcut->ringing = ringing;
					POSITION pos = shortcutButtons.FindIndex(i);
					CButton* button = shortcutButtons.GetAt(pos);
					if (::IsWindow(button->m_hWnd)) {
						button->SetIcon(mainDlg->imageListStatus->ExtractIcon(shortcut->image));
						//button->RedrawWindow(); causes freezing
						button->Invalidate();
					}
				}
			}
		}
		if (blink) {
			if (!blinkTimer) {
				blinkTimer = SetTimer(IDT_TIMER_SHORTCUTS_BLINK, 500, NULL);
				OnTimerShortcutsBlink();
			}
		}
	}
}

void Dialer::OnTimerShortcutsBlink()
{
	if (!blinkTimer) {
		return;
	}
	bool ringing = false;
	if (shortcuts.GetCount() == shortcutButtons.GetCount()) {
		for (int i = 0; i < shortcuts.GetCount(); i++) {
			Shortcut* shortcut = &shortcuts.GetAt(i);
			if (shortcut->ringing) {
				ringing = true;
				POSITION pos = shortcutButtons.FindIndex(i);
				CButton* button = shortcutButtons.GetAt(pos);
				if (::IsWindow(button->m_hWnd)) {
					button->SetIcon(mainDlg->imageListStatus->ExtractIcon(blinkState ? shortcut->image : MSIP_CONTACT_ICON_BLANK));
					//button->RedrawWindow();// crash on VM ?
					button->Invalidate();
				}
			}
		}
	}
	if (!ringing) {
		blinkTimer = NULL;
		KillTimer(IDT_TIMER_CONTACTS_BLINK);
		blinkState = false;
	}
	else {
		blinkState = !blinkState;
	}
}

BOOL Dialer::OnInitDialog()
{
	CBaseDialog::OnInitDialog();
	if (langPack.rtl) {
		m_SliderCtrlOutput.ModifyStyleEx(0, WS_EX_LAYOUTRTL);
		m_SliderCtrlInput.ModifyStyleEx(0, WS_EX_LAYOUTRTL);
		GetDlgItem(IDC_NUMBER)->ModifyStyleEx(0, WS_EX_LAYOUTRTL);
	}

	CRect windowRect;
	GetWindowRect(windowRect);
	windowSize.x = windowRect.Width();
	windowSize.y = windowRect.Height();

	m_hCursorHand = ::LoadCursor(NULL, IDC_HAND);
	CFont* font = this->GetFont();
	LOGFONT lf;
	font->GetLogFont(&lf);

	lf.lfHeight = -MulDiv(11, dpiY, 96);
	m_font_shortcuts.CreateFontIndirect(&lf);

	RebuildShortcuts(true);

	TranslateDialog(this->m_hWnd);

	int a = MulDiv(100, dpiY, 96);
	if (a < 125) {
		m_ButtonVoicemail.LoadBitmaps(IDB_VMAIL_100, IDB_VMAIL_DOWN_100, IDB_VMAIL_FOCUS_100);
		m_ButtonVoicemailDisabled.LoadBitmaps(IDB_VMAIL_GREY_100, IDB_VMAIL_GREY_DOWN_100, IDB_VMAIL_GREY_FOCUS_100);
	}
	else if (a < 150) {
		m_ButtonVoicemail.LoadBitmaps(IDB_VMAIL_125, IDB_VMAIL_DOWN_125, IDB_VMAIL_FOCUS_125);
		m_ButtonVoicemailDisabled.LoadBitmaps(IDB_VMAIL_GREY_125, IDB_VMAIL_GREY_DOWN_125, IDB_VMAIL_GREY_FOCUS_125);
	}
	else if (a < 175) {
		m_ButtonVoicemail.LoadBitmaps(IDB_VMAIL_150, IDB_VMAIL_DOWN_150, IDB_VMAIL_FOCUS_150);
		m_ButtonVoicemailDisabled.LoadBitmaps(IDB_VMAIL_GREY_150, IDB_VMAIL_GREY_DOWN_150, IDB_VMAIL_GREY_FOCUS_150);
	}
	else  {
		m_ButtonVoicemail.LoadBitmaps(IDB_VMAIL_175, IDB_VMAIL_DOWN_175, IDB_VMAIL_FOCUS_175);
		m_ButtonVoicemailDisabled.LoadBitmaps(IDB_VMAIL_GREY_175, IDB_VMAIL_GREY_DOWN_175, IDB_VMAIL_GREY_FOCUS_175);
	}
	m_ButtonVoicemail.SizeToContent();
	m_ButtonVoicemailDisabled.SizeToContent();

	if (m_ToolTip.Create(this)) {
		m_ToolTip.AddTool(&m_ButtonDialerRedial, Translate(_T("Redial")));
		m_ToolTip.AddTool(&m_ButtonDialerDelete, Translate(_T("Backspace")));
		m_ToolTip.AddTool(&m_ButtonDialerClear, Translate(_T("Clear")));
		CString str = Translate(_T("Voicemail Number"));
		m_ToolTip.AddTool(&m_ButtonVoicemail, str);
		m_ToolTip.AddTool(&m_ButtonVoicemailDisabled, str);
		m_ToolTip.Activate(TRUE);
	}

	RebuildButtons(true);
	AutoMove(IDC_NUMBER, 0, 0, 100, 0);
	AutoMove(IDC_DIALER_DTMF, 100, 0, 0, 0);

	int height = 17;
	int height4 = height * 4;
	int height2 = height * 2;
	int height3 = height * 3;
	AutoMove(IDC_KEY_1, 0, 0, 33, height);
	AutoMove(IDC_KEY_4, 0, height, 33, height);
	AutoMove(IDC_KEY_7, 0, height2, 33, height);
	AutoMove(IDC_KEY_STAR, 0, height3, 33, height);
	AutoMove(IDC_REDIAL, 0, height4, 33, height);
	AutoMove(IDC_DELETE, 0, height4, 33, 17);

	AutoMove(IDC_KEY_2, 33, 0, 34, height);
	AutoMove(IDC_KEY_5, 33, height, 34, height);
	AutoMove(IDC_KEY_8, 33, height2, 34, height);
	AutoMove(IDC_KEY_0, 33, height3, 34, height);
	AutoMove(IDC_KEY_PLUS, 33, height4, 34, height);
	AutoMove(IDC_KEY_3, 67, 0, 33, height);
	AutoMove(IDC_KEY_6, 67, height, 33, height);
	AutoMove(IDC_KEY_9, 67, height2, 33, height);
	AutoMove(IDC_KEY_GRATE, 67, height3, 33, height);
	AutoMove(IDC_CLEAR, 67, height4, 33, height);

	// Chat and video are not exposed in this fork; the hero row is two equal actions.
#ifdef _GLOBAL_VIDEO
	GetDlgItem(IDC_VIDEO_CALL)->ShowWindow(SW_HIDE);
#endif
	CRect heroRect;
	CRect dialerClient;
	m_ButtonCall.GetWindowRect(&heroRect);
	ScreenToClient(&heroRect);
	GetClientRect(&dialerClient);
	heroRect.left = MulDiv(4, dpiY, 96);
	heroRect.right = dialerClient.right - MulDiv(4, dpiY, 96);
	int heroMiddle = heroRect.left + heroRect.Width() / 2;
	m_ButtonDialTone.SetWindowPos(NULL, heroRect.left, heroRect.top,
		heroMiddle - heroRect.left, heroRect.Height(), SWP_NOACTIVATE | SWP_NOZORDER);
	m_ButtonCall.SetWindowPos(NULL, heroMiddle, heroRect.top,
		heroRect.right - heroMiddle, heroRect.Height(), SWP_NOACTIVATE | SWP_NOZORDER);
	AutoMove(IDC_MESSAGE, 0, 85, 50, 15);
	AutoMove(IDC_CALL, 50, 85, 50, 15);

	AutoMove(IDC_END, 14, 85, 72, 15);
	AutoMove(IDC_HOLD, 0, 85, 14, 15);
	AutoMove(IDC_TRANSFER, 86, 85, 14, 15);

	AutoMove(IDC_BUTTON_MUTE_OUTPUT, 0, 100, 0, 0);
	AutoMove(IDC_BUTTON_MUTE_INPUT, 0, 100, 0, 0);
	AutoMove(IDC_VOLUME_INPUT, 0, 100, 100, 0);
	AutoMove(IDC_VOLUME_OUTPUT, 0, 100, 100, 0);
	AutoMove(IDC_BUTTON_MINUS_INPUT, 0, 100, 0, 0);
	AutoMove(IDC_BUTTON_MINUS_OUTPUT, 0, 100, 0, 0);
	AutoMove(IDC_BUTTON_PLUS_INPUT, 100, 100, 0, 0);
	AutoMove(IDC_BUTTON_PLUS_OUTPUT, 100, 100, 0, 0);
	AutoMove(IDC_DIALER_VOICEMAIL, 100, 100, 0, 0);
	AutoMove(IDC_DIALER_VOICEMAIL_DISABLED, 100, 100, 0, 0);

	DialedLoad();

	//--
	lf.lfHeight = -MulDiv(13, dpiY, 96);
	m_font_call.CreateFontIndirect(&lf);
	//--
	lf.lfHeight = -MulDiv(19, dpiY, 96);
	m_font.CreateFontIndirect(&lf);
	//--
	m_font_number.CreateFontIndirect(&lf);
	//--
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	combobox->SetWindowPos(NULL, 0, 0, combobox->GetDroppedWidth(), MulDiv(400, dpiY, 96), SWP_NOZORDER | SWP_NOMOVE);
	combobox->SetFont(&m_font_number);
	GetDlgItem(IDC_KEY_1)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_2)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_3)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_4)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_5)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_6)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_7)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_8)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_9)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_0)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_STAR)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_GRATE)->SetFont(&m_font);
	GetDlgItem(IDC_KEY_PLUS)->SetFont(&m_font);
	GetDlgItem(IDC_CLEAR)->SetFont(&m_font_call);
	GetDlgItem(IDC_REDIAL)->SetFont(&m_font_call);
	GetDlgItem(IDC_DELETE)->SetFont(&m_font);

	m_ButtonCall.m_FaceColor = _GLOBAL_DIALER_CALL_COLOR;
	m_ButtonCall.m_TextColor = RGB(255, 255, 255);
	m_ButtonDialTone.m_FaceColor = _GLOBAL_DIALER_CALL_COLOR;
	m_ButtonDialTone.m_TextColor = RGB(255, 255, 255);
	m_ButtonEnd.m_FaceColor = _GLOBAL_DIALER_END_COLOR;
	m_ButtonEnd.m_TextColor = RGB(255, 255, 255);
	m_ButtonEnd.EnableWindow(m_ButtonEnd.IsWindowEnabled());
	m_ButtonCall.SetFont(&m_font_call);
	m_ButtonDialTone.SetFont(&m_font_call);
	m_ButtonDialTone.ModifyStyle(BS_ICON, BS_PUSHBUTTON);
	m_ButtonDialTone.SetWindowText(Translate(_T("Dial-Tone")));
	m_ButtonDialTone.EnableWindow(TRUE);
	m_ButtonEnd.SetFont(&m_font_call);
	m_ButtonEnd.ShowWindow(SW_HIDE);
	m_ButtonEnd.EnableWindow(TRUE);

	muteOutput = FALSE;
	muteInput = FALSE;

	m_SliderCtrlOutput.SetRange(0, 100);
	m_SliderCtrlOutput.SetPos(accountSettings.volumeOutput);

	m_SliderCtrlInput.SetRange(0, 100);
	m_SliderCtrlInput.SetPos(accountSettings.volumeInput);

	m_hIconMuteOutput = LoadImageIcon(IDI_MUTE_OUTPUT, 16, 16);
	((CButton*)GetDlgItem(IDC_BUTTON_MUTE_OUTPUT))->SetIcon(m_hIconMuteOutput);
	m_hIconMutedOutput = LoadImageIcon(IDI_MUTED_OUTPUT, 16, 16);

	m_hIconMuteInput = LoadImageIcon(IDI_MUTE_INPUT, 16, 16);
	((CButton*)GetDlgItem(IDC_BUTTON_MUTE_INPUT))->SetIcon(m_hIconMuteInput);
	m_hIconMutedInput = LoadImageIcon(IDI_MUTED_INPUT, 16, 16);

	m_hIconHold = LoadImageIcon(IDI_HOLD, 16, 16);
	m_hIconResume = LoadImageIcon(IDI_RESUME, 16, 16);
	((CButton*)GetDlgItem(IDC_HOLD))->SetIcon(m_hIconHold);
	m_hIconTransfer = LoadImageIcon(IDI_TRANSFER, 16, 16);
	((CButton*)GetDlgItem(IDC_TRANSFER))->SetIcon(m_hIconTransfer);
	UpdateCallButton();

return TRUE;
}

int Dialer::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (mainDlg->widthAdd || mainDlg->heightAdd) {
		SetWindowPos(NULL, 0, 0, lpCreateStruct->cx + mainDlg->widthAdd, lpCreateStruct->cy + mainDlg->heightAdd, SWP_NOMOVE | SWP_NOZORDER);
	}
	if (langPack.rtl) {
		ModifyStyleEx(WS_EX_LAYOUTRTL, 0);
	}
	return CBaseDialog::OnCreate(lpCreateStruct);
}

void Dialer::OnDestroy()
{
	mainDlg->StopDialTone();
	KillTimer(IDT_TIMER_VU_METER);
	CBaseDialog::OnDestroy();
}

void Dialer::PostNcDestroy()
{
	CBaseDialog::PostNcDestroy();
	delete this;
}

BEGIN_MESSAGE_MAP(Dialer, CBaseDialog)
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, OnBnClickedCancel)
	ON_WM_SETCURSOR()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_DIALER_DND, &Dialer::OnBnClickedDND)
	ON_BN_CLICKED(IDC_DIALER_FWD, &Dialer::OnBnClickedFWD)
	ON_EN_CHANGE(IDC_DIALER_CFW_DESTINATION, &Dialer::OnCfwDestinationChange)
	ON_BN_CLICKED(IDC_DIALER_AA, &Dialer::OnBnClickedAA)
	ON_BN_CLICKED(IDC_DIALER_AC, &Dialer::OnBnClickedAC)
	ON_BN_CLICKED(IDC_DIALER_CONF, &Dialer::OnBnClickedConf)
	ON_BN_CLICKED(IDC_DIALER_REC, &Dialer::OnBnClickedRec)
	ON_BN_CLICKED(IDC_DIALER_VOICEMAIL, OnBnClickedVoicemail)
	ON_BN_CLICKED(IDC_DIALER_VOICEMAIL_DISABLED, OnBnClickedVoicemail)
	ON_BN_CLICKED(IDC_BUTTON_PLUS_INPUT, &Dialer::OnBnClickedPlusInput)
	ON_BN_CLICKED(IDC_BUTTON_MINUS_INPUT, &Dialer::OnBnClickedMinusInput)
	ON_BN_CLICKED(IDC_BUTTON_PLUS_OUTPUT, &Dialer::OnBnClickedPlusOutput)
	ON_BN_CLICKED(IDC_BUTTON_MINUS_OUTPUT, &Dialer::OnBnClickedMinusOutput)
	ON_BN_CLICKED(IDC_BUTTON_MUTE_OUTPUT, &Dialer::OnBnClickedMuteOutput)
	ON_BN_CLICKED(IDC_BUTTON_MUTE_INPUT, &Dialer::OnBnClickedMuteInput)
	ON_COMMAND_RANGE(IDC_SHORTCUT_RANGE, IDC_SHORTCUT_RANGE + 24, &Dialer::OnBnClickedShortcut)
	ON_WM_RBUTTONUP()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()

	ON_BN_CLICKED(IDC_CALL, OnBnClickedCall)
	ON_BN_CLICKED(IDC_MESSAGE, OnBnClickedDialTone)
	ON_BN_CLICKED(IDC_DIALER_DTMF, OnBnClickedDTMF)
	ON_BN_CLICKED(IDC_HOLD, OnBnClickedHold)
	ON_BN_CLICKED(IDC_TRANSFER, OnBnClickedTransfer)
	ON_BN_CLICKED(IDC_END, OnBnClickedEnd)
	ON_CBN_EDITCHANGE(IDC_NUMBER, &Dialer::OnCbnEditchangeComboAddr)
	ON_CBN_SELCHANGE(IDC_NUMBER, &Dialer::OnCbnSelchangeComboAddr)

	ON_BN_CLICKED(IDC_KEY_1, &Dialer::OnBnClickedKey1)
	ON_BN_CLICKED(IDC_KEY_2, &Dialer::OnBnClickedKey2)
	ON_BN_CLICKED(IDC_KEY_3, &Dialer::OnBnClickedKey3)
	ON_BN_CLICKED(IDC_KEY_4, &Dialer::OnBnClickedKey4)
	ON_BN_CLICKED(IDC_KEY_5, &Dialer::OnBnClickedKey5)
	ON_BN_CLICKED(IDC_KEY_6, &Dialer::OnBnClickedKey6)
	ON_BN_CLICKED(IDC_KEY_7, &Dialer::OnBnClickedKey7)
	ON_BN_CLICKED(IDC_KEY_8, &Dialer::OnBnClickedKey8)
	ON_BN_CLICKED(IDC_KEY_9, &Dialer::OnBnClickedKey9)
	ON_BN_CLICKED(IDC_KEY_STAR, &Dialer::OnBnClickedKeyStar)
	ON_BN_CLICKED(IDC_KEY_0, &Dialer::OnBnClickedKey0)
	ON_BN_CLICKED(IDC_KEY_GRATE, &Dialer::OnBnClickedKeyGrate)
	ON_BN_CLICKED(IDC_REDIAL, &Dialer::OnBnClickedRedial)
	ON_BN_CLICKED(IDC_DELETE, &Dialer::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_KEY_PLUS, &Dialer::OnBnClickedKeyPlus)
	ON_BN_CLICKED(IDC_CLEAR, &Dialer::OnBnClickedClear)
	ON_BN_CLICKED(IDC_DIALER_ACCOUNT_SWITCH, &Dialer::OnBnClickedAccountSwitch)
	ON_WM_HSCROLL()
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
END_MESSAGE_MAP()

void Dialer::SetName(CString str)
{
}

void Dialer::UpdateVoicemailButton(bool hasMail)
{
	if (m_hasVoicemail != hasMail) {
		m_hasVoicemail = hasMail;
	}
	if (m_isButtonVoicemailVisible) {
		if (hasMail) {
			m_ButtonVoicemailDisabled.ShowWindow(SW_HIDE);
			m_ButtonVoicemail.ShowWindow(SW_SHOW);
		}
		else {
			m_ButtonVoicemail.ShowWindow(SW_HIDE);
			m_ButtonVoicemailDisabled.ShowWindow(SW_SHOW);
		}
	}
	else {
		m_ButtonVoicemail.ShowWindow(SW_HIDE);
		m_ButtonVoicemailDisabled.ShowWindow(SW_HIDE);
	}
}

void Dialer::RebuildButtons(bool init)
{
	m_rebuildingButtons = true;
	CloseAccountSidecar();
	if (IsChild(&m_AccountIdentity)) {
		m_AccountIdentity.DestroyWindow();
	}
	if (IsChild(&m_AccountSwitch)) {
		m_AccountSwitch.DestroyWindow();
	}

	if (accountSettings.accountId && !accountSettings.account.voicemailNumber.IsEmpty()) {
		m_isButtonVoicemailVisible = true;
		UpdateVoicemailButton(m_hasVoicemail);
	}
	else {
		m_isButtonVoicemailVisible = false;
		UpdateVoicemailButton(m_hasVoicemail);
	}
	if (IsChild(&m_ButtonDND)) {
		if (m_ToolTip) {
			m_ToolTip.DelTool(&m_ButtonDND);
		}
		m_ButtonDND.DestroyWindow();
	}
	if (IsChild(&m_ButtonFWD)) {
		if (m_ToolTip) {
			m_ToolTip.DelTool(&m_ButtonFWD);
		}
		m_ButtonFWD.DestroyWindow();
	}
	if (IsChild(&m_CfwDestination)) {
		m_CfwDestination.DestroyWindow();
	}
	if (IsChild(&m_ButtonAA)) {
		if (m_ToolTip) {
			m_ToolTip.DelTool(&m_ButtonAA);
		}
		m_ButtonAA.DestroyWindow();
	}
	if (IsChild(&m_ButtonAC)) {
		if (m_ToolTip) {
			m_ToolTip.DelTool(&m_ButtonAC);
		}
		m_ButtonAC.DestroyWindow();
	}
	if (IsChild(&m_ButtonConf)) {
		if (m_ToolTip) {
			m_ToolTip.DelTool(&m_ButtonConf);
		}
		m_ButtonConf.DestroyWindow();
	}
	if (IsChild(&m_ButtonRec)) {
		if (m_ToolTip) {
			m_ToolTip.DelTool(&m_ButtonRec);
		}
		m_ButtonRec.DestroyWindow();
	}
	bool addDND = accountSettings.denyIncoming == _T("button");
	bool addAccountControls = true;
	if (addDND || addAccountControls) {
		CRect windowRect;
		if (!init) {
			GetWindowRect(windowRect);
			SetWindowPos(NULL, 0, 0, windowSize.x, windowSize.y, SWP_NOZORDER | SWP_NOMOVE);
		}

		CRect rect;
		m_ButtonVoicemail.GetWindowRect(&rect);
		ScreenToClient(rect);
		rect.top -= 1;
		rect.bottom += 1;
		//rect.left -= 1;
		//rect.right += 2;

		CRect mapRect;
		mapRect.top = 5;
		mapRect.bottom = 2;
		MapDialogRect(&mapRect);
		int stepPx = mapRect.bottom + rect.Width();

		if (m_isButtonVoicemailVisible) {
			rect.left -= stepPx;
			rect.right -= stepPx;
		}
		m_ButtonFWD.Create(_T("CFW"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_PUSHLIKE,
			rect, this, IDC_DIALER_FWD);
		m_ButtonFWD.SetFont(GetFont());
		AutoMove(m_ButtonFWD.m_hWnd, 100, 100, 0, 0);
		if (m_ToolTip) m_ToolTip.AddTool(&m_ButtonFWD, Translate(_T("Call Forwarding")));
		rect.left -= stepPx;
		rect.right -= stepPx;

		int destinationWidth = stepPx * 3 - mapRect.bottom;
		CRect destinationRect(rect.right - destinationWidth, rect.top, rect.right, rect.bottom);
		m_CfwDestination.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
			destinationRect, this, IDC_DIALER_CFW_DESTINATION);
		m_CfwDestination.SetFont(GetFont());
		m_CfwDestination.SetPlaceholder(_T("Forward Destination"));
		m_CfwDestination.SetWindowText(accountSettings.forwardingNumber);
		AutoMove(m_CfwDestination.m_hWnd, 100, 100, 0, 0);
		rect.left = destinationRect.left - stepPx;
		rect.right = destinationRect.left - mapRect.bottom;
		if (addDND) {
			m_ButtonDND.Create(Translate(_T("DND")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_CHECKBOX | BS_PUSHLIKE, rect, this, IDC_DIALER_DND);
			m_ButtonDND.SetFont(GetFont());
			m_ButtonDND.SetCheck(accountSettings.DND ? BST_CHECKED : BST_UNCHECKED);
			AutoMove(m_ButtonDND.m_hWnd, 100, 100, 0, 0);
			if (m_ToolTip) {
				m_ToolTip.AddTool(&m_ButtonDND, Translate(_T("Do Not Disturb")));
			}
			rect.left -= stepPx;
			rect.right -= stepPx;
		}
		CRect identityRect = rect;
		identityRect.left = MulDiv(4, dpiY, 96);
		int switchWidth = MulDiv(24, dpiY, 96);
		CRect switchRect = identityRect;
		switchRect.right = switchRect.left + switchWidth;
		m_AccountSwitch.Create(_T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_ICON, switchRect, this, IDC_DIALER_ACCOUNT_SWITCH);
		m_AccountSwitch.SetFont(GetFont());
		m_AccountSwitch.SetButtonIcon(LoadImageIcon(IDI_CONTACT, 16, 16));
		if (m_ToolTip) {
			m_ToolTip.AddTool(&m_AccountSwitch, Translate(_T("Switch account")));
		}
		identityRect.left = switchRect.right + MulDiv(4, dpiY, 96);
		m_AccountIdentity.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, identityRect, this, IDC_DIALER_ACCOUNT_IDENTITY);
		m_AccountIdentity.SetFont(GetFont());
		AutoMove(m_AccountIdentity.m_hWnd, 100, 100, 0, 0);
		UpdateAccountIdentity();

		SetDarkMode(accountSettings.darkMode);
		m_rebuildingButtons = false;
		UpdateCfwState(false);
		if (!init) {
			SetWindowPos(NULL, 0, 0, windowRect.Width(), windowRect.Height(), SWP_NOZORDER | SWP_NOMOVE);
		}
	}
	else {
		m_rebuildingButtons = false;
	}
}

int Dialer::GetVisibleControlsBottom() const
{
	CWnd* parent = GetParent();
	if (!parent || !::IsWindow(m_hWnd)) {
		return 0;
	}
	int bottom = 0;
	for (HWND child = ::GetWindow(m_hWnd, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
		if (!::IsWindowVisible(child)) {
			continue;
		}
		CRect rect;
		::GetWindowRect(child, &rect);
		parent->ScreenToClient(&rect);
		bottom = max(bottom, (int)rect.bottom);
	}
	return bottom;
}

void Dialer::UpdateAccountIdentity()
{
	if (!IsChild(&m_AccountIdentity)) {
		return;
	}
	CString identity = accountSettings.account.username;
	m_AccountIdentity.SetWindowText(identity);
}

void Dialer::SetDarkMode(bool enabled)
{
	m_ButtonCall.m_FaceColor = enabled ? DarkPalette::Surface() : _GLOBAL_DIALER_CALL_COLOR;
	m_ButtonCall.m_TextColor = RGB(255, 255, 255);
	m_ButtonDialTone.m_FaceColor = m_dialToneSessionActive ? _GLOBAL_DIALER_END_COLOR
		: (enabled ? DarkPalette::Surface() : _GLOBAL_DIALER_CALL_COLOR);
	m_ButtonDialTone.m_TextColor = RGB(255, 255, 255);
	m_ButtonEnd.m_FaceColor = enabled ? DarkPalette::Surface() : _GLOBAL_DIALER_END_COLOR;
	m_ButtonEnd.m_TextColor = RGB(255, 255, 255);
	// CButtonEx only pushes its colours into CMFCButton from EnableWindow.
	m_ButtonCall.EnableWindow(m_ButtonCall.IsWindowEnabled());
	m_ButtonDialTone.EnableWindow(m_ButtonDialTone.IsWindowEnabled());
	m_ButtonEnd.EnableWindow(m_ButtonEnd.IsWindowEnabled());
	m_ButtonDND.SetDarkMode(enabled);
	m_ButtonFWD.SetDarkMode(enabled);
	m_CfwDestination.SetDarkMode(enabled);
	m_ButtonAA.SetDarkMode(enabled);
	m_ButtonAC.SetDarkMode(enabled);
	m_ButtonRec.SetDarkMode(enabled);
	m_ButtonConf.SetDarkMode(enabled);
	m_ComboNumber.SetDarkMode(enabled);
	LPCWSTR theme = enabled ? L"DarkMode_Explorer" : NULL;
	m_AccountSwitch.SetDarkMode(enabled);
	SetWindowTheme(m_AccountSwitch.m_hWnd, theme, NULL);
	SetWindowTheme(GetDlgItem(IDC_BUTTON_MUTE_OUTPUT)->m_hWnd, theme, NULL);
	SetWindowTheme(GetDlgItem(IDC_BUTTON_MUTE_INPUT)->m_hWnd, theme, NULL);
	SetWindowTheme(m_SliderCtrlOutput.m_hWnd, theme, NULL);
	SetWindowTheme(m_SliderCtrlInput.m_hWnd, theme, NULL);
	GetDlgItem(IDC_BUTTON_MUTE_OUTPUT)->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	GetDlgItem(IDC_BUTTON_MUTE_INPUT)->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	m_SliderCtrlOutput.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	m_SliderCtrlInput.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	if (m_accountSidecar) {
		m_accountSidecar->SetDarkMode(enabled);
	}
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void Dialer::OpenAccountSidecar()
{
	if (!m_accountSidecar) {
		m_accountSidecar = new CAccountSidecar(this);
		m_accountSidecar->CreateEx(WS_EX_TOOLWINDOW, AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW), NULL, WS_POPUP | WS_BORDER, CRect(0, 0, 0, 0), GetParent(), 0);
	}
	m_accountSidecar->ShowAccounts();
}

void Dialer::CloseAccountSidecar()
{
	if (m_accountSidecar) {
		CAccountSidecar* sidecar = m_accountSidecar;
		m_accountSidecar = NULL;
		if (::IsWindow(sidecar->m_hWnd)) {
			sidecar->DestroyWindow();
		}
		delete sidecar;
	}
}

void Dialer::RepositionAccountSidecar()
{
	if (m_accountSidecar && ::IsWindow(m_accountSidecar->m_hWnd)) {
		m_accountSidecar->ShowAccounts();
	}
}

void Dialer::OnTimer(UINT_PTR TimerVal)
{
	if (TimerVal == IDT_TIMER_VU_METER) {
		TimerVuMeter();
	}
	if (TimerVal == IDT_TIMER_DTMF) {
		KillTimer(IDT_TIMER_DTMF);
		DTMF(digitsDTMFDelayed);
		digitsDTMFDelayed.Empty();
	}
	if (TimerVal == IDT_TIMER_SHORTCUTS_BLINK) {
		OnTimerShortcutsBlink();
	}
}

BOOL Dialer::PreTranslateMessage(MSG* pMsg)
{
	if (m_ToolTip) {
		m_ToolTip.RelayEvent(pMsg);
	}

	BOOL catched = FALSE;
	BOOL isEdit = FALSE;
	CEdit* edit = NULL;
	if (pMsg->message == WM_CHAR || (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE)) {
		CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
		edit = (CEdit*)FindWindowEx(combobox->m_hWnd, NULL, _T("EDIT"), NULL);
		isEdit = !edit || edit == GetFocus();
	}
	if (pMsg->message == WM_CHAR)
	{
		if (pMsg->wParam == 48)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_0));
				OnBnClickedKey0();
				catched = TRUE;
			}
			else {
				DTMF(_T("0"));
			}
		}
		else if (pMsg->wParam == 49)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_1));
				OnBnClickedKey1();
				catched = TRUE;
			}
			else {
				DTMF(_T("1"));
			}
		}
		else if (pMsg->wParam == 50)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_2));
				OnBnClickedKey2();
				catched = TRUE;
			}
			else {
				DTMF(_T("2"));
			}
		}
		else if (pMsg->wParam == 51)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_3));
				OnBnClickedKey3();
				catched = TRUE;
			}
			else {
				DTMF(_T("3"));
			}
		}
		else if (pMsg->wParam == 52)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_4));
				OnBnClickedKey4();
				catched = TRUE;
			}
			else {
				DTMF(_T("4"));
			}
		}
		else if (pMsg->wParam == 53)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_5));
				OnBnClickedKey5();
				catched = TRUE;
			}
			else {
				DTMF(_T("5"));
			}
		}
		else if (pMsg->wParam == 54)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_6));
				OnBnClickedKey6();
				catched = TRUE;
			}
			else {
				DTMF(_T("6"));
			}
		}
		else if (pMsg->wParam == 55)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_7));
				OnBnClickedKey7();
				catched = TRUE;
			}
			else {
				DTMF(_T("7"));
			}
		}
		else if (pMsg->wParam == 56)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_8));
				OnBnClickedKey8();
				catched = TRUE;
			}
			else {
				DTMF(_T("8"));
			}
		}
		else if (pMsg->wParam == 57)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_9));
				OnBnClickedKey9();
				catched = TRUE;
			}
			else {
				DTMF(_T("9"));
			}
		}
		else if (pMsg->wParam == 35 || pMsg->wParam == 47)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_GRATE));
				OnBnClickedKeyGrate();
				catched = TRUE;
			}
			else {
				DTMF(_T("#"));
			}
		}
		else if (pMsg->wParam == 42)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_STAR));
				OnBnClickedKeyStar();
				catched = TRUE;
			}
			else {
				DTMF(_T("*"));
			}
		}
		else if (pMsg->wParam == 43)
		{
			if (!isEdit) {
				GotoDlgCtrl(GetDlgItem(IDC_KEY_PLUS));
				OnBnClickedKeyPlus();
				catched = TRUE;
			}
		}
		else if (pMsg->wParam == 8 || pMsg->wParam == 45)
		{
			if (!isEdit)
			{
				GotoDlgCtrl(GetDlgItem(IDC_DELETE));
				OnBnClickedDelete();
				catched = TRUE;
			}
		}
		else if (pMsg->wParam == 46)
		{
			if (!isEdit)
			{
				Input(_T("."), TRUE);
				catched = TRUE;
			}
		}
	}
	else if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == VK_ESCAPE) {
			WINDOWINFO wndInfo;
			m_ButtonEnd.GetWindowInfo(&wndInfo);
			bool isEndVisisble = wndInfo.dwStyle & WS_VISIBLE;
			if (accountSettings.singleMode && isEndVisisble) {
				OnBnClickedEnd();
				catched = TRUE;
			}
			else {
				if (!isEdit) {
					GotoDlgCtrl(GetDlgItem(IDC_NUMBER));
					catched = TRUE;
				}
				if (edit) {
					CString str;
					edit->GetWindowText(str);
					if (!str.IsEmpty()) {
						Clear();
						catched = TRUE;
					}
				}
			}
		}
	}
	if (!catched)
	{
		return CBaseDialog::PreTranslateMessage(pMsg);
	}
	else {
		return TRUE;
	}
}

HBRUSH Dialer::OnCtlColor(CDC* pDC, CWnd *pWnd, UINT nCtlColor)
{
	if (accountSettings.darkMode && (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_EDIT)) {
		static CBrush darkPageBrush(DarkPalette::Window());
		static CBrush darkInputBrush(DarkPalette::Input());
		pDC->SetTextColor(DarkPalette::Text());
		if (nCtlColor == CTLCOLOR_EDIT) {
			pDC->SetBkColor(DarkPalette::Input());
			return darkInputBrush;
		}
		pDC->SetBkColor(DarkPalette::Window());
		return darkPageBrush;
	}
	HBRUSH br = CBaseDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	if (pWnd == &m_ButtonMinusInput
		|| pWnd == &m_ButtonMinusOutput
		|| pWnd == &m_ButtonPlusInput
		|| pWnd == &m_ButtonPlusOutput
		) {
		pDC->SetTextColor(RGB(127, 127, 127));
	}
	return br;
}

void Dialer::OnBnClickedOk()
{
	WINDOWINFO wndInfo;
	m_ButtonEnd.GetWindowInfo(&wndInfo);
	bool isEndVisisble = wndInfo.dwStyle & WS_VISIBLE;
	if (accountSettings.singleMode && isEndVisisble) {
		if (m_ButtonEnd.IsWindowEnabled()) {
			OnBnClickedEnd();
		}
	}
	else {
		OnBnClickedCall();
	}
}

void Dialer::OnBnClickedCancel()
{
	mainDlg->ShowWindow(SW_HIDE);
}

void Dialer::DTMFDelayed(CString digits, int delay)
{
	digitsDTMFDelayed = digits;
	SetTimer(IDT_TIMER_DTMF, delay, NULL);
}

void Dialer::DTMF(CString digits, bool force)
{
	bool delayed = false;
	if (digits.Right(1) == _T("?")) {
		digits = digits.Left(digits.GetLength() - 1);
		delayed = true;
	}
	pjsua_call_id call_id = PJSUA_INVALID_ID;
	MessagesContact*  messagesContact = mainDlg->messagesDlg->GetMessageContact();
	if (messagesContact && messagesContact->callId != -1) {
		call_id = messagesContact->callId;
		if (delayed) {
			SetDTMF(digits);
		}
	}
	if (!delayed) {
		WINDOWINFO wndInfo;
		GetDlgItem(IDC_DIALER_DTMF)->GetWindowInfo(&wndInfo);
		bool isButtonVisisble = wndInfo.dwStyle & WS_VISIBLE;
		if (isButtonVisisble && !force) {
			return;
		}
		msip_call_dial_dtmf(call_id, digits);
	}
}

void Dialer::SetDTMF(CString digits)
{
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	CRect rect;
	combobox->GetWindowRect(rect);
	
	CRect mapRect;
	mapRect.bottom = 45;
	MapDialogRect(&mapRect);

	WINDOWINFO wndInfo;
	GetDlgItem(IDC_DIALER_DTMF)->GetWindowInfo(&wndInfo);
	bool isButtonVisisble = wndInfo.dwStyle & WS_VISIBLE;

	if (!digits.IsEmpty()) {
		SetNumber(digits);
		if (!isButtonVisisble) {
			GetDlgItem(IDC_DIALER_DTMF)->ShowWindow(SW_SHOW);
			combobox->SetWindowPos(NULL, 0, 0, rect.Width() - mapRect.bottom, rect.Height(), SWP_NOZORDER | SWP_NOMOVE);
		}
	}
	else {
		CString old;
		combobox->GetWindowText(old);
		if (!old.IsEmpty()) {
			SetNumber(_T(""));
		}
		if (isButtonVisisble) {
			GetDlgItem(IDC_DIALER_DTMF)->ShowWindow(SW_HIDE);
			combobox->SetWindowPos(NULL, 0, 0, rect.Width() + mapRect.bottom, rect.Height(), SWP_NOZORDER | SWP_NOMOVE);
		}
	}
}

void Dialer::Input(CString digits, BOOL disableDTMF)
{
	if (!digits.IsEmpty()) {
		mainDlg->DialToneTargetEntered();
	}
	if (!disableDTMF) {
		DTMF(digits);
	}
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	CEdit* edit = (CEdit*)FindWindowEx(combobox->m_hWnd, NULL, _T("EDIT"), NULL);
	if (edit) {
		int nLength = edit->GetWindowTextLength();
		edit->SetSel(nLength, nLength);
		edit->ReplaceSel(digits);
	}
}

void Dialer::DialedClear()
{
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	combobox->ResetContent();
	combobox->Clear();
}
void Dialer::DialedLoad()
{
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	CString key;
	CString val;
	LPTSTR ptr = val.GetBuffer(255);
	int i = 0;
	while (TRUE) {
		key.Format(_T("%d"), i);
		if (GetPrivateProfileString(_T("Dialed"), key, NULL, ptr, 256, accountSettings.iniFile)) {
			combobox->AddString(ptr);
		}
		else {
			break;
		}
		i++;
	}
}

void Dialer::DialedSave(CComboBox *combobox)
{
	CString key;
	CString val;
	WritePrivateProfileString(_T("Dialed"), NULL, NULL, accountSettings.iniFile);
	for (int i = 0; i < combobox->GetCount(); i++)
	{
		int n = combobox->GetLBTextLen(i);
		combobox->GetLBText(i, val.GetBuffer(n));
		val.ReleaseBuffer();

		key.Format(_T("%d"), i);
		WritePrivateProfileString(_T("Dialed"), key, val, accountSettings.iniFile);
	}
}

void Dialer::DialedAdd(CString number)
{
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	int pos = combobox->FindStringExact(-1, number);
	if (pos == CB_ERR || pos > 0) {
		if (pos > 0) {
			combobox->DeleteString(pos);
		}
		else if (combobox->GetCount() >= 10)
		{
			combobox->DeleteString(combobox->GetCount() - 1);
		}
		combobox->InsertString(0, number);
		combobox->SetCurSel(0);
	}
	DialedSave(combobox);
}

void Dialer::SetNumber(CString  number, int callsCount)
{
	if (!number.IsEmpty()) {
		mainDlg->DialToneTargetEntered(true);
	}
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	CString old;
	combobox->GetWindowText(old);
	if (old.IsEmpty() || number.Find(old) != 0) {
		combobox->SetWindowText(number);
	}
	UpdateCallButton(0, callsCount);
	delayedDTMF = false;
}

void Dialer::UpdateCallButton(BOOL forse, int callsCount)
{
	int len;
	if (!forse) {
		CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
		CString target;
		combobox->GetWindowText(target);
		target.Trim();
		len = target.GetLength();
	}
	else {
		len = 1;
	}
	if (callsCount == -1) {
		callsCount = mainDlg->messagesDlg->GetCallsCount();
	}
	bool state = false;
	if (accountSettings.singleMode) {
		bool isEndVisisble = false;
		WINDOWINFO wndInfo;
		m_ButtonEnd.GetWindowInfo(&wndInfo);
		isEndVisisble = wndInfo.dwStyle & WS_VISIBLE;
		if (callsCount) {
			if (!isEndVisisble) {
				m_ButtonCall.ShowWindow(SW_HIDE);
#ifdef _GLOBAL_VIDEO
				GetDlgItem(IDC_VIDEO_CALL)->ShowWindow(SW_HIDE);
#endif
				GetDlgItem(IDC_MESSAGE)->ShowWindow(SW_HIDE);
				GetDlgItem(IDC_HOLD)->ShowWindow(SW_SHOW);
				GetDlgItem(IDC_TRANSFER)->ShowWindow(SW_SHOW);
				m_ButtonEnd.ShowWindow(SW_SHOW);
				GotoDlgCtrl(GetDlgItem(IDC_END));
			}
		}
		else {
			if (isEndVisisble) {
				GetDlgItem(IDC_HOLD)->ShowWindow(SW_HIDE);
				GetDlgItem(IDC_TRANSFER)->ShowWindow(SW_HIDE);
				m_ButtonEnd.ShowWindow(SW_HIDE);

				m_ButtonCall.ShowWindow(SW_SHOW);
				GetDlgItem(IDC_MESSAGE)->ShowWindow(SW_SHOW);
			}
		}
		state = len ? true : false;

	}
	else {
		state = len ? true : false;
	}
	m_ButtonCall.EnableWindow(state);
#ifdef _GLOBAL_VIDEO
	GetDlgItem(IDC_VIDEO_CALL)->ShowWindow(SW_HIDE);
	GetDlgItem(IDC_VIDEO_CALL)->EnableWindow(FALSE);
#endif
	m_ButtonDialTone.EnableWindow(m_dialToneSessionActive || (!len && callsCount <= 0));
	CButton *buttonRedial = (CButton *)GetDlgItem(IDC_REDIAL);
	CButton *buttonDelete = (CButton *)GetDlgItem(IDC_DELETE);
	if (!state) {
		buttonDelete->ShowWindow(SW_HIDE);
		buttonRedial->ShowWindow(SW_SHOW);
	}
	else {
		buttonRedial->ShowWindow(SW_HIDE);
		buttonDelete->ShowWindow(SW_SHOW);
	}
	if (!len) {
		SetDTMF(_T(""));
	}
}

void Dialer::SetDialToneSessionActive(bool active)
{
	m_dialToneSessionActive = active;
	m_ButtonDialTone.SetWindowText(Translate(active ? _T("Hang-Up") : _T("Dial-Tone")));
	m_ButtonDialTone.m_FaceColor = active ? _GLOBAL_DIALER_END_COLOR
		: (accountSettings.darkMode ? DarkPalette::Surface() : _GLOBAL_DIALER_CALL_COLOR);
	UpdateCallButton();
	m_ButtonDialTone.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

void Dialer::Action(DialerActions action)
{
	CString number;
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	combobox->GetWindowText(number);
	number.Trim();
	if (!number.IsEmpty()) {
		mainDlg->DialToneCallStarting();
		bool res = false;
		if (action != ACTION_MESSAGE) {
			res = mainDlg->MakeCall(number, action == ACTION_VIDEO_CALL);
		}
		else {
			res = mainDlg->MessagesOpen(number);
		}
		if (res) {
			//-- save dialed in combobox
			DialedAdd(number);
			if (!accountSettings.singleMode) {
				Clear(true, true);
			}
			//-- end
		}
		else {
			mainDlg->DialToneCallCancelled();
		}
	}
}

void Dialer::Clear(bool update, bool preserveQualification)
{
	mainDlg->StopDialTone(L"Dial target cleared \x00B7 READY left", preserveQualification);
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	combobox->SetCurSel(-1);
	if (update) {
		UpdateCallButton();
	}
}

void Dialer::OnBnClickedCall()
{
	Action(ACTION_CALL);
}

void Dialer::OnBnClickedDialTone()
{
	if (m_dialToneSessionActive) {
		mainDlg->StopDialTone(L"Hang-Up pressed \x00B7 Dial-Tone session ended");
	}
	else {
		mainDlg->BeginDialToneReadiness();
	}
}

void Dialer::OnBnClickedDTMF()
{
	CString number;
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	combobox->GetWindowText(number);
	number.Trim();
	if (!number.IsEmpty()) {
		DTMF(number, true);
		SetDTMF(_T(""));
	}
}

#ifdef _GLOBAL_VIDEO
void Dialer::OnBnClickedVideoCall()
{
	Action(ACTION_VIDEO_CALL);
}
#endif

void Dialer::OnBnClickedHold()
{
	mainDlg->messagesDlg->OnBnClickedHold();
}

void Dialer::OnBnClickedTransfer()
{
	mainDlg->OpenTransferDlg(mainDlg, MSIP_ACTION_TRANSFER);
}

void Dialer::OnBnClickedEnd()
{
	MessagesContact*  messagesContact = mainDlg->messagesDlg->GetMessageContact();
	if (messagesContact && messagesContact->callId != -1) {
		msip_call_end(messagesContact->callId);
	}
	else {
		call_hangup_all_noincoming();
	}
}

void Dialer::OnCbnEditchangeComboAddr()
{
	CComboBox* combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	if (combobox->GetWindowTextLength()) {
		mainDlg->DialToneTargetEntered();
	}
	UpdateCallButton();
}

void Dialer::OnCbnSelchangeComboAddr()
{
	mainDlg->DialToneTargetEntered(true);
	UpdateCallButton(TRUE);
}

void Dialer::OnBnClickedKey1()
{
	Input(_T("1"));
}

void Dialer::OnBnClickedKey2()
{
	Input(_T("2"));
}

void Dialer::OnBnClickedKey3()
{
	Input(_T("3"));
}

void Dialer::OnBnClickedKey4()
{
	Input(_T("4"));
}

void Dialer::OnBnClickedKey5()
{
	Input(_T("5"));
}

void Dialer::OnBnClickedKey6()
{
	Input(_T("6"));
}

void Dialer::OnBnClickedKey7()
{
	Input(_T("7"));
}

void Dialer::OnBnClickedKey8()
{
	Input(_T("8"));
}

void Dialer::OnBnClickedKey9()
{
	Input(_T("9"));
}

void Dialer::OnBnClickedKeyStar()
{
	Input(_T("*"));
}

void Dialer::OnBnClickedKey0()
{
	Input(_T("0"));
}

void Dialer::OnBnClickedKeyGrate()
{
	Input(_T("#"));
}

void Dialer::OnBnClickedRedial()
{
	if (!accountSettings.lastCallNumber.IsEmpty()) {
		SetNumber(accountSettings.lastCallNumber);
	}
}

void Dialer::OnBnClickedCallTrace()
{
	mainDlg->MakeCall(_T("*69"));
}

void Dialer::OnBnClickedAccountSwitch()
{
	OpenAccountSidecar();
}

void Dialer::OnBnClickedDelete()
{
	CComboBox *combobox = (CComboBox*)GetDlgItem(IDC_NUMBER);
	CEdit* edit = (CEdit*)FindWindowEx(combobox->m_hWnd, NULL, _T("EDIT"), NULL);
	if (edit) {
		int nLength = edit->GetWindowTextLength();
		edit->SetSel(nLength - 1, nLength);
		edit->ReplaceSel(_T(""));
	}
}

void Dialer::OnBnClickedKeyPlus()
{
	Input(_T("+"), TRUE);
}

void Dialer::OnBnClickedClear()
{
	Clear();
}

void Dialer::OnLButtonUp(UINT nFlags, CPoint pt)
{
}

void Dialer::OnRButtonUp(UINT nFlags, CPoint pt)
{
}

void Dialer::OnMouseMove(UINT nFlags, CPoint pt)
{
}

void Dialer::MuteOutput(bool state)
{
	CButton *button = (CButton*)GetDlgItem(IDC_BUTTON_MUTE_OUTPUT);
	button->SetCheck(!state ? BST_CHECKED : BST_UNCHECKED);
	OnBnClickedMuteOutput();
}

void Dialer::MuteInput(bool state)
{
	CButton *button = (CButton*)GetDlgItem(IDC_BUTTON_MUTE_INPUT);
	button->SetCheck(!state ? BST_CHECKED : BST_UNCHECKED);
	OnBnClickedMuteInput();
}

void Dialer::OnHScroll(UINT, UINT, CScrollBar* sender)
{
	if (is_pjsua_running()) {
		int pos;
		if (!sender || sender == (CScrollBar*)&m_SliderCtrlOutput) {
			if (sender && muteOutput) {
				MuteOutput(false);
				return;
			}
			pos = m_SliderCtrlOutput.GetPos();
			//msip_audio_output_set_volume(pos,muteOutput);
			msip_audio_conf_set_volume(pos, muteOutput);
			accountSettings.volumeOutput = pos;
			mainDlg->AccountSettingsPendingSave();
		}
		if (!sender || sender == (CScrollBar*)&m_SliderCtrlInput) {
			if (sender && muteInput) {
				MuteInput(false);
				return;
			}
			pos = m_SliderCtrlInput.GetPos();
			msip_audio_input_set_volume(pos, muteInput);
			accountSettings.volumeInput = pos;
			mainDlg->AccountSettingsPendingSave();
		}
	}
}

void Dialer::OnBnClickedMinusInput()
{
	int pos = m_SliderCtrlInput.GetPos();
	if (pos > 0) {
		pos -= 5;
		if (pos < 0) {
			pos = 0;
		}
		m_SliderCtrlInput.SetPos(pos);
		OnHScroll(0, 0, (CScrollBar *)&m_SliderCtrlInput);
	}


}

void Dialer::OnBnClickedPlusInput()
{
	int pos = m_SliderCtrlInput.GetPos();
	if (pos < 100) {
		pos += 5;
		if (pos > 100) {
			pos = 100;
		}
		m_SliderCtrlInput.SetPos(pos);
		OnHScroll(0, 0, (CScrollBar *)&m_SliderCtrlInput);
	}
}

void Dialer::OnBnClickedMinusOutput()
{
	int pos = m_SliderCtrlOutput.GetPos();
	if (pos > 0) {
		pos -= 5;
		if (pos < 0) {
			pos = 0;
		}
		m_SliderCtrlOutput.SetPos(pos);
		OnHScroll(0, 0, (CScrollBar *)&m_SliderCtrlOutput);
	}
}

void Dialer::OnBnClickedPlusOutput()
{
	int pos = m_SliderCtrlOutput.GetPos();
	if (pos < 100) {
		pos += 5;
		if (pos > 100) {
			pos = 100;
		}
		m_SliderCtrlOutput.SetPos(pos);
		OnHScroll(0, 0, (CScrollBar *)&m_SliderCtrlOutput);
	}
}

void Dialer::OnBnClickedMuteOutput()
{
	CButton *button = (CButton*)GetDlgItem(IDC_BUTTON_MUTE_OUTPUT);
	if (button->GetCheck() == BST_CHECKED) {
		button->SetIcon(m_hIconMuteOutput);
		muteOutput = FALSE;
		OnHScroll(0, 0, NULL);
	}
	else {
		button->SetIcon(m_hIconMutedOutput);
		muteOutput = TRUE;
		OnHScroll(0, 0, NULL);
	}
	button->SetCheck(!button->GetCheck());
}

void Dialer::OnBnClickedMuteInput()
{
	CButton *button = (CButton*)GetDlgItem(IDC_BUTTON_MUTE_INPUT);
	if (button->GetCheck() == BST_CHECKED) {
		button->SetIcon(m_hIconMuteInput);
		muteInput = FALSE;
		OnHScroll(0, 0, NULL);
	}
	else {
		button->SetIcon(m_hIconMutedInput);
		muteInput = TRUE;
		OnHScroll(0, 0, NULL);
	}
	button->SetCheck(!button->GetCheck());
	if (accountSettings.headsetSupport) {
		Hid::SetMute(muteInput);
	}
}

void Dialer::TimerVuMeter()
{
	unsigned tx_level = 0, rx_level = 0;
	pjsua_conf_port_id ids[PJSUA_MAX_CONF_PORTS];
	unsigned count = PJSUA_MAX_CONF_PORTS;
	if (is_pjsua_running() && pjsua_call_get_count() && pjsua_enum_conf_ports(ids, &count) == PJ_SUCCESS && count > 1) {
		for (unsigned i = 0; i < count; i++) {
			unsigned tx_level_curr, rx_level_curr;
			pjsua_conf_port_info conf_port_info;
#ifdef NDEBUG
			if (pjsua_conf_get_port_info(ids[i], &conf_port_info) == PJ_SUCCESS) {
				if (pjsua_conf_get_signal_level(ids[i], &tx_level_curr, &rx_level_curr) == PJ_SUCCESS) {
					if (conf_port_info.slot_id == 0) {
						tx_level = rx_level_curr * (conf_port_info.rx_level_adj > 0 ? 1 : 0);
					}
					else {
						rx_level_curr = conf_port_info.rx_level_adj > 0 ? rx_level_curr : 0;
						if (rx_level_curr > rx_level) {
							rx_level = rx_level_curr;
						}
					}
				}
			}
#endif
		}
		if (!m_SliderCtrlInput.IsActive) m_SliderCtrlInput.IsActive = true;
		if (!m_SliderCtrlOutput.IsActive) m_SliderCtrlOutput.IsActive = true;
	}
	else {
		KillTimer(IDT_TIMER_VU_METER);
		m_SliderCtrlInput.IsActive = false;
		m_SliderCtrlOutput.IsActive = false;
	}
	//CString s;
	//s.Format(_T("tx %d rx %d"),tx_level_max, tx_level_max);
	//mainDlg->SetWindowText(s);
	m_SliderCtrlInput.SetSelection(0, tx_level / 0.95);
	m_SliderCtrlInput.Invalidate(FALSE);
	m_SliderCtrlOutput.SetSelection(0, rx_level / 1.15);
	m_SliderCtrlOutput.Invalidate(FALSE);
}


BOOL Dialer::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (pWnd == &m_ButtonVoicemail || pWnd == &m_ButtonVoicemailDisabled) {
		::SetCursor(m_hCursorHand);
		return TRUE;
	}
	return CBaseDialog::OnSetCursor(pWnd, nHitTest, message);
}

void Dialer::OnBnClickedDND()
{
	mainDlg->SwitchDND();
}

void Dialer::OnBnClickedFWD()
{
	CString destination;
	m_CfwDestination.GetWindowText(destination);
	destination.Trim();
	accountSettings.FWD = !destination.IsEmpty() && m_ButtonFWD.GetCheck() == BST_CHECKED;
	m_ButtonFWD.SetCheck(accountSettings.FWD ? BST_CHECKED : BST_UNCHECKED);
	mainDlg->UpdateWindowText();
	mainDlg->AccountSettingsPendingSave();
}

void Dialer::OnCfwDestinationChange()
{
	if (!m_rebuildingButtons) UpdateCfwState(true);
}

void Dialer::UpdateCfwState(bool persist)
{
	if (!::IsWindow(m_CfwDestination.m_hWnd) || !::IsWindow(m_ButtonFWD.m_hWnd)) return;
	CString destination;
	m_CfwDestination.GetWindowText(destination);
	accountSettings.forwardingNumber = destination;
	CString usable = destination;
	usable.Trim();
	if (usable.IsEmpty()) accountSettings.FWD = false;
	m_ButtonFWD.EnableWindow(!usable.IsEmpty());
	m_ButtonFWD.SetCheck(accountSettings.FWD ? BST_CHECKED : BST_UNCHECKED);
	if (persist) {
		mainDlg->UpdateWindowText();
		mainDlg->AccountSettingsPendingSave();
	}
}

void Dialer::OnBnClickedAA()
{
	accountSettings.AA = m_ButtonAA.GetCheck() == BST_CHECKED;
	mainDlg->UpdateWindowText();
	mainDlg->AccountSettingsPendingSave();
}

void Dialer::OnBnClickedAC()
{
	accountSettings.AC = m_ButtonAC.GetCheck() == BST_CHECKED;
	mainDlg->UpdateWindowText();
	mainDlg->AccountSettingsPendingSave();
}

void Dialer::OnBnClickedConf()
{
	if (accountSettings.singleMode) {
		mainDlg->OpenTransferDlg(mainDlg, MSIP_ACTION_INVITE);
	}
	else {
		mainDlg->messagesDlg->OnBnClickedConference();
	}
}

void Dialer::OnBnClickedRec()
{
	MessagesContact*  messagesContact = mainDlg->messagesDlg->GetMessageContact();
	if (messagesContact && messagesContact->callId != -1) {
		call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(messagesContact->callId);
		if (user_data) {
			user_data->CS.Lock();
			if (user_data->recorder_id == PJSUA_INVALID_ID) {
				msip_call_recording_start(user_data);
			}
			else {
				msip_call_recording_stop(user_data, 0, true);
			}
			user_data->CS.Unlock();
			mainDlg->messagesDlg->UpdateRecButton(user_data);
		}
	}
}

void Dialer::OnBnClickedVoicemail()
{
	if (accountSettings.accountId && !accountSettings.account.voicemailNumber.IsEmpty()) {
		mainDlg->MakeCall(accountSettings.account.voicemailNumber);
	}
}

void Dialer::OnBnClickedShortcut(UINT nID)
{
	if (shortcuts.GetCount() == shortcutButtons.GetCount()) {
		int i = nID - IDC_SHORTCUT_RANGE;
		mainDlg->ShortcutAction(&shortcuts.GetAt(i), false, !(((CButton*)GetDlgItem(nID))->GetCheck() & BST_CHECKED));
	}
}

void Dialer::SetCheckDND(bool checked)
{
	if (IsChild(&m_ButtonDND)) {
		m_ButtonDND.SetCheck(checked ? BST_CHECKED : BST_UNCHECKED);
	}
}
void Dialer::SetCheckREC(bool checked)
{
	if (IsChild(&m_ButtonRec)) {
		m_ButtonRec.SetCheck(checked ? BST_CHECKED : BST_UNCHECKED);
	}
}
void Dialer::EnableButtonCONF(bool enabled)
{
	if (IsChild(&m_ButtonConf)) {
		m_ButtonConf.EnableWindow(enabled);
	}
}

