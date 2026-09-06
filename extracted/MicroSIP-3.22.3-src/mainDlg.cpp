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

#define THIS_FILENAME "mainDlg.cpp"

#include "mainDlg.h"
#include "microsip.h"

#include "Mmsystem.h"
#include "settings.h"
#include "global.h"
#include "ModelessMessageBox.h"
#include "json.h"
#include "Markup.h"
#include "langpack.h"
#include "jumplist.h"
#include "atlenc.h"
#include "Hid.h"
#include "CMask.h"

#include <winuser.h>
#include <windows.h>
#include <io.h>
#include <afxmt.h>
#include <afxinet.h>
#include <ws2tcpip.h>
#include <Dbt.h>
#include <Strsafe.h>
#include <locale.h> 
#include <Wtsapi32.h>
#include "atlrx.h"
#include "StdioFileEx.h"
#include "DarkPalette.h"
#include "ButtonBottom.h"

#include "afxvisualmanager.h"
#include "afxvisualmanagerwindows.h"

#include "iphlpapi.h"
#include "wininet.h"
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

static CString BrandingFolder()
{
	CString root = accountSettings.appDataRoaming;
	root.TrimRight(_T("\\"));
	return root + _T("\\freepbxUK\\Branding");
}

static CString BrandingLogoPath()
{
	if (accountSettings.brandingLogoFile.IsEmpty()
		|| accountSettings.brandingLogoFile.FindOneOf(_T("\\/")) != -1) return CString();
	return BrandingFolder() + _T("\\") + accountSettings.brandingLogoFile;
}

static bool LoadBitmapBytes(const BYTE* bytes, DWORD size, Gdiplus::Bitmap*& bitmap, IStream*& stream)
{
	bitmap = NULL;
	stream = NULL;
	if (!bytes || !size) return false;
	HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, size);
	if (!buffer) return false;
	void* destination = GlobalLock(buffer);
	if (!destination) { GlobalFree(buffer); return false; }
	memcpy(destination, bytes, size);
	GlobalUnlock(buffer);
	if (CreateStreamOnHGlobal(buffer, TRUE, &stream) != S_OK) { GlobalFree(buffer); return false; }
	bitmap = Gdiplus::Bitmap::FromStream(stream, FALSE);
	if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
		delete bitmap; bitmap = NULL; stream->Release(); stream = NULL; return false;
	}
	return true;
}

static bool LoadBitmapResource(Gdiplus::Bitmap*& bitmap, IStream*& stream)
{
	HINSTANCE instance = AfxGetResourceHandle();
	HRSRC resource = FindResource(instance, MAKEINTRESOURCE(IDR_FREEPBXUK_LOGO), RT_RCDATA);
	if (!resource) return false;
	HGLOBAL loaded = LoadResource(instance, resource);
	return loaded && LoadBitmapBytes((const BYTE*)LockResource(loaded), SizeofResource(instance, resource), bitmap, stream);
}

static bool LoadBitmapFile(const CString& path, Gdiplus::Bitmap*& bitmap, IStream*& stream)
{
	CFile file;
	if (!file.Open(path, CFile::modeRead | CFile::shareDenyNone)) return false;
	ULONGLONG length = file.GetLength();
	if (!length || length > MAXDWORD) { file.Close(); return false; }
	CArray<BYTE, BYTE> bytes;
	bytes.SetSize((INT_PTR)length);
	UINT read = file.Read(bytes.GetData(), (UINT)length);
	file.Close();
	return read == length && LoadBitmapBytes(bytes.GetData(), (DWORD)length, bitmap, stream);
}

static bool ValidBrandingUrl(const CString& value)
{
	if (value.IsEmpty()) return true;
	CString lower = value; lower.MakeLower();
	if (lower.Find(_T("http://")) != 0 && lower.Find(_T("https://")) != 0) return false;
	URL_COMPONENTS parts = { sizeof(parts) };
	TCHAR host[INTERNET_MAX_HOST_NAME_LENGTH + 1] = { 0 };
	parts.lpszHostName = host;
	parts.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;
	return InternetCrackUrl(value, 0, 0, &parts) && parts.dwHostNameLength > 0
		&& (parts.nScheme == INTERNET_SCHEME_HTTP || parts.nScheme == INTERNET_SCHEME_HTTPS);
}

class CBrandingDlg : public CDialog
{
public:
	CBrandingDlg(CWnd* parent) : CDialog(IDD_BRANDING, parent), custom(accountSettings.brandingCustom), image(NULL), stream(NULL) {}
	~CBrandingDlg() { ClearImage(); }

protected:
	BOOL OnInitDialog()
	{
		CDialog::OnInitDialog();
		url = custom ? accountSettings.brandingUrl : _T("https://freepbx.uk");
		SetDlgItemText(IDC_BRANDING_URL, url);
		GetDlgItem(IDC_BRANDING_URL)->EnableWindow(custom);
		((CEdit*)GetDlgItem(IDC_BRANDING_URL))->SetReadOnly(!custom);
		LoadPreview(custom ? BrandingLogoPath() : CString(), !custom);
		BOOL dark = accountSettings.darkMode;
		DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
		for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
			SetWindowTheme(child->m_hWnd, accountSettings.darkMode ? L"DarkMode_Explorer" : NULL, NULL);
		return TRUE;
	}

	void ClearImage() { delete image; image = NULL; if (stream) { stream->Release(); stream = NULL; } }
	bool LoadPreview(const CString& path, bool bundled = false)
	{
		ClearImage();
		bool loaded = bundled ? LoadBitmapResource(image, stream) : (!path.IsEmpty() && LoadBitmapFile(path, image, stream));
		if (::IsWindow(m_hWnd)) GetDlgItem(IDC_BRANDING_PREVIEW)->Invalidate();
		return loaded;
	}

	afx_msg void OnDrawItem(int controlId, LPDRAWITEMSTRUCT item)
	{
		if (controlId != IDC_BRANDING_PREVIEW) { CDialog::OnDrawItem(controlId, item); return; }
		Gdiplus::Graphics graphics(item->hDC);
		COLORREF background = accountSettings.darkMode ? DarkPalette::Window() : GetSysColor(COLOR_3DFACE);
		graphics.Clear(Gdiplus::Color(255, GetRValue(background), GetGValue(background), GetBValue(background)));
		int clientWidth = item->rcItem.right - item->rcItem.left;
		int clientHeight = item->rcItem.bottom - item->rcItem.top;
		const int horizontalMargin = MulDiv(16, dpiY, 96);
		const int verticalPadding = MulDiv(8, dpiY, 96);
		const int logoDisplayHeight = max(0, clientHeight - verticalPadding * 2);
		const int availableWidth = max(0, clientWidth - horizontalMargin * 2);
		if (image) {
			const UINT sourceWidth = image->GetWidth(), sourceHeight = image->GetHeight();
			double scale = sourceHeight ? (double)logoDisplayHeight / sourceHeight : 0.0;
			if (sourceWidth && sourceWidth * scale > availableWidth) scale = (double)availableWidth / sourceWidth;
			int width = (int)(sourceWidth * scale), height = (int)(sourceHeight * scale);
			int left = (clientWidth - width) / 2, top = verticalPadding + (logoDisplayHeight - height) / 2;
			if (width > 0 && height > 0) {
				graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
				graphics.DrawImage(image, Gdiplus::Rect(left, top, width, height));
			}
		}
		else {
			Gdiplus::Font font(L"Segoe UI", (Gdiplus::REAL)MulDiv(9, dpiY, 96), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush brush(accountSettings.darkMode ? Gdiplus::Color(255, 190, 196, 202) : Gdiplus::Color(255, 100, 100, 100));
			Gdiplus::StringFormat format; format.SetAlignment(Gdiplus::StringAlignmentCenter); format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			graphics.DrawString(L"Custom logo unavailable", -1, &font, Gdiplus::RectF(0, 0, (Gdiplus::REAL)clientWidth, (Gdiplus::REAL)clientHeight), &format, &brush);
		}
	}

	afx_msg void OnChangeLogo()
	{
		CFileDialog picker(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
			_T("Images (*.png;*.jpg;*.jpeg;*.gif;*.bmp)|*.png;*.jpg;*.jpeg;*.gif;*.bmp|All files (*.*)|*.*||"), this);
		if (picker.DoModal() != IDOK) return;
		Gdiplus::Bitmap* test = NULL; IStream* testStream = NULL;
		if (!LoadBitmapFile(picker.GetPathName(), test, testStream)) { AfxMessageBox(_T("The selected image could not be loaded."), MB_ICONERROR); return; }
		delete test; testStream->Release();
		custom = true;
		stagedSource = picker.GetPathName();
		url.Empty();
		SetDlgItemText(IDC_BRANDING_URL, _T(""));
		GetDlgItem(IDC_BRANDING_URL)->EnableWindow(TRUE);
		((CEdit*)GetDlgItem(IDC_BRANDING_URL))->SetReadOnly(FALSE);
		LoadPreview(stagedSource);
	}

	afx_msg void OnRestoreDefault()
	{
		custom = false; stagedSource.Empty(); url = _T("https://freepbx.uk");
		SetDlgItemText(IDC_BRANDING_URL, url); GetDlgItem(IDC_BRANDING_URL)->EnableWindow(FALSE);
		((CEdit*)GetDlgItem(IDC_BRANDING_URL))->SetReadOnly(TRUE);
		LoadPreview(CString(), true);
	}

	void OnOK()
	{
		GetDlgItemText(IDC_BRANDING_URL, url);
		if (custom && !ValidBrandingUrl(url)) { AfxMessageBox(_T("Enter a valid http:// or https:// URL, or leave it blank."), MB_ICONERROR); GetDlgItem(IDC_BRANDING_URL)->SetFocus(); return; }
		CString oldPath = BrandingLogoPath(), newFile = accountSettings.brandingLogoFile;
		if (custom && !stagedSource.IsEmpty()) {
			CString folder = BrandingFolder();
			CString parent = folder.Left(folder.ReverseFind(_T('\\')));
			if ((!CreateDirectory(parent, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
				|| (!CreateDirectory(folder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)) { AfxMessageBox(_T("Unable to create the Branding folder."), MB_ICONERROR); return; }
			int dot = stagedSource.ReverseFind(_T('.'));
			CString extension = dot == -1 ? CString(_T(".img")) : stagedSource.Mid(dot);
			if (extension.GetLength() > 10 || extension.FindOneOf(_T("\\/:*?\"<>|")) != -1) extension = _T(".img");
			newFile = _T("logo") + extension;
			CString finalPath = folder + _T("\\") + newFile, pendingPath = folder + _T("\\logo.pending") + extension;
			DeleteFile(pendingPath);
			if (!CopyFile(stagedSource, pendingPath, FALSE) || !MoveFileEx(pendingPath, finalPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFile(pendingPath); AfxMessageBox(_T("Unable to save the custom logo."), MB_ICONERROR); return; }
		}
		accountSettings.brandingCustom = custom;
		accountSettings.brandingUrl = custom ? url : CString();
		accountSettings.brandingLogoFile = custom ? newFile : CString();
		accountSettings.SettingsSave();
		if (!oldPath.IsEmpty() && oldPath.CompareNoCase(BrandingLogoPath()) != 0) DeleteFile(oldPath);
		CDialog::OnOK();
	}

	afx_msg BOOL OnEraseBkgnd(CDC* dc)
	{
		if (!accountSettings.darkMode) return CDialog::OnEraseBkgnd(dc);
		CRect rect; GetClientRect(&rect); dc->FillSolidRect(rect, DarkPalette::Window()); return TRUE;
	}
	afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* child, UINT type)
	{
		if (accountSettings.darkMode) {
			static CBrush panel(DarkPalette::Window()), input(DarkPalette::Input());
			dc->SetTextColor(DarkPalette::Text());
			if (type == CTLCOLOR_EDIT) { dc->SetBkColor(DarkPalette::Input()); return input; }
			if (type == CTLCOLOR_STATIC || type == CTLCOLOR_DLG) { dc->SetBkColor(DarkPalette::Window()); return panel; }
		}
		return CDialog::OnCtlColor(dc, child, type);
	}

	DECLARE_MESSAGE_MAP()
private:
	bool custom;
	CString url, stagedSource;
	Gdiplus::Bitmap* image;
	IStream* stream;
};

BEGIN_MESSAGE_MAP(CBrandingDlg, CDialog)
	ON_WM_DRAWITEM()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_BRANDING_CHANGE, OnChangeLogo)
	ON_BN_CLICKED(IDC_BRANDING_RESTORE, OnRestoreDefault)
END_MESSAGE_MAP()

class CmainDlg::CFreepbxFooter : public CWnd
{
public:
	CFreepbxFooter() : image(NULL), stream(NULL), handCursor(NULL) {}
	~CFreepbxFooter()
	{
		delete image;
		if (stream) {
			stream->Release();
		}
	}

	bool LoadBranding()
	{
		delete image; image = NULL;
		if (stream) { stream->Release(); stream = NULL; }
		logoUrl = accountSettings.brandingCustom ? accountSettings.brandingUrl : _T("https://freepbx.uk");
		bool loaded = accountSettings.brandingCustom
			? LoadBitmapFile(BrandingLogoPath(), image, stream)
			: LoadBitmapResource(image, stream);
		logoClickable = loaded && !logoUrl.IsEmpty() && ValidBrandingUrl(logoUrl);
		return loaded;
	}

protected:
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnPaint()
	{
		CPaintDC dc(this);
		CRect client;
		GetClientRect(&client);
		Gdiplus::Graphics graphics(dc.m_hDC);
		COLORREF background = accountSettings.darkMode ? DarkPalette::Window() : GetSysColor(COLOR_3DFACE);
		graphics.Clear(Gdiplus::Color(255, GetRValue(background), GetGValue(background), GetBValue(background)));
		const int horizontalMargin = MulDiv(16, dpiY, 96);
		const int verticalPadding = MulDiv(8, dpiY, 96);
		const int contentGap = MulDiv(4, dpiY, 96);
		const int attributionHeight = MulDiv(12, dpiY, 96);
		Gdiplus::Font font(L"Segoe UI", (Gdiplus::REAL)MulDiv(8, dpiY, 96), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		const int logoDisplayHeight = max(0,
			client.Height() - verticalPadding * 2 - contentGap - attributionHeight);
		const int availableWidth = max(0, client.Width() - horizontalMargin * 2);
		const UINT sourceWidth = image ? image->GetWidth() : 0;
		const UINT sourceHeight = image ? image->GetHeight() : 0;
		double scale = sourceHeight ? (double)logoDisplayHeight / sourceHeight : 0.0;
		if (sourceWidth && sourceWidth * scale > availableWidth) {
			scale = (double)availableWidth / sourceWidth;
		}
		int width = (int)(sourceWidth * scale);
		int height = (int)(sourceHeight * scale);
		int left = (client.Width() - width) / 2;
		int top = verticalPadding + (logoDisplayHeight - height) / 2;
		freepbxLinkRect = logoClickable ? CRect(left, top, left + width, top + height) : CRect(0, 0, 0, 0);
		if (width > 0 && height > 0) {
			graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
			graphics.DrawImage(image, Gdiplus::Rect(left, top, width, height));
		}

		Gdiplus::SolidBrush textBrush(accountSettings.darkMode ? Gdiplus::Color(255, 190, 196, 202) : Gdiplus::Color(255, 100, 100, 100));
		Gdiplus::SolidBrush linkBrush(Gdiplus::Color(255, 40, 90, 150));
		Gdiplus::RectF measured;
		const WCHAR* prefix = L"Powered by ";
		const WCHAR* link = L"MicroSIP";
		const WCHAR* suffix = L" \x2013 SIP Softphone for Windows";
		graphics.MeasureString(prefix, -1, &font, Gdiplus::PointF(0, 0), &measured);
		Gdiplus::REAL prefixWidth = measured.Width;
		graphics.MeasureString(link, -1, &font, Gdiplus::PointF(0, 0), &measured);
		Gdiplus::REAL linkWidth = measured.Width;
		Gdiplus::REAL suffixWidth;
		graphics.MeasureString(suffix, -1, &font, Gdiplus::PointF(0, 0), &measured);
		suffixWidth = measured.Width;
		Gdiplus::REAL textWidth = prefixWidth + linkWidth + suffixWidth;
		Gdiplus::REAL textX = (Gdiplus::REAL)((client.Width() - textWidth) / 2);
		Gdiplus::REAL textY = (Gdiplus::REAL)(verticalPadding + logoDisplayHeight + contentGap);
		graphics.DrawString(prefix, -1, &font, Gdiplus::PointF(textX, textY), &textBrush);
		textX += prefixWidth;
		microsipLinkRect = CRect((int)textX, (int)textY, (int)(textX + linkWidth), (int)(textY + MulDiv(12, dpiY, 96)));
		graphics.DrawString(link, -1, &font, Gdiplus::PointF(textX, textY), &linkBrush);
		textX += linkWidth;
		graphics.DrawString(suffix, -1, &font, Gdiplus::PointF(textX, textY), &textBrush);
	}
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point)
	{
		if (logoClickable && freepbxLinkRect.PtInRect(point)) {
			ShellExecute(NULL, _T("open"), logoUrl, NULL, NULL, SW_SHOWNORMAL);
		}
		else if (microsipLinkRect.PtInRect(point)) {
			ShellExecute(NULL, _T("open"), _T("https://www.microsip.org/"), NULL, NULL, SW_SHOWNORMAL);
		}
		CWnd::OnLButtonUp(nFlags, point);
	}
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
	{
		CPoint point;
		GetCursorPos(&point);
		ScreenToClient(&point);
		if ((logoClickable && freepbxLinkRect.PtInRect(point)) || microsipLinkRect.PtInRect(point)) {
			if (!handCursor) {
				handCursor = LoadCursor(NULL, IDC_HAND);
			}
			SetCursor(handCursor);
			return TRUE;
		}
		return CWnd::OnSetCursor(pWnd, nHitTest, message);
	}
	DECLARE_MESSAGE_MAP()

private:
	Gdiplus::Bitmap* image;
	IStream* stream;
	CRect freepbxLinkRect;
	CRect microsipLinkRect;
	HCURSOR handCursor;
	CString logoUrl;
	bool logoClickable = false;
};

BEGIN_MESSAGE_MAP(CmainDlg::CFreepbxFooter, CWnd)
	ON_WM_ERASEBKGND()
	ON_WM_PAINT()
	ON_WM_LBUTTONUP()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

// Scrolling technical event log for the current call session.
static const COLORREF CALL_TRACE_SAVE_FLASH_COLOR = RGB(34, 139, 34);
static const COLORREF CALL_TRACE_CLEAR_FLASH_COLOR = RGB(178, 34, 34);
static const COLORREF CALL_TRACE_FLASH_TEXT_COLOR = RGB(255, 255, 255);

class CmainDlg::CCallTracePanel : public CWnd
{
public:
	CCallTracePanel() : lineCount(0) {}

	bool CreatePanel(CWnd* parent)
	{
		// The panel overlaps the unused tail of the dialler page.  Clip both sibling
		// windows so a later dialler repaint cannot cover this window's children.
		if (!CreateEx(0, AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW), NULL,
			WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), parent, 0)) {
			return false;
		}
		LOGFONT lf;
		pj_bzero(&lf, sizeof(lf));
		lf.lfHeight = -MulDiv(11, dpiY, 96);
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = DEFAULT_CHARSET;
		StringCchCopy(lf.lfFaceName, LF_FACESIZE, _T("Consolas"));
		logFont.CreateFontIndirect(&lf);

		traceMode.Create(Translate(_T("Call Trace")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE,
			CRect(0, 0, 0, 0), this, IDC_CALL_TRACE_MODE);
		notesMode.Create(Translate(_T("Call Notes")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE,
			CRect(0, 0, 0, 0), this, IDC_CALL_NOTES_MODE);
		spkClock.Create(_T("Spk Clock"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			CRect(0, 0, 0, 0), this, IDC_DIALER_SPK_CLOCK);
		echoTest.Create(_T("Echo Test"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			CRect(0, 0, 0, 0), this, IDC_DIALER_ECHO_TEST);
		current.Create(Translate(_T("Current")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			CRect(0, 0, 0, 0), this, IDC_CALL_RECORD_CURRENT);
		recent.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 0, 0), this, IDC_CALL_RECORD_RECENT);
		copy.Create(Translate(_T("Copy")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			CRect(0, 0, 0, 0), this, IDC_CALL_RECORD_COPY);
		save.Create(Translate(_T("Save")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			CRect(0, 0, 0, 0), this, IDC_CALL_RECORD_SAVE);
		undo.Create(Translate(_T("Undo")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			CRect(0, 0, 0, 0), this, IDC_CALL_RECORD_UNDO);
		clear.Create(Translate(_T("Clear")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			CRect(0, 0, 0, 0), this, IDC_CALL_TRACE_CLEAR);
		traceMode.SetFont(parent->GetFont());
		notesMode.SetFont(parent->GetFont());
		spkClock.SetFont(parent->GetFont());
		echoTest.SetFont(parent->GetFont());
		current.SetFont(parent->GetFont());
		recent.SetFont(parent->GetFont());
		save.SetFont(parent->GetFont());
		copy.SetFont(parent->GetFont());
		undo.SetFont(parent->GetFont());
		clear.SetFont(parent->GetFont());
		log.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_READONLY
			| ES_AUTOVSCROLL | ES_LEFT, CRect(0, 0, 0, 0), this, IDC_CALL_TRACE_LOG);
		log.SetFont(&logFont);
		notes.Create(WS_CHILD | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_LEFT,
			CRect(0, 0, 0, 0), this, IDC_CALL_NOTES_EDIT);
		notes.SetFont(&logFont);
		notes.SetPlaceholder(Translate(_T("Start typing...")));
		traceMode.SetCheck(BST_CHECKED);
		mode = 0;
		viewingCurrent = true;
		SetDarkMode(accountSettings.darkMode);
		Reset();
		RefreshRecent();
		UpdateActionStates();
		return true;
	}

	void Reset()
	{
		ResetPresentation();
		lineCount = 0;
		currentTrace.Empty();
		if (::IsWindow(log.m_hWnd) && mode == 0 && viewingCurrent) {
			log.SetWindowText(Translate(_T("No active call")));
		}
		UpdateActionStates();
	}

	void Clear()
	{
		ResetPresentation();
		lineCount = 0;
		currentTrace.Empty();
		if (::IsWindow(log.m_hWnd) && mode == 0 && viewingCurrent) {
			log.SetWindowText(_T(""));
		}
		UpdateActionStates();
	}

	void Append(CString text)
	{
		if (!::IsWindow(log.m_hWnd)) {
			return;
		}
		SYSTEMTIME now;
		GetLocalTime(&now);
		CString line;
		line.Format(_T("%s%02d:%02d:%02d.%03d  %s"), lineCount ? _T("\r\n") : _T(""),
			now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
		currentTrace += line;
		lineCount++;
		TrimHistory();
		if (mode != 0 || !viewingCurrent) {
			return;
		}
		bool follow = IsScrolledToBottom();
		POINT original = GetScrollPosition();
		AppendVisibleLine(line, text.GetLength());
		UpdateActionStates();
		if (follow) {
			log.SetSel(log.GetWindowTextLength(), log.GetWindowTextLength());
			log.SendMessage(EM_SCROLLCARET);
			m_scrollTarget = GetScrollPosition();
			SetScrollPosition(original);
			StartScrollAnimation();
		}
	}

	bool BeginCall(pjsua_call_info* callInfo, bool preserveQualification = false)
	{
		if (!callInfo || callInfo->id == metadata.callId) {
			return true;
		}
		if (mode == 1 && viewingCurrent) {
			currentNotes = GetText(notes);
		}
		if (!currentNotes.IsEmpty() && currentNotes != savedCurrentNotes && !PromptDiscardNotes()) {
			return false;
		}
		metadata.callId = callInfo->id;
		metadata.started = CTime::GetCurrentTime();
		metadata.finalDuration = -1;
		metadata.active = true;
		metadata.username = accountSettings.account.username;
		metadata.sipCallId = CString(callInfo->call_id.ptr, callInfo->call_id.slen);
		metadata.callerId = CallerFromUri(CString(callInfo->remote_info.ptr, callInfo->remote_info.slen));
		if (metadata.callerId.IsEmpty()) {
			metadata.callerId = _T("Unknown");
		}
		currentNotes.Empty();
		savedCurrentNotes.Empty();
		notes.SetWindowText(_T(""));
		if (!preserveQualification) {
			Clear();
		}
		ShowCurrent();
		return true;
	}

	void EndCall(pjsua_call_info* callInfo)
	{
		if (callInfo && callInfo->id == metadata.callId && metadata.active) {
			metadata.finalDuration = max(0, (int)(CTime::GetCurrentTime() - metadata.started).GetTotalSeconds());
			metadata.active = false;
		}
	}

	void ShowMode(int newMode)
	{
		CString notesToSave = currentNotes;
		if (mode == 1) {
			notesToSave = GetText(notes);
			if (viewingCurrent) currentNotes = notesToSave;
		}
		mode = newMode;
		viewingCurrent = true;
		traceMode.SetCheck(mode == 0 ? BST_CHECKED : BST_UNCHECKED);
		notesMode.SetCheck(mode == 1 ? BST_CHECKED : BST_UNCHECKED);
		ShowCurrent();
		RefreshRecent();
		UpdateActionStates();
	}

	void ShowCurrent()
	{
		viewingCurrent = true;
		current.EnableWindow(TRUE);
		if (mode == 0) {
			log.SetWindowText(currentTrace.IsEmpty() && metadata.callId == PJSUA_INVALID_ID ? Translate(_T("No active call")) : currentTrace);
			FormatTraceTimestamps();
			log.ShowWindow(SW_SHOW);
			notes.ShowWindow(SW_HIDE);
		}
		else {
			notes.SetWindowText(currentNotes);
			log.ShowWindow(SW_HIDE);
			notes.ShowWindow(SW_SHOW);
		}
		UpdateActionStates();
	}

	void RefreshRecent()
	{
		recent.ResetContent();
		recentPaths.RemoveAll();
		CString folder = SaveFolder();
		CString pattern = folder + _T("\\*-") + (mode == 0 ? _T("Trace-") : _T("Note-")) + _T("*.txt");
		CFileFind finder;
		BOOL found = finder.FindFile(pattern);
		CArray<CString, CString&> paths;
		while (found) {
			found = finder.FindNextFile();
			if (!finder.IsDots() && !finder.IsDirectory()) {
				paths.Add(finder.GetFilePath());
			}
		}
		for (int i = 0; i < paths.GetCount(); i++) {
			for (int j = i + 1; j < paths.GetCount(); j++) {
				if (SaveTimeFromPath(paths[j]) > SaveTimeFromPath(paths[i])) {
					CString swap = paths[i]; paths[i] = paths[j]; paths[j] = swap;
				}
			}
		}
		for (int i = 0; i < paths.GetCount() && i < 5; i++) {
			CString fileName = paths[i].Mid(paths[i].ReverseFind('\\') + 1);
			recent.AddString(fileName);
			recentPaths.Add(paths[i]);
		}
	}

	void LoadRecent()
	{
		int selection = recent.GetCurSel();
		if (selection == CB_ERR || selection >= recentPaths.GetCount()) {
			return;
		}
		CStdioFileEx file;
		file.SetCodePage(CP_UTF8);
		CString text;
		if (!file.Open(recentPaths[selection], CFile::modeRead | CFile::typeText) || !file.ReadString(text)) {
			return;
		}
		CString all = text;
		while (file.ReadString(text)) {
			all += _T("\r\n") + text;
		}
		file.Close();
		viewingCurrent = false;
		if (mode == 0) {
			log.SetWindowText(all);
			FormatTraceTimestamps();
		}
		else {
			notes.SetWindowText(all);
		}
		UpdateActionStates();
	}

	void Save()
	{
		CString notesToSave = currentNotes;
		if (mode == 1 && viewingCurrent) {
			currentNotes = GetText(notes);
			notesToSave = currentNotes;
		}
		else if (mode == 1 && !viewingCurrent) {
			notesToSave = GetText(notes);
		}
		CString folder = SaveFolder();
		if (!EnsureFolder(folder)) {
			AfxMessageBox(Translate(_T("Unable to create the Call Records folder.")), MB_ICONERROR);
			return;
		}
		CString caller = SafeFilePart(metadata.callerId.IsEmpty() ? _T("Unknown") : metadata.callerId);
		CString username = SafeFilePart(metadata.username.IsEmpty() ? _T("Unknown") : metadata.username);
		CTime started = metadata.started.GetTime() ? metadata.started : CTime::GetCurrentTime();
		int duration = metadata.finalDuration >= 0 ? metadata.finalDuration : (metadata.active ? max(0, (int)(CTime::GetCurrentTime() - started).GetTotalSeconds()) : 0);
		ULONGLONG saveTime = UnixMilliseconds();
		CString fileName;
		fileName.Format(_T("%s-%s-%dS-%s-%s-%I64u.txt"), caller, started.Format(_T("%Y-%m-%d-%H%M%S")), duration,
			username, mode == 0 ? _T("Trace") : _T("Note"), saveTime);
		CString body;
		body.Format(_T("Caller ID: %s\r\nStarted: %s\r\nDuration: %d\r\nUsername: %s\r\nSIP Call-ID: %s\r\n\r\n%s:\r\n%s\r\n"),
			metadata.callerId, started.Format(_T("%Y-%m-%d %H:%M:%S")), duration, metadata.username, metadata.sipCallId,
			mode == 0 ? _T("Trace") : _T("Notes"), mode == 0 ? currentTrace : notesToSave);
		CStdioFileEx file;
		file.SetCodePage(CP_UTF8);
		file.SetWriteBOM(true);
		CFileException error;
		if (!file.Open(folder + _T("\\") + fileName, CFile::modeCreate | CFile::modeWrite | CFile::typeText, &error)) {
			AfxMessageBox(Translate(_T("Unable to save the call record.")), MB_ICONERROR);
			return;
		}
		file.WriteString(body);
		file.Close();
		if (mode == 1 && viewingCurrent) {
			savedCurrentNotes = currentNotes;
		}
		RefreshRecent();
		ShowFeedback(FeedbackSave);
		UpdateActionStates();
	}

	void SetDarkMode(bool enabled)
	{
		darkMode = enabled;
		traceMode.SetDarkMode(enabled);
		notesMode.SetDarkMode(enabled);
		spkClock.SetDarkMode(enabled);
		echoTest.SetDarkMode(enabled);
		current.SetDarkMode(enabled);
		save.SetDarkMode(enabled);
		copy.SetDarkMode(enabled);
		undo.SetDarkMode(enabled);
		clear.SetDarkMode(enabled);
		recent.SetDarkMode(enabled);
		notes.SetDarkMode(enabled);
		SetWindowTheme(log.m_hWnd, enabled ? L"DarkMode_Explorer" : NULL, NULL);
		SetWindowTheme(notes.m_hWnd, enabled ? L"DarkMode_Explorer" : NULL, NULL);
		// Rich Edit ignores WM_CTLCOLOR, so background and default text colour go through its own API.
		log.SetBackgroundColor(enabled ? FALSE : TRUE, enabled ? DarkPalette::Input() : 0);
		CHARFORMAT2 base;
		memset(&base, 0, sizeof(base));
		base.cbSize = sizeof(CHARFORMAT2);
		base.dwMask = CFM_COLOR;
		base.crTextColor = EventColor();
		log.SetDefaultCharFormat(base);
		FormatTraceTimestamps();
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}

	void LayoutChildren()
	{
		if (!::IsWindow(m_hWnd)) {
			return;
		}
		CRect client;
		GetClientRect(&client);
		int pad = MulDiv(4, dpiY, 96);
		int headerHeight = MulDiv(18, dpiY, 96);
		int modeWidth = MulDiv(62, dpiY, 96);
		int rowWidth = max(0, client.Width() - pad * 2);
		int fourButtonWidth = max(0, (rowWidth - pad * 3) / 4);
		int fourButtonRowWidth = fourButtonWidth * 4 + pad * 3;
		int fourButtonLeft = pad + max(0, (rowWidth - fourButtonRowWidth) / 2);
		traceMode.SetWindowPos(NULL, fourButtonLeft, pad, fourButtonWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
		notesMode.SetWindowPos(NULL, fourButtonLeft + fourButtonWidth + pad, pad, fourButtonWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
		spkClock.SetWindowPos(NULL, fourButtonLeft + (fourButtonWidth + pad) * 2, pad, fourButtonWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
		echoTest.SetWindowPos(NULL, fourButtonLeft + (fourButtonWidth + pad) * 3, pad, fourButtonWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
		int recentTop = pad * 2 + headerHeight;
		current.SetWindowPos(NULL, pad, recentTop, modeWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
		int recentLeft = pad + modeWidth + pad;
		recent.SetWindowPos(NULL, recentLeft, recentTop, max(0, client.right - pad - recentLeft), MulDiv(120, dpiY, 96), SWP_NOACTIVATE | SWP_NOZORDER);
		int logTop = recentTop + headerHeight + pad;
		int actionTop = max(logTop, client.bottom - pad - headerHeight);
		int editorHeight = max(0, actionTop - pad - logTop);
		log.SetWindowPos(NULL, pad, logTop, client.Width() - pad * 2, editorHeight,
			SWP_NOACTIVATE | SWP_NOZORDER);
		notes.SetWindowPos(NULL, pad, logTop, client.Width() - pad * 2, editorHeight,
			SWP_NOACTIVATE | SWP_NOZORDER);
		copy.SetWindowPos(NULL, fourButtonLeft, actionTop, fourButtonWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
		save.SetWindowPos(NULL, fourButtonLeft + fourButtonWidth + pad, actionTop, fourButtonWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
		undo.SetWindowPos(NULL, fourButtonLeft + (fourButtonWidth + pad) * 2, actionTop, fourButtonWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
		clear.SetWindowPos(NULL, fourButtonLeft + (fourButtonWidth + pad) * 3, actionTop, fourButtonWidth, headerHeight, SWP_NOACTIVATE | SWP_NOZORDER);
	}

	// The children keep WS_VISIBLE while the panel is hidden, so showing the panel does not invalidate them.
	void ShowPanel()
	{
		BOOL wasVisible = IsWindowVisible();
		ShowWindow(SW_SHOW);
		LayoutChildren();
		if (!wasVisible) {
			RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		}
	}

protected:
	afx_msg BOOL OnEraseBkgnd(CDC* dc)
	{
		CRect rect;
		GetClientRect(&rect);
		dc->FillSolidRect(rect, darkMode ? DarkPalette::Window() : GetSysColor(COLOR_3DFACE));
		return TRUE;
	}
	afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* child, UINT controlColor)
	{
		if (darkMode) {
			static CBrush darkPanelBrush(DarkPalette::Window());
			static CBrush darkEditBrush(DarkPalette::Input());
			if (controlColor == CTLCOLOR_EDIT || controlColor == CTLCOLOR_LISTBOX) {
				dc->SetTextColor(DarkPalette::Text());
				dc->SetBkColor(DarkPalette::Input());
				return darkEditBrush;
			}
			if (controlColor == CTLCOLOR_STATIC || controlColor == CTLCOLOR_DLG) {
				dc->SetTextColor(DarkPalette::Text());
				dc->SetBkColor(DarkPalette::Window());
				return darkPanelBrush;
			}
		}
		return CWnd::OnCtlColor(dc, child, controlColor);
	}
	afx_msg void OnSize(UINT type, int cx, int cy)
	{
		CWnd::OnSize(type, cx, cy);
		LayoutChildren();
	}
	afx_msg void OnTimer(UINT_PTR timerId)
	{
		if (timerId == IDT_CALL_TRACE_FEEDBACK) {
			EndFeedback();
			return;
		}
		if (timerId != IDT_CALL_TRACE_SCROLL) {
			CWnd::OnTimer(timerId);
			return;
		}
		if (mode != 0 || !viewingCurrent || !autoFollow) {
			KillTimer(IDT_CALL_TRACE_SCROLL);
			return;
		}
		POINT current = GetScrollPosition();
		int remaining = m_scrollTarget.y - current.y;
		if (abs(remaining) <= 1) {
			SetScrollPosition(m_scrollTarget);
			KillTimer(IDT_CALL_TRACE_SCROLL);
			return;
		}
		current.y += remaining / 4;
		if (current.y == m_scrollTarget.y) current.y += remaining > 0 ? 1 : -1;
		SetScrollPosition(current);
	}
	afx_msg void OnClear()
	{
		if (mode == 0 && viewingCurrent) {
			Clear();
			ShowFeedback(FeedbackClear);
		}
		else if (mode == 1 && viewingCurrent) {
			currentNotes.Empty();
			notes.SetWindowText(_T(""));
			ShowFeedback(FeedbackClear);
		}
		UpdateActionStates();
	}
	afx_msg void OnTraceMode() { ShowMode(0); }
	afx_msg void OnNotesMode() { ShowMode(1); }
	afx_msg void OnCurrent() { ShowCurrent(); }
	afx_msg void OnSave() { Save(); }
	afx_msg void OnCopy()
	{
		CString text;
		if (mode == 0) log.GetWindowText(text);
		else notes.GetWindowText(text);
		if (!text.IsEmpty()) mainDlg->CopyStringToClipboard(text);
		UpdateActionStates();
	}
	afx_msg void OnUndo()
	{
		if (mode == 1 && notes.CanUndo()) {
			notes.SetFocus();
			notes.Undo();
		}
		UpdateActionStates();
	}
	afx_msg void OnNotesUpdate() { UpdateActionStates(); }
	afx_msg void OnSpkClock() { mainDlg->MakeCall(_T("*60")); }
	afx_msg void OnEchoTest() { mainDlg->MakeCall(_T("*43")); }
	afx_msg void OnRecentChange() { LoadRecent(); }
	afx_msg void OnTraceScroll()
	{
		if (programmaticScroll) return;
		autoFollow = IsScrolledToBottom();
		if (!autoFollow) KillTimer(IDT_CALL_TRACE_SCROLL);
	}
	DECLARE_MESSAGE_MAP()

private:
	enum { IDT_CALL_TRACE_SCROLL = 0x7F01, CALL_TRACE_SCROLL_FRAME_MS = 16,
		IDT_CALL_TRACE_FEEDBACK = 0x7F02, CALL_TRACE_FEEDBACK_MS = 600 };
	enum FeedbackState { FeedbackNone, FeedbackSave, FeedbackClear };

	// A single active state keeps repeated or alternating clicks from stranding either button.
	void ShowFeedback(FeedbackState state)
	{
		EndFeedback();
		if (state == FeedbackSave) {
			save.SetFlash(CALL_TRACE_SAVE_FLASH_COLOR, CALL_TRACE_FLASH_TEXT_COLOR);
		}
		else if (state == FeedbackClear) {
			clear.SetFlash(CALL_TRACE_CLEAR_FLASH_COLOR, CALL_TRACE_FLASH_TEXT_COLOR);
		}
		SetTimer(IDT_CALL_TRACE_FEEDBACK, CALL_TRACE_FEEDBACK_MS, NULL);
	}

	void EndFeedback()
	{
		KillTimer(IDT_CALL_TRACE_FEEDBACK);
		save.ClearFlash();
		clear.ClearFlash();
	}

	void UpdateActionStates()
	{
		if (::IsWindow(undo.m_hWnd)) {
			undo.EnableWindow(mode == 1 && ::IsWindow(notes.m_hWnd) && notes.CanUndo());
		}
		if (::IsWindow(copy.m_hWnd)) {
			int length = mode == 0 ? log.GetWindowTextLength() : notes.GetWindowTextLength();
			copy.EnableWindow(length > 0);
		}
	}

	void ResetPresentation()
	{
		KillTimer(IDT_CALL_TRACE_SCROLL);
		m_scrollTarget.x = 0;
		m_scrollTarget.y = 0;
		autoFollow = true;
	}

	POINT GetScrollPosition()
	{
		POINT point = { 0, 0 };
		log.SendMessage(EM_GETSCROLLPOS, 0, (LPARAM)&point);
		return point;
	}

	void SetScrollPosition(POINT point)
	{
		programmaticScroll = true;
		log.SendMessage(EM_SETSCROLLPOS, 0, (LPARAM)&point);
		programmaticScroll = false;
	}

	void StartScrollAnimation()
	{
		if (m_scrollTarget.y != GetScrollPosition().y) SetTimer(IDT_CALL_TRACE_SCROLL, CALL_TRACE_SCROLL_FRAME_MS, NULL);
	}

	COLORREF EventColor() const { return darkMode ? DarkPalette::Text() : GetSysColor(COLOR_WINDOWTEXT); }
	COLORREF TimestampColor() const { return darkMode ? DarkPalette::SecondaryText() : RGB(120, 120, 120); }

	void AppendVisibleLine(const CString& line, int eventLength)
	{
		if (lineCount == 1) log.SetWindowText(_T(""));
		int length = log.GetWindowTextLength();
		int timestampLength = line.GetLength() - eventLength;
		log.SetSel(length, length);
		CHARFORMAT2 timestamp;
		memset(&timestamp, 0, sizeof(timestamp));
		timestamp.cbSize = sizeof(CHARFORMAT2);
		timestamp.dwMask = CFM_COLOR;
		timestamp.crTextColor = TimestampColor();
		log.SetSelectionCharFormat(timestamp);
		log.ReplaceSel(line.Left(timestampLength));
		CHARFORMAT2 event;
		memset(&event, 0, sizeof(event));
		event.cbSize = sizeof(CHARFORMAT2);
		event.dwMask = CFM_COLOR;
		event.crTextColor = EventColor();
		log.SetSelectionCharFormat(event);
		log.ReplaceSel(line.Mid(timestampLength));
	}

	// Recolours the whole buffer, so it also covers historical snapshots and theme switches.
	void FormatTraceTimestamps()
	{
		if (!::IsWindow(log.m_hWnd)) {
			return;
		}
		CString text;
		log.GetWindowText(text);
		POINT scroll = GetScrollPosition();
		long selectionStart = 0;
		long selectionEnd = 0;
		log.GetSel(selectionStart, selectionEnd);
		log.SetRedraw(FALSE);
		CHARFORMAT2 event;
		memset(&event, 0, sizeof(event));
		event.cbSize = sizeof(CHARFORMAT2);
		event.dwMask = CFM_COLOR;
		event.crTextColor = EventColor();
		log.SetSel(0, -1);
		log.SetSelectionCharFormat(event);
		CHARFORMAT2 timestamp;
		memset(&timestamp, 0, sizeof(timestamp));
		timestamp.cbSize = sizeof(CHARFORMAT2);
		timestamp.dwMask = CFM_COLOR;
		timestamp.crTextColor = TimestampColor();
		int start = 0;
		while (start < text.GetLength()) {
			int end = text.Find(_T('\n'), start);
			int length = (end == -1 ? text.GetLength() : end) - start;
			if (length >= 14 && text.Mid(start + 2, 1) == _T(":") && text.Mid(start + 5, 1) == _T(":")) {
				log.SetSel(start, start + 14);
				log.SetSelectionCharFormat(timestamp);
			}
			if (end == -1) break;
			start = end + 1;
		}
		log.SetSel(selectionStart, selectionEnd);
		SetScrollPosition(scroll);
		log.SetRedraw(TRUE);
		log.Invalidate();
	}

	struct CallMetadata {
		pjsua_call_id callId = PJSUA_INVALID_ID;
		CTime started;
		int finalDuration = -1;
		bool active = false;
		CString callerId;
		CString username;
		CString sipCallId;
	};

	static CString GetText(CEdit& edit)
	{
		CString text;
		edit.GetWindowText(text);
		return text;
	}

	static CString CallerFromUri(CString uri)
	{
		int scheme = uri.Find(_T("sip:"));
		if (scheme != -1) uri = uri.Mid(scheme + 4);
		int end = uri.FindOneOf(_T("@;>"));
		return end == -1 ? uri : uri.Left(end);
	}

	static ULONGLONG SaveTimeFromPath(CString path)
	{
		int dot = path.ReverseFind(_T('.'));
		int dash = path.ReverseFind(_T('-'));
		if (dash == -1 || dot <= dash) return 0;
		return _ttoi64(path.Mid(dash + 1, dot - dash - 1));
	}

	static CString SafeFilePart(CString part)
	{
		part.Trim();
		for (int i = 0; i < part.GetLength(); i++) {
			if (part[i] < 32 || _tcschr(_T("\\/:*?\"<>|"), part[i])) part.SetAt(i, _T('_'));
		}
		part.Trim(_T(". "));
		return part.IsEmpty() ? _T("Unknown") : part;
	}

	static CString SaveFolder()
	{
		CString folder = accountSettings.appDataRoaming;
		folder.TrimRight(_T("\\"));
		return folder + _T("\\freepbxUK\\CallRecords");
	}

	static bool EnsureFolder(CString folder)
	{
		int position = 0;
		while ((position = folder.Find(_T('\\'), position)) != -1) {
			CString partial = folder.Left(position++);
			if (partial.GetLength() > 2 && !CreateDirectory(partial, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
		}
		return CreateDirectory(folder, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
	}

	static ULONGLONG UnixMilliseconds()
	{
		FILETIME fileTime;
		GetSystemTimeAsFileTime(&fileTime);
		ULARGE_INTEGER value;
		value.LowPart = fileTime.dwLowDateTime;
		value.HighPart = fileTime.dwHighDateTime;
		return (value.QuadPart - 116444736000000000ULL) / 10000;
	}

	bool PromptDiscardNotes()
	{
		int result = AfxMessageBox(Translate(_T("Save notes for the previous call?")), MB_YESNOCANCEL | MB_ICONQUESTION);
		if (result == IDCANCEL) return false;
		if (result == IDYES) {
			int previousMode = mode;
			bool previousCurrent = viewingCurrent;
			mode = 1;
			viewingCurrent = true;
			Save();
			mode = previousMode;
			viewingCurrent = previousCurrent;
		}
		return true;
	}

	bool IsScrolledToBottom()
	{
		CRect rect;
		log.GetClientRect(&rect);
		CDC* dc = log.GetDC();
		if (!dc) {
			return true;
		}
		CFont* oldFont = dc->SelectObject(&logFont);
		TEXTMETRIC tm;
		dc->GetTextMetrics(&tm);
		dc->SelectObject(oldFont);
		log.ReleaseDC(dc);
		if (tm.tmHeight <= 0) {
			return true;
		}
		int visibleLines = rect.Height() / tm.tmHeight;
		return log.GetFirstVisibleLine() + visibleLines >= log.GetLineCount();
	}

	// Bounded textual history; oldest lines are dropped, no SIP messages are retained.
	void TrimHistory()
	{
		const int maxLines = 1000;
		if (lineCount <= maxLines) {
			return;
		}
		int drop = lineCount - maxLines;
		int charIndex = 0;
		while (drop-- > 0) {
			charIndex = currentTrace.Find(_T('\n'), charIndex);
			if (charIndex == -1) {
				currentTrace.Empty();
				lineCount = 0;
				return;
			}
			charIndex++;
		}
		if (!charIndex) {
			return;
		}
		currentTrace = currentTrace.Mid(charIndex);
		lineCount = maxLines;
	}

	CButtonBottom traceMode;
	CButtonBottom notesMode;
	CButtonBottom spkClock;
	CButtonBottom echoTest;
	CButtonBottom current;
	CDarkComboBox recent;
	CButtonBottom copy;
	CButtonBottom save;
	CButtonBottom undo;
	CButtonBottom clear;
	CRichEditCtrl log;
	CPlaceholderEdit notes;
	CFont logFont;
	int lineCount;
	bool darkMode = false;
	int mode;
	bool viewingCurrent;
	CString currentTrace;
	CString currentNotes;
	CString savedCurrentNotes;
	CArray<CString, CString&> recentPaths;
	POINT m_scrollTarget = { 0, 0 };
	bool autoFollow = true;
	bool programmaticScroll = false;
	CallMetadata metadata;
};

BEGIN_MESSAGE_MAP(CmainDlg::CCallTracePanel, CWnd)
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_CALL_TRACE_CLEAR, OnClear)
	ON_BN_CLICKED(IDC_CALL_TRACE_MODE, OnTraceMode)
	ON_BN_CLICKED(IDC_CALL_NOTES_MODE, OnNotesMode)
	ON_BN_CLICKED(IDC_CALL_RECORD_CURRENT, OnCurrent)
	ON_BN_CLICKED(IDC_CALL_RECORD_SAVE, OnSave)
	ON_BN_CLICKED(IDC_CALL_RECORD_COPY, OnCopy)
	ON_BN_CLICKED(IDC_CALL_RECORD_UNDO, OnUndo)
	ON_BN_CLICKED(IDC_DIALER_SPK_CLOCK, OnSpkClock)
	ON_BN_CLICKED(IDC_DIALER_ECHO_TEST, OnEchoTest)
	ON_CBN_SELCHANGE(IDC_CALL_RECORD_RECENT, OnRecentChange)
	ON_EN_UPDATE(IDC_CALL_NOTES_EDIT, OnNotesUpdate)
	ON_EN_VSCROLL(IDC_CALL_TRACE_LOG, OnTraceScroll)
END_MESSAGE_MAP()

CmainDlg* mainDlg;

static UINT WM_SHELLHOOKMESSAGE;
static UINT WM_TASKBARRESTARTMESSAGE;
#define WM_APPBAR_CALLBACK (WM_USER + 0x50)

static bool updateCheckerShow;

static bool upstream_updates_enabled()
{
	// Upstream MicroSIP updates are disabled in the FreePBX UK fork.
	// Fork updates must not replace this build with an upstream MicroSIP release.
	return false;
}

static UINT BASED_CODE indicators[] =
{
	IDS_STATUSBAR,
	IDS_STATUSBAR2,
};

struct DialToneOptionsToken
{
	HWND window;
	unsigned generation;
	DWORD started;
	pj_pool_t* pool;
	pjsip_auth_clt_sess authSession;
	bool authInitialized;
	unsigned authRetries;
};

struct DialToneOptionsResult
{
	unsigned generation;
	int statusCode;
	DWORD elapsed;
	CString statusText;
	CString responseSource;
	CString localAddress;
	CString activeTransport;
	bool authenticationFailed;
};

static CString pj_error_text(pj_status_t status)
{
	char text[PJ_ERR_MSG_SIZE];
	pj_strerror(status, text, sizeof(text));
	return MSIP::Utf8DecodeUni(text);
}

static CString sip_target_for_trace(CString target)
{
	target.Trim();
	int headers = target.Find(_T('?'));
	if (headers != -1) {
		target = target.Left(headers);
	}
	int scheme = target.Find(_T(':'));
	int at = target.ReverseFind(_T('@'));
	if (scheme != -1 && at > scheme) {
		CString userInfo = target.Mid(scheme + 1, at - scheme - 1);
		if (userInfo.Find(_T(':')) != -1) {
			target = target.Left(scheme + 1) + _T("<redacted>") + target.Mid(at);
		}
	}
	return target;
}

static void destroy_dial_tone_options_token(DialToneOptionsToken* request)
{
	if (!request) {
		return;
	}
	if (request->authInitialized) {
		pjsip_auth_clt_deinit(&request->authSession);
	}
	if (request->pool) {
		pj_pool_release(request->pool);
	}
	delete request;
}

static void on_dial_tone_options_request(void* token, pjsip_event* event)
{
	DialToneOptionsToken* request = (DialToneOptionsToken*)token;
	if (!request) {
		return;
	}
	pjsip_transaction* transaction = NULL;
	pjsip_rx_data* response = NULL;
	if (event && event->type == PJSIP_EVENT_TSX_STATE && event->body.tsx_state.tsx) {
		transaction = event->body.tsx_state.tsx;
		response = event->body.tsx_state.src.rdata;
	}
	if (transaction && response
		&& (transaction->status_code == PJSIP_SC_UNAUTHORIZED
			|| transaction->status_code == PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED)
		&& request->authInitialized && request->authRetries < 3 && transaction->last_tx) {
		pjsip_tx_data* authenticatedRequest = NULL;
		pj_status_t authStatus = pjsip_auth_clt_reinit_req(&request->authSession, response,
			transaction->last_tx, &authenticatedRequest);
		if (authStatus == PJ_SUCCESS && authenticatedRequest) {
			request->authRetries++;
			pj_status_t sendStatus = pjsip_endpt_send_request(pjsua_get_pjsip_endpt(),
				authenticatedRequest, -1, request, &on_dial_tone_options_request);
			if (sendStatus == PJ_SUCCESS) {
				return;
			}
		}
	}
	DialToneOptionsResult* result = new DialToneOptionsResult();
	result->generation = request->generation;
	result->elapsed = GetTickCount() - request->started;
	result->statusCode = 0;
	result->authenticationFailed = false;
	if (transaction) {
		result->statusCode = transaction->status_code;
		result->statusText = MSIP::PjToStr(&transaction->status_text);
		result->authenticationFailed = result->statusCode == PJSIP_SC_UNAUTHORIZED
			|| result->statusCode == PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED;
		if (transaction->transport) {
			result->activeTransport = MSIP::Utf8DecodeUni(transaction->transport->type_name);
			char address[PJ_INET6_ADDRSTRLEN + 16];
			if (pj_sockaddr_print(&transaction->transport->local_addr, address, sizeof(address), 3)) {
				result->localAddress = MSIP::Utf8DecodeUni(address);
			}
		}
		if (response && response->pkt_info.src_name[0]) {
			result->responseSource = MSIP::Utf8DecodeUni(response->pkt_info.src_name);
			CString port;
			port.Format(_T(":%d"), response->pkt_info.src_port);
			result->responseSource += port;
		}
	}
	if (!::IsWindow(request->window)
		|| !::PostMessage(request->window, UM_DIAL_TONE_OPTIONS, 0, (LPARAM)result)) {
		delete result;
	}
	destroy_dial_tone_options_token(request);
}

static void on_acc_send_request(pjsua_acc_id, void* token, pjsip_event* event)
{
	on_dial_tone_options_request(token, event);
}

static int usersDirectorySequence;
static int usersDirectoryRefresh;
static int usersDirectorySilent;
static int usersDirectoryReconnect;

CCriticalSection gethostbyaddrThreadCS;
static CString gethostbyaddrThreadResult;
static DWORD WINAPI gethostbyaddrThread(LPVOID lpParam)
{
	CString* addr = (CString*)lpParam;
	CString res = *addr;
	delete addr;
	struct hostent* he = NULL;
	struct in_addr inaddr;
	inaddr.S_un.S_addr = inet_addr(CStringA(res));
	if (inaddr.S_un.S_addr != INADDR_NONE && inaddr.S_un.S_addr != INADDR_ANY) {
		he = gethostbyaddr((char*)&inaddr, 4, AF_INET);
		if (he) {
			res = he->h_name;
		}
	}
	gethostbyaddrThreadCS.Lock();
	gethostbyaddrThreadResult = res;
	gethostbyaddrThreadCS.Unlock();
	return 0;
}

static CString normalize_caller_identity(const pj_str_t* caller_identity)
{
	CString normalized = MSIP::PjToStr(caller_identity, true);
	if (normalized.Find('@') == -1) {
		normalized.Empty();
	}
	else {
		int pos = normalized.Find(';');
		if (pos != -1) {
			normalized = normalized.Left(pos);
		}
		normalized.Trim();
	}
	return normalized;
}

static CString effective_call_identity_for_log(const pjsua_call_info* call_info, const CString& callerID)
{
	SIPURI sipuri;
	MSIP::ParseSIPURI(MSIP::PjToStr(&call_info->remote_info, TRUE), &sipuri);
	if (!callerID.IsEmpty()) {
		SIPURI sipuriCallerID;
		MSIP::ParseSIPURI(callerID, &sipuriCallerID);
		if (!sipuriCallerID.user.IsEmpty()) {
			sipuri.user = sipuriCallerID.user;
		}
		if (!sipuriCallerID.domain.IsEmpty()) {
			sipuri.domain = sipuriCallerID.domain;
		}
		if (!sipuriCallerID.name.IsEmpty()) {
			sipuri.name = sipuriCallerID.name;
		}
	}
	CString uri = (!sipuri.user.IsEmpty() ? sipuri.user + _T("@") : _T("")) + sipuri.domain;
	if (!sipuri.name.IsEmpty()) {
		return sipuri.name + _T(" <") + uri + _T(">");
	}
	return uri;
}

static bool redirect_incoming_call(pjsua_call_info* call_info, const CString& configuredDestination,
	CString& contactTarget, CString& failure)
{
	contactTarget = configuredDestination;
	contactTarget.Trim();
	if (contactTarget.IsEmpty()) {
		failure = _T("destination is blank");
		return false;
	}

	CString lower = contactTarget;
	lower.MakeLower();
	if (lower.Find(_T("tel:")) == 0) {
		contactTarget = contactTarget.Mid(4);
		contactTarget.Trim();
		lower = contactTarget;
		lower.MakeLower();
	}
	if (lower.Find(_T("sip:")) != 0 && lower.Find(_T("sips:")) != 0) {
		if (contactTarget.Find(_T('@')) == -1) {
			SIPURI localUri;
			MSIP::ParseSIPURI(MSIP::PjToStr(&call_info->local_info, TRUE), &localUri);
			if (localUri.domain.IsEmpty()) {
				failure = _T("incoming account has no SIP domain");
				return false;
			}
			contactTarget += _T("@") + localUri.domain;
		}
		contactTarget = _T("sip:") + contactTarget;
	}

	pj_pool_t* pool = pjsua_pool_create("client_cfw", 1024, 1024);
	if (!pool) {
		failure = _T("unable to allocate SIP redirect data");
		return false;
	}
	CStringA targetUtf8 = MSIP::Utf8EncodeUni(contactTarget);
	int targetLength = targetUtf8.GetLength();
	char* targetBuffer = targetUtf8.GetBuffer();
	pjsip_uri* targetUri = pjsip_parse_uri(pool, targetBuffer, targetLength, 0);
	if (!targetUri || (!PJSIP_URI_SCHEME_IS_SIP(targetUri) && !PJSIP_URI_SCHEME_IS_SIPS(targetUri))) {
		targetUtf8.ReleaseBuffer();
		pj_pool_release(pool);
		failure = _T("destination is not a valid SIP/SIPS Contact URI");
		return false;
	}

	pjsua_msg_data response;
	pjsua_msg_data_init(&response);
	pjsip_contact_hdr* contact = pjsip_contact_hdr_create(pool);
	contact->uri = targetUri;
	pj_list_push_back(&response.hdr_list, contact);
	pj_status_t status = pjsua_call_answer(call_info->id, PJSIP_SC_MOVED_TEMPORARILY,
		NULL, &response);
	targetUtf8.ReleaseBuffer();
	pj_pool_release(pool);
	if (status != PJ_SUCCESS) {
		failure = pj_error_text(status);
		return false;
	}
	return true;
}

static void post_call_forwarding_result(bool succeeded, const CString& destination, const CString& detail = _T(""))
{
	CString* message = new CString();
	if (succeeded) {
		message->Format(_T("CFW           REDIRECT · %s"), destination);
	}
	else {
		message->Format(_T("CFW           FAILED · %s%s%s"), destination,
			detail.IsEmpty() ? _T("") : _T(" · "), detail);
	}
	if (!::PostMessage(mainDlg->m_hWnd, UM_CALL_FORWARDING_RESULT, succeeded, (LPARAM)message)) {
		delete message;
	}
}

static void on_reg_started2(pjsua_acc_id acc_id, pjsua_reg_info* info)
{
	if (info->renew) {
		PostMessage(mainDlg->m_hWnd, UM_UPDATEWINDOWTEXT, 1, 0);
	}
}

static void on_reg_state2(pjsua_acc_id acc_id, pjsua_reg_info* info)
{
	if (!IsWindow(mainDlg->m_hWnd)) {
		return;
	}
	CString* str = NULL;
	if (info->cbparam->code >= 400 && info->cbparam->rdata) {
		pjsip_generic_string_hdr* hsr;
		const pj_str_t headerError = { "P-Registrar-Error",17 };
		hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(info->cbparam->rdata->msg_info.msg, &headerError, NULL);
		if (hsr) {
			str = new CString();
			str->SetString(MSIP::PjToStr(&hsr->hvalue, true));
		}
	}
	PostMessage(mainDlg->m_hWnd, UM_ON_REG_STATE2, (WPARAM)info->cbparam->code, (LPARAM)str);
}

LRESULT CmainDlg::onRegState2(WPARAM wParam, LPARAM lParam)
{
	int code = wParam;
	if (code != PJSIP_SC_OK) {
		StopDialTone(_T("Registration changed from READY · dial tone stopped"));
	}
	CString headerError;
	if (lParam) {
		CString* str = (CString*)lParam;
		headerError = *str;
		delete str;
	}

	if (code == 200) {
		Subscribe();
		if (accountSettings.usersDirectory.Find(_T("%s")) != -1 || accountSettings.usersDirectory.Find(_T("{")) != -1) {
			UsersDirectoryLoad();
		}
	}
	else {
	}

	UpdateWindowText(headerError, IDI_DEFAULT, true);

	return 0;
}

/* Callback from timer when the maximum call duration has been
 * exceeded.
 */
static void call_timeout_callback(pj_timer_heap_t* timer_heap,
	struct pj_timer_entry* entry)
{
	pjsua_call_id call_id = entry->id;
	pjsua_msg_data msg_data_;
	pjsip_generic_string_hdr warn;
	pj_str_t hname = pj_str("Warning");
	pj_str_t hvalue = pj_str("399 localhost \"Call duration exceeded\"");

	PJ_UNUSED_ARG(timer_heap);

	if (call_id == PJSUA_INVALID_ID) {
		PJ_LOG(1, (THIS_FILENAME, "Invalid call ID in timer callback"));
		return;
	}

	/* Add warning header */
	pjsua_msg_data_init(&msg_data_);
	pjsip_generic_string_hdr_init2(&warn, &hname, &hvalue);
	pj_list_push_back(&msg_data_.hdr_list, &warn);

	/* Call duration has been exceeded; disconnect the call */
	PJ_LOG(3, (THIS_FILENAME, "Duration (%d seconds) has been exceeded "
		"for call %d, disconnecting the call",
		accountSettings.autoHangUpTime, call_id));
	entry->id = PJSUA_INVALID_ID;
	pjsua_call_hangup(call_id, 200, NULL, &msg_data_);
}

static void on_call_state(pjsua_call_id call_id, pjsip_event* e)
{
	if (!IsWindow(mainDlg->m_hWnd)) {
		return;
	}
	pjsua_call_info* call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}

	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED && call_info->last_status == 481) {
		return;
	}
	call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_info->id);
	// reset user_data after call transfer
	if (user_data) {
		user_data->CS.Lock();
		bool callIdMissmatch = user_data->call_id != PJSUA_INVALID_ID && user_data->call_id != call_info->id;
		bool hidden = user_data->hidden;
		user_data->CS.Unlock();
		if (callIdMissmatch) {
			user_data = new call_user_data(call_info->id);
			pjsua_call_set_user_data(call_info->id, user_data);
		}
		else {
			if (hidden) {
				if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
                    pjsua_call_set_user_data(call_info->id, NULL);
					delete user_data;
				}
				return;
			}
		}
	}
	if (!user_data) {
		user_data = new call_user_data(call_info->id);
		pjsua_call_set_user_data(call_info->id, user_data);
	}

	user_data->CS.Lock();

	switch (call_info->state) {
	case PJSIP_INV_STATE_CALLING:
		msip_call_unhold(call_info);
		break;
	case PJSIP_INV_STATE_CONNECTING:
		msip_call_unhold(call_info);
		break;
	case PJSIP_INV_STATE_CONFIRMED:
		if (accountSettings.autoRecording) {
			msip_call_recording_start(user_data, call_info);
		}
		if (accountSettings.autoHangUpTime > 0) {
			/* Schedule timer to hangup call after the specified duration */
			pj_time_val delay;
			user_data->auto_hangup_timer.id = call_info->id;
			user_data->auto_hangup_timer.cb = &call_timeout_callback;
			delay.sec = accountSettings.autoHangUpTime;
			delay.msec = 0;
			pjsua_schedule_timer(&user_data->auto_hangup_timer, &delay);
		}
		break;
	case PJSIP_INV_STATE_DISCONNECTED:
		pjsua_call_set_user_data(call_info->id, NULL);
		break;
	}

	user_data->CS.Unlock();

	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_STATE, (WPARAM)call_info, (LPARAM)user_data);
}

LRESULT CmainDlg::onCallState(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_info* call_info = (pjsua_call_info*)wParam;
	call_user_data* user_data = (call_user_data*)lParam;

	CallTraceOnCallState(call_info);

	SIPURI sipuri;
    ParseCallSIPURI(call_info, user_data, &sipuri);
    CString number = (!sipuri.user.IsEmpty() ? sipuri.user + _T("@") : _T("")) + sipuri.domain;

	user_data->CS.Lock();

	CString* str = new CString();
	CString adder;

	if (call_info->state != PJSIP_INV_STATE_DISCONNECTED && call_info->state != PJSIP_INV_STATE_CONNECTING && call_info->remote_contact.slen > 0) {
		SIPURI contactURI;
		ParseCallSIPURI(call_info, user_data, &contactURI);
		CString contactDomain = MSIP::RemovePort(contactURI.domain);
		struct hostent* he = NULL;
		if (MSIP::IsIP(contactDomain)) {
			HANDLE hThread;
			CString* addr = new CString(contactDomain);
			if (addr) {
				hThread = CreateThread(NULL, 0, gethostbyaddrThread, addr, 0, NULL);
				if (WaitForSingleObject(hThread, 500) == 0) {
					gethostbyaddrThreadCS.Lock();
					contactDomain = gethostbyaddrThreadResult;
					gethostbyaddrThreadCS.Unlock();
				}
			}
		}
		adder.AppendFormat(_T("%s; "), contactDomain);
	}

	if (call_info->state == PJSIP_INV_STATE_CONFIRMED
		|| call_info->state == PJSIP_INV_STATE_CONNECTING) {
		if (autoAnswerTimerCallId != PJSUA_INVALID_ID) {
			KillTimer(IDT_TIMER_AUTOANSWER);
			autoAnswerTimerCallId = PJSUA_INVALID_ID;
		}
		if (forwardingTimerCallId != PJSUA_INVALID_ID) {
			KillTimer(IDT_TIMER_FORWARDING);
			forwardingTimerCallId = PJSUA_INVALID_ID;
		}
	}

	unsigned cnt = 0;
	unsigned cnt_srtp = 0;

	switch (call_info->state) {
	case PJSIP_INV_STATE_CALLING:
		*str = Translate(_T("Calling"));
		str->AppendFormat(_T(" %s "), !sipuri.user.IsEmpty() ? sipuri.user : sipuri.domain);
		str->Append(_T("..."));
		break;
	case PJSIP_INV_STATE_INCOMING:
		str->SetString(Translate(_T("Incoming Call")));
		break;
	case PJSIP_INV_STATE_EARLY:
		str->SetString(Translate(MSIP::PjToStr(&call_info->last_status_text).GetBuffer()));
		break;
	case PJSIP_INV_STATE_CONNECTING:
		str->Format(_T("%s..."), Translate(_T("Connecting")));
		break;
	case PJSIP_INV_STATE_CONFIRMED:
		str->SetString(Translate(_T("Connected")));
		for (unsigned i = 0; i < call_info->media_cnt; i++) {
			if (call_info->media[i].dir != PJMEDIA_DIR_NONE &&
				(call_info->media[i].type == PJMEDIA_TYPE_AUDIO || call_info->media[i].type == PJMEDIA_TYPE_VIDEO)) {
				cnt++;
				pjsua_call_info call_info_stub;
				if (is_pjsua_running() && pjsua_call_get_info(call_info->id, &call_info_stub) == PJ_SUCCESS) {
                    bool srtp = false;
                    bool ice = false;
                    pjmedia_transport_info t;
                    if (pjsua_call_get_med_transport_info(call_info->id, call_info->media[i].index, &t) == PJ_SUCCESS) {
                        for (unsigned j = 0; j < t.specific_info_cnt; j++) {
                            if (t.spc_info[j].buffer[0]) {
                                switch (t.spc_info[j].type) {
                                case PJMEDIA_TRANSPORT_TYPE_SRTP:
                                    srtp = true;
                                    break;
                                case PJMEDIA_TRANSPORT_TYPE_ICE:
                                    ice = true;
                                    break;
                                }
                            }
                        }
                    }
					pjsua_stream_info psi;
					if (pjsua_call_get_stream_info(call_info->id, call_info->media[i].index, &psi) == PJ_SUCCESS) {
                        pjmedia_tp_proto proto = PJMEDIA_TP_PROTO_NONE;
                        if (psi.type == PJMEDIA_TYPE_AUDIO) {
                            proto = psi.info.aud.proto;
                            adder.AppendFormat(_T("%s@%dkHz %dkbit/s%s, "),
                                MSIP::PjToStr(&psi.info.aud.fmt.encoding_name),
                                psi.info.aud.fmt.clock_rate / 1000,
                                psi.info.aud.param->info.avg_bps / 1000,
                                psi.info.aud.fmt.channel_cnt == 2 ? _T(" Stereo") : _T("")
                            );
                        }
                        else if (psi.type == PJMEDIA_TYPE_VIDEO) {
                            proto = psi.info.vid.proto;
                            adder.AppendFormat(_T("%s %dkbit/s, "),
                                MSIP::PjToStr(&psi.info.vid.codec_info.encoding_name),
                                psi.info.vid.codec_param->enc_fmt.det.vid.max_bps / 1000
                            );
                        }
                        if (srtp) {
                            srtp = true;
                        }
                        if (proto & PJMEDIA_TP_PROTO_DTLS) {
                            cnt_srtp++;
                            adder.Append(_T("DTLS-SRTP, "));
                        } else if (srtp || (proto & PJMEDIA_TP_PROFILE_SRTP)) {
                            bool secure = false;
                            if (transport_tls != -1) {
                                pj_pool_t* tmp_pool = pjsua_pool_create("msip_ocs", 256, 256);
                                if (tmp_pool) {
                                    pjsua_acc_config acc_cfg;
                                    pjsua_acc_config_default(&acc_cfg);
                                    if (pjsua_acc_get_config(call_info->acc_id, tmp_pool, &acc_cfg) == PJ_SUCCESS) {
                                        secure = pj_strstr(&acc_cfg.id, &pj_str(";transport=tls"));
                                    }
                                    pj_pool_release(tmp_pool);
                                }
                            }
                            if (secure) {
                                cnt_srtp++;
                                adder.Append(_T("SRTP, "));
                            }
                            else {
                                adder.Append(_T("SRTP without TLS, "));
                            }
                        }
                        else {
                            adder.Append(_T("unencrypted, "));
                        }
                        if (ice) {
                            adder.Append(_T("ICE, "));
                        }
					}
                }
			}
		}
		if (cnt_srtp && cnt == cnt_srtp) {
			user_data->srtp = MSIP_SRTP;
		}
		else {
			user_data->srtp = MSIP_SRTP_DISABLED;
		}
		break;
	}
	if (!str->IsEmpty() && !adder.IsEmpty()) {
		str->AppendFormat(_T(" (%s)"), adder.Left(adder.GetLength() - 2));
	}
	if (call_info->state == PJSIP_INV_STATE_CALLING) {
		//--
		if (!accountSettings.cmdOutgoingCall.IsEmpty()) {
			CString params = sipuri.user;
			MSIP::RunCmd(URLMask(accountSettings.cmdOutgoingCall, &sipuri, call_info->acc_id, user_data), params);
		}
		//--
	}

	if (call_info->state == PJSIP_INV_STATE_CONFIRMED) {
		PostMessage(WM_TIMER, IDT_TIMER_CALL, NULL);
		SetTimer(IDT_TIMER_CALL, 1000, NULL);
		if (call_info->role == PJSIP_ROLE_UAS) {
			//--
			if (!accountSettings.cmdCallAnswer.IsEmpty()
				) {
				CString params = sipuri.user;
				MSIP::RunCmd(accountSettings.cmdCallAnswer, params);
			}
			if (call_info->rem_vid_cnt && !accountSettings.cmdCallAnswerVideo.IsEmpty()) {
				CString params = sipuri.user;
				MSIP::RunCmd(accountSettings.cmdCallAnswerVideo, params);
			}
			//--
		}
		//--
		if (!accountSettings.cmdCallStart.IsEmpty()) {
			CString params = sipuri.user;
			MSIP::RunCmd(accountSettings.cmdCallStart, params);
		}
		//--
		if (!user_data->commands.IsEmpty()) {
			SetTimer((UINT_PTR)call_info->id, 1000, (TIMERPROC)DTMFQueueTimerHandler);
		}
	}

	if (!accountSettings.singleMode) {
		if (call_info->state != PJSIP_INV_STATE_CONFIRMED) {
			if (call_info->state != PJSIP_INV_STATE_DISCONNECTED) {
				UpdateWindowText(*str, call_info->role == PJSIP_ROLE_UAS ? IDI_CALL_IN : IDI_CALL_OUT);
			}
		}
	}

	if (call_info->role == PJSIP_ROLE_UAC) {
		if (call_info->last_status == 180 && !call_info->media_cnt) {
			if (toneCalls.IsEmpty()) {
				PostMessage(WM_TIMER, IDT_TIMER_TONE, NULL);
				SetTimer(IDT_TIMER_TONE, 4500, NULL);
				toneCalls.AddTail(call_info->id);
			}
			else if (toneCalls.Find(call_info->id) == NULL) {
				toneCalls.AddTail(call_info->id);
			}
		}
		else {
			POSITION position = toneCalls.Find(call_info->id);
			if (position != NULL) {
				toneCalls.RemoveAt(position);
				if (toneCalls.IsEmpty()) {
					KillTimer(IDT_TIMER_TONE);
					PostMessage(UM_ON_PLAYER_STOP, 0, 0);
				}
			}
		}
	}

	bool doNotShowMessagesWindow =
		call_info->state == PJSIP_INV_STATE_INCOMING ||
		call_info->state == PJSIP_INV_STATE_EARLY ||
		call_info->state == PJSIP_INV_STATE_DISCONNECTED ||
		accountSettings.singleMode;

	if (user_data->autoAnswer) {
		if (!accountSettings.bringToFrontOnIncoming) {
			doNotShowMessagesWindow = true;
		}
	}
	MessagesContact* messagesContact = messagesDlg->AddTab(number,
		(!accountSettings.singleMode &&
			(call_info->state == PJSIP_INV_STATE_CONFIRMED
				|| call_info->state == PJSIP_INV_STATE_CONNECTING)
			)
		||
		(accountSettings.singleMode
			&&
			(
				(call_info->role == PJSIP_ROLE_UAC && call_info->state != PJSIP_INV_STATE_DISCONNECTED)
				||
				(call_info->role == PJSIP_ROLE_UAS &&
					(call_info->state == PJSIP_INV_STATE_CONFIRMED
						|| call_info->state == PJSIP_INV_STATE_CONNECTING)
					)
				))
		? TRUE : FALSE,
		call_info, user_data, doNotShowMessagesWindow, call_info->state == PJSIP_INV_STATE_DISCONNECTED
	);

	if (call_info->state == PJSIP_INV_STATE_CONFIRMED) {
		if (!accountSettings.singleMode && accountSettings.AC) {
			messagesDlg->OnMergeAll();
		}
	}

	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		if (call_info->role == PJSIP_ROLE_UAS && call_info->connect_duration.sec == 0 && call_info->connect_duration.msec == 0 && call_info->last_status != 486) {
			//-- missed call
			missed = true;
		}
	}

	if (messagesContact) {
		CString name = messagesContact->name;
		CString number = messagesContact->number + messagesContact->numberParameters + messagesContact->commands;
		if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
			messagesContact->mediaStatus = PJSUA_CALL_MEDIA_ERROR;
			if (call_info->role == PJSIP_ROLE_UAS && call_info->last_status == 486) {
				mainDlg->pageCalls->Add(call_info->call_id, number, name, MSIP_CALL_MISS, user_data);
			}
		}
		else {
			if (call_info->role == PJSIP_ROLE_UAS) {
				pageCalls->Add(call_info->call_id, number, name, MSIP_CALL_IN, user_data);
			}
			else {
				pageCalls->Add(call_info->call_id, number, name, MSIP_CALL_OUT, user_data);
			}
		}
	}
	if (accountSettings.singleMode) {
		if (call_info->state != PJSIP_INV_STATE_DISCONNECTED) {
			if (call_info->state != PJSIP_INV_STATE_CONFIRMED) {
				UpdateWindowText(*str, call_info->role == PJSIP_ROLE_UAS ? IDI_CALL_IN : IDI_CALL_OUT);
			}
			int tabN = 0;
			GotoTab(tabN);
			messagesDlg->OnChangeTab(call_info, user_data);
		}
	}

	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		messagesDlg->OnEndCall(call_info, user_data);
	}
	else {
		if (messagesContact && !str->IsEmpty()) {
			messagesDlg->AddMessage(messagesContact, *str, MSIP_MESSAGE_TYPE_SYSTEM,
				call_info->state == PJSIP_INV_STATE_INCOMING || call_info->state == PJSIP_INV_STATE_EARLY
			);
		}
	}

	bool hasCalls = messagesDlg->GetCallsCount();

	if (call_info->role == PJSIP_ROLE_UAS) {
		if (call_info->state != PJSIP_INV_STATE_INCOMING && call_info->state != PJSIP_INV_STATE_EARLY) {
			int count = ringinDlgs.GetCount();
			if (!count) {
				if (call_info->state != PJSIP_INV_STATE_DISCONNECTED || (call_info->state == PJSIP_INV_STATE_DISCONNECTED && call_info->connect_duration.sec == 0 && call_info->connect_duration.msec == 0)) {
					PlayerStop();
				}
			}
			else {
				for (int i = 0; i < count; i++) {
					RinginDlg* ringinDlg = ringinDlgs.GetAt(i);
					if (call_info->id == ringinDlg->call_id) {
						if (count == 1) {
							PlayerStop();
						}
						ringinDlgs.RemoveAt(i);
						ringinDlg->DestroyWindow();
						break;
					}
				}
			}
		}
	}

	if (call_info->state != PJSIP_INV_STATE_INCOMING &&
		call_info->state != PJSIP_INV_STATE_EARLY
		) {
		if (call_info->state != PJSIP_INV_STATE_DISCONNECTED) {
			if (messagesContact) {
				CString name = messagesContact->name;
				pageDialer->SetName(name);
			}
		}
	}

	user_data->CS.Unlock();

	// --delete user data
	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		if (user_data) {
			delete user_data;
		}
	}
	// --
	delete call_info;
	delete str;

	if (pageDialer->IsChild(&pageDialer->m_ButtonRec)) {
		pageDialer->m_ButtonRec.EnableWindow(hasCalls);
	}
	if (accountSettings.headsetSupport) {
		Hid::SetOffhookRing(hasCalls, ringinDlgs.GetCount());
	}
	if (hasCalls) {
#ifdef _GLOBAL_VIDEO
		SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED | ES_DISPLAY_REQUIRED | (mainDlg->previewWin ? ES_DISPLAY_REQUIRED : 0));
#else
		SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
#endif
	}
	else {
		SetThreadExecutionState(ES_CONTINUOUS);
	}
	return 0;
}

static void on_call_media_state(pjsua_call_id call_id)
{
	pjsua_call_info* call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}

	call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_info->id);
	if (!user_data) {
		user_data = new call_user_data(call_info->id);
		pjsua_call_set_user_data(call_info->id, user_data);
	}

	if (call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE
		|| call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD
		) {
		msip_conference_join(call_info);
		pjsua_conf_connect(call_info->conf_slot, 0);
		pjsua_conf_connect(0, call_info->conf_slot);
		//--
		user_data->CS.Lock();
		user_data->holdFrom = -1;
		if (user_data->recorder_id != PJSUA_INVALID_ID) {
			pjsua_conf_port_id rec_conf_port_id = pjsua_recorder_get_conf_port(user_data->recorder_id);
			pjsua_conf_connect(call_info->conf_slot, rec_conf_port_id);
			pjsua_conf_adjust_tx_level(rec_conf_port_id, 1);
		}
		user_data->CS.Unlock();

		//--
		::SetTimer(mainDlg->pageDialer->m_hWnd, IDT_TIMER_VU_METER, 100, NULL);
		//--
	}
	else {
		if (user_data->recorder_id != PJSUA_INVALID_ID) {
			pjsua_conf_port_id rec_conf_port_id = pjsua_recorder_get_conf_port(user_data->recorder_id);
			pjsua_conf_adjust_tx_level(rec_conf_port_id, 0);
		}
		msip_conference_leave(call_info, user_data, true);
		pjsua_conf_disconnect(call_info->conf_slot, 0);
		pjsua_conf_disconnect(0, call_info->conf_slot);
		call_deinit_tonegen(call_info->id);
		//--
		user_data->CS.Lock();
		user_data->holdFrom = msip_get_duration(&call_info->connect_duration);
		user_data->CS.Unlock();
		//--
	}

	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_MEDIA_STATE, (WPARAM)call_info, (LPARAM)user_data);
}

LRESULT CmainDlg::onCallMediaState(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_info* call_info = (pjsua_call_info*)wParam;
	call_user_data* user_data = (call_user_data*)lParam;

	CallTraceOnMediaState(call_info);

	messagesDlg->UpdateHoldButton(call_info);

	CString message;
	CString number = MSIP::PjToStr(&call_info->remote_info, TRUE);

	MessagesContact* messagesContact = messagesDlg->AddTab(number, FALSE, call_info, user_data, TRUE, TRUE);

	if (messagesContact) {
		if (call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD) {
			message = _T("Call on Remote Hold");
		}
		if (call_info->media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD) {
			message = _T("Call on Local Hold");
		}
		if (call_info->media_status == PJSUA_CALL_MEDIA_NONE) {
			message = _T("Call on Hold");
		}
		if (messagesContact->mediaStatus != PJSUA_CALL_MEDIA_ERROR && messagesContact->mediaStatus != call_info->media_status && call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE) {
			message = _T("Call is Active");
		}
		if (!message.IsEmpty()) {
			messagesDlg->AddMessage(messagesContact, Translate(message.GetBuffer()), MSIP_MESSAGE_TYPE_SYSTEM, TRUE);
		}
		messagesContact->mediaStatus = call_info->media_status;
		pageDialer->SetName();
	}
	if (call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE
		|| call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD
		) {
		onRefreshLevels(0, 0);
	}

	delete call_info;

	return 0;
}

static void on_call_media_event(pjsua_call_id call_id,
	unsigned med_idx,
	pjmedia_event* event)
{
	//char event_name[5];

	//PJ_LOG(5, (THIS_FILENAME, "Event %s",
		//pjmedia_fourcc_name(event->type, event_name)));

	//#if PJSUA_HAS_VIDEO
		//if (event->type == PJMEDIA_EVENT_FMT_CHANGED) {
		//	pjsua_call_info ci;
		//	pjsua_call_get_info(call_id, &ci);
		//	if ((ci.media[med_idx].type == PJMEDIA_TYPE_VIDEO) &&
		//		(ci.media[med_idx].dir & PJMEDIA_DIR_DECODING)) {
		//		pjsua_vid_win_id wid;
		//		pjmedia_rect_size size;
		//		pjsua_vid_win_info win_info;

		//		wid = ci.media[med_idx].stream.vid.win_in;
		//		pjsua_vid_win_get_info(wid, &win_info);

		//		size = event->data.fmt_changed.new_fmt.det.vid.size;
		//		if (size.w != win_info.size.w || size.h != win_info.size.h) {
		//			pjsua_vid_win_set_size(wid, &size);
		//			/* Re-arrange video windows */
		//			arrange_window(PJSUA_INVALID_ID);
		//		}
		//	}
		//}
	//#else
	//	PJ_UNUSED_ARG(call_id);
	//	PJ_UNUSED_ARG(med_idx);
	//	PJ_UNUSED_ARG(event);
	//#endif
}

static void on_incoming_call(pjsua_acc_id acc, pjsua_call_id call_id,
	pjsip_rx_data* rdata)
{
	pjsua_call_info* call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS) {
		return;
	}

	call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_info->id);
	if (!user_data) {
		user_data = new call_user_data(call_info->id);
		pjsua_call_set_user_data(call_info->id, user_data);
	}

	user_data->CS.Lock();

	if (accountSettings.forceCodec) {
		pjsua_call* call;
		pjsip_dialog* dlg;
		pj_status_t status;
		status = acquire_call("on_incoming_call()", call_id, &call, &dlg);
		if (status == PJ_SUCCESS) {
			pjmedia_sdp_neg_set_prefer_remote_codec_order(call->inv->neg, PJ_FALSE);
			pjsip_dlg_dec_lock(dlg);
		}
	}
    pjsip_generic_string_hdr* hsr;
    // -- diversion
    const pj_str_t headerDiversion = { "Diversion",9 };
    hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerDiversion, NULL);
    if (hsr) {
        CString str = MSIP::PjToStr(&hsr->hvalue, true);
        SIPURI sipuriDiversion;
        MSIP::ParseSIPURI(str, &sipuriDiversion);
        user_data->diversion = !sipuriDiversion.user.IsEmpty() ? sipuriDiversion.user : sipuriDiversion.domain;
    }
    // -- end diversion
    // -- caller id
    user_data->callerID = GetPAI(rdata);
    // -- end caller id
    // -- user agent
    const pj_str_t headerUserAgent = { "User-Agent",10 };
    hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerUserAgent, NULL);
    if (hsr) {
        user_data->userAgent = MSIP::PjToStr(&hsr->hvalue, true);
        int pos = user_data->userAgent.FindOneOf(_T("~+-"));
        if (pos) {
            user_data->userAgent = user_data->userAgent.Left(pos);
        }
    }
    // -- end user agent

    SIPURI sipuri;
    ParseCallSIPURI(call_info, user_data, &sipuri);

	if (!accountSettings.cmdIncomingCall.IsEmpty()) {
		CString params = sipuri.user;
		MSIP::RunCmd(accountSettings.cmdIncomingCall, params);
	}
	//--
	//--
	bool busy = false;
	pjsua_call_id call_ids[PJSUA_MAX_CALLS];
	unsigned calls_count = PJSUA_MAX_CALLS;
	unsigned calls_count_cmp = 0;
	if (pjsua_enum_calls(call_ids, &calls_count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < calls_count; ++i) {
			pjsua_call_info call_info_curr;
			if (pjsua_call_get_info(call_ids[i], &call_info_curr) == PJ_SUCCESS) {
                call_user_data* user_data_curr = (call_user_data*)pjsua_call_get_user_data(call_info_curr.id);
                SIPURI sipuri_curr;
                ParseCallSIPURI(&call_info_curr, user_data_curr, &sipuri_curr);
				if (call_info_curr.id != call_info->id &&
					sipuri.user + _T("@") + sipuri.domain == sipuri_curr.user + _T("@") + sipuri_curr.domain
					) {
					busy = true;
					break;
				}
                if (user_data) {
                    user_data->CS.Lock();
                    if (!user_data_curr->hangup && call_info_curr.state != PJSIP_INV_STATE_DISCONNECTED) {
                        calls_count_cmp++;
                    }
                    user_data->CS.Unlock();
                }
                else {
                    if (call_info_curr.state != PJSIP_INV_STATE_DISCONNECTED) {
                        calls_count_cmp++;
                    }
                }
			}
		}
	}
	if (busy) {
		// 486 Busy Here
		msip_call_busy(call_info->id, _T("Call already exists"));
		user_data->hidden = true;
	}
	else if ((!accountSettings.callWaiting && calls_count_cmp > 1) || (accountSettings.maxConcurrentCalls > 0 && calls_count_cmp > accountSettings.maxConcurrentCalls)) {
		// 486 Busy Here
		msip_call_busy(call_info->id, _T("Active calls limit"));
		user_data->hidden = true;
	}
	else if (!mainDlg->callIdIncomingIgnore.IsEmpty() && mainDlg->callIdIncomingIgnore == MSIP::PjToStr(&call_info->call_id)) {
		pjsua_call_answer(call_info->id, 487, NULL, NULL);
		user_data->hidden = true;
	}
	else {
		bool reject = false;
		CString reason;
		if (accountSettings.denyIncoming == _T("all")) {
			reject = true;
		}
		else if (accountSettings.denyIncoming == _T("button")) {
			reject = accountSettings.DND;
			reason = _T("Do Not Disturb");
		}
		else if (accountSettings.denyIncoming == _T("user")) {
			SIPURI sipuri_curr;
			MSIP::ParseSIPURI(MSIP::PjToStr(&call_info->local_info, TRUE), &sipuri_curr);
			if (sipuri_curr.user != get_account_username()) {
				reject = true;
			}
		}
		else if (accountSettings.denyIncoming == _T("domain")) {
			SIPURI sipuri_curr;
			MSIP::ParseSIPURI(MSIP::PjToStr(&call_info->local_info, TRUE), &sipuri_curr);
			if (accountSettings.accountId) {
				if (sipuri_curr.domain != get_account_domain()) {
					reject = true;
				}
			}
		}
		else if (accountSettings.denyIncoming == _T("remotedomain")) {
			if (accountSettings.accountId) {
				if (sipuri.domain != get_account_domain()) {
					reject = true;
				}
			}
		}
		else if (accountSettings.denyIncoming == _T("userdomain")) {
			SIPURI sipuri_curr;
			MSIP::ParseSIPURI(MSIP::PjToStr(&call_info->local_info, TRUE), &sipuri_curr);
			if (sipuri_curr.user != get_account_username()) {
				reject = true;
			}
			else {
				CString domain = get_account_domain();
				if (domain != _T("") && sipuri_curr.domain != domain) {
					reject = true;
				}
			}
		}
		if (reject) {
			if (reason.IsEmpty()) {
				reason = _T("Denied");
			}
			msip_call_busy(call_info->id, reason);
			user_data->hidden = true;
		}
		else {
			CString forwardingDestination = accountSettings.forwardingNumber;
			forwardingDestination.Trim();
			if (accountSettings.FWD && !forwardingDestination.IsEmpty()) {
				CString contactTarget;
				CString failure;
				if (redirect_incoming_call(call_info, forwardingDestination, contactTarget, failure)) {
					CString traceDestination = forwardingDestination;
					if (traceDestination.CompareNoCase(contactTarget) != 0) {
						traceDestination += _T(" -> ") + contactTarget;
					}
					post_call_forwarding_result(true, traceDestination);
					user_data->hidden = true;
					user_data->CS.Unlock();
					delete call_info;
					return;
				}
				post_call_forwarding_result(false, forwardingDestination, failure);
			}
			bool autoAnswer = false;
			int autoAnswerDelay = accountSettings.autoAnswerDelay;
			if (accountSettings.autoAnswer == _T("all")) {
				autoAnswer = true;
			}
			else if (accountSettings.autoAnswer == _T("button")) {
				autoAnswer = accountSettings.AA;
			}
			else if (accountSettings.autoAnswer == _T("header")) {
				//--
				pjsip_generic_string_hdr* hsr = NULL;
				const pj_str_t header = pj_str("X-AUTOANSWER");
				hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &header, NULL);
				if (hsr) {
					CString autoAnswerValue = MSIP::PjToStr(&hsr->hvalue, TRUE);
					autoAnswerValue.MakeLower();
					if (autoAnswerValue == _T("true") || autoAnswerValue == _T("1")) {
						autoAnswer = true;
					}
				}
				//--
				if (!autoAnswer) {
					pjsip_generic_string_hdr* hsr = NULL;
					const pj_str_t header = pj_str("Call-Info");
					hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &header, NULL);
					if (hsr) {
						CString callInfoValue = MSIP::PjToStr(&hsr->hvalue, TRUE);
						callInfoValue.MakeLower();
						if (callInfoValue.Find(_T("auto answer")) != -1) {
							autoAnswer = true;
						}
						else {
							CAtlRegExp<> regex;
							REParseError parseStatus = regex.Parse(_T("answer-after={[0-9]+}"), true);
							if (parseStatus == REPARSE_ERROR_OK) {
								CAtlREMatchContext<> mc;
								if (regex.Match(callInfoValue, &mc) && mc.m_uNumGroups == 1) {
									const CAtlREMatchContext<>::RECHAR* szStart = 0;
									const CAtlREMatchContext<>::RECHAR* szEnd = 0;
									mc.GetMatch(0, &szStart, &szEnd);
									ptrdiff_t nLength = szEnd - szStart;
									CStringA text(szStart, nLength);
									autoAnswerDelay = atoi(text);
									autoAnswer = true;
								}
							}
						}
					}
				}
			}

			if (autoAnswer && !accountSettings.autoAnswerNumber.IsEmpty()) {
				bool found = false;
				int pos = 0;
				CString resToken = accountSettings.autoAnswerNumber.Tokenize(_T(";|"), pos);
				while (!resToken.IsEmpty()) {
					resToken.Trim();
					if (!resToken.IsEmpty()) {
						CMask mask;
						if (mask.WildMatch(resToken, sipuri.user, _T(""))) {
							found = true;
							break;
						}
					}
					resToken = accountSettings.autoAnswerNumber.Tokenize(_T(";|"), pos);
				}
				if (!found) {
					autoAnswer = false;
				}
			}
			if (autoAnswer) {
				if (autoAnswerDelay > 0) {
					if (mainDlg->autoAnswerTimerCallId == PJSUA_INVALID_ID) {
						mainDlg->autoAnswerTimerCallId = call_info->id;
						mainDlg->SetTimer(IDT_TIMER_AUTOANSWER, autoAnswerDelay * 1000, NULL);
					}
				}
				else {
					user_data->autoAnswer = true;
				}
			}
			PostMessage(mainDlg->m_hWnd, UM_ON_INCOMING_CALL, (WPARAM)call_info, (LPARAM)user_data);
		}
	}
	user_data->CS.Unlock();
}

LRESULT CmainDlg::onIncomingCall(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_info* call_info = (pjsua_call_info*)wParam;
	call_user_data* user_data = (call_user_data*)lParam;

	user_data->CS.Lock();

	SIPURI sipuri;
    ParseCallSIPURI(call_info, user_data, &sipuri);

    CString numberOriginal;
    GetNameForCall(sipuri, user_data, numberOriginal);

	accountSettings.lastCallNumber = sipuri.user;
	accountSettings.lastCallHasVideo = false;

	bool autoAnswer = user_data->autoAnswer;
	user_data->autoAnswer = false;
	bool playBeep = false;

	if (autoAnswer && AutoAnswer(call_info->id)) {
		}
		else {
            bool createRinging = true;
            if (createRinging) {
                PostMessage(UM_CREATE_RINGING, (WPARAM)call_info->id, NULL);
            }
			pjsua_call_answer(call_info->id, 180, NULL, NULL);
			if (messagesDlg->GetCallsCount()) {

				playBeep = true;
			}
			else {
					if (!accountSettings.ringtone.GetLength()) {
						onPlayerPlay(MSIP_SOUND_RINGTONE, 0);
					}
					else {
						onPlayerPlay(MSIP_SOUND_CUSTOM, (LPARAM)&accountSettings.ringtone);
					}
			}
			if (accountSettings.headsetSupport) {
				Hid::SetRing(true);
			}
			//--
			if (!accountSettings.cmdCallRing.IsEmpty()) {
				CString params = sipuri.user;
				MSIP::RunCmd(accountSettings.cmdCallRing, params);
			}
			//--
		}
	if (accountSettings.localDTMF && playBeep) {
		onPlayerPlay(MSIP_SOUND_RINGIN2, 0);
	}

	user_data->CS.Unlock();
	delete call_info;
	return 0;
}

static void on_nat_detect(const pj_stun_nat_detect_result * res)
{
	if (res->status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILENAME, "NAT detection failed", res->status);
	}
	else {
		if (res->nat_type == PJ_STUN_NAT_TYPE_SYMMETRIC) {
			if (IsWindow(mainDlg->m_hWnd)) {
				CString message;
				//				pjsua_acc_config acc_cfg;
				//				pj_pool_t *pool;
				//				pool = pjsua_pool_create("acc_cfg-pjsua", 1000, 1000);
				//				if (pool) {
				//					pjsua_acc_id ids[PJSUA_MAX_ACC];
				//					unsigned count = PJSUA_MAX_ACC;
				//					if (pjsua_enum_accs(ids, &count) == PJ_SUCCESS) {
				//						for (unsigned i = 0; i < count; i++) {
				//							if (pjsua_acc_get_config(ids[i], pool, &acc_cfg) == PJ_SUCCESS) {
				//								acc_cfg.sip_stun_use = PJSUA_STUN_USE_DISABLED;
				//								acc_cfg.media_stun_use = PJSUA_STUN_USE_DISABLED;
				//								if (pjsua_acc_modify(ids[i], &acc_cfg) == PJ_SUCCESS) {
				//									message = _T("STUN was automatically disabled.");
//									message.Append(_T(" For more info visit MicroSIP website, help page."));
//
//								}
//							}
//						}
//					}
//					pj_pool_release(pool);
//				}
				message = _T("The softphpne may not work properly with enabled STUN and your internet connection.");
				mainDlg->BaloonPopup(Translate(_T("Symmetric NAT detected!")), Translate(message.GetBuffer()));
			}
		}
		PJ_LOG(3, (THIS_FILENAME, "NAT detected as %s", res->nat_type_name));
	}
}

void on_buddy_state(pjsua_buddy_id buddy_id)
{
	if (!IsWindow(mainDlg->m_hWnd)) {
		return;
	}
	mainDlg->PostMessage(UM_ON_BUDDY_STATE, (WPARAM)buddy_id);
}

LRESULT CmainDlg::onBuddyState(WPARAM wParam, LPARAM lParam)
{
	if (isSubscribed && is_pjsua_running()) {
		pjsua_buddy_id buddy_id = wParam;
		pjsua_buddy_info buddy_info;
		if (pjsua_buddy_is_valid(buddy_id) && pjsua_buddy_get_info(buddy_id, &buddy_info) == PJ_SUCCESS) {
			int image;
			bool ringing = false;
			CString info;
			switch (buddy_info.status)
			{
			case PJSUA_BUDDY_STATUS_OFFLINE:
				image = MSIP_CONTACT_ICON_OFFLINE;
				break;
			case PJSUA_BUDDY_STATUS_ONLINE:
				if (PJRPID_ACTIVITY_UNKNOWN && !buddy_info.rpid.activity) {
					image = MSIP_CONTACT_ICON_ON_THE_PHONE;
				}
				else if (buddy_info.rpid.activity == PJRPID_ACTIVITY_AWAY)
				{
					image = MSIP_CONTACT_ICON_AWAY;
				}
				else if (buddy_info.rpid.activity == PJRPID_ACTIVITY_BUSY) {
					image = MSIP_CONTACT_ICON_BUSY;
				}
				else {
					image = MSIP_CONTACT_ICON_ONLINE;
				}
				break;
			default:
				image = MSIP_CONTACT_ICON_UNKNOWN;
			}
			info = MSIP::PjToStr(&buddy_info.status_text);
			if (buddy_info.status == PJSUA_BUDDY_STATUS_ONLINE) {
				if (info == _T("On the phone")) {
					image = MSIP_CONTACT_ICON_ON_THE_PHONE;
				}
				else if (MSIP::PjToStr(&buddy_info.status_text).Left(4) == _T("Ring")) {
					image = MSIP_CONTACT_ICON_ON_THE_PHONE;
					ringing = true;
				}
			}
			CString* buddyNumber = (CString*)pjsua_buddy_get_user_data(buddy_id);
			//--
			pageContacts->PresenceReceived(buddyNumber, image, ringing, &info);
			pageDialer->PresenceReceived(buddyNumber, image, ringing);
		}
	}
	return 0;
}

static void on_pager2(pjsua_call_id call_id, const pj_str_t * from, const pj_str_t * to, const pj_str_t * contact, const pj_str_t * mime_type, const pj_str_t * body, pjsip_rx_data * rdata, pjsua_acc_id acc_id)
{
	if (pj_strcmp2(mime_type, "text/plain") != 0 || accountSettings.disableMessaging) {
		return;
	}
	if (IsWindow(mainDlg->m_hWnd)) {
		CString* number = new CString();
		CString* message = new CString();
		number->SetString(MSIP::PjToStr(from, TRUE));
		message->SetString(MSIP::PjToStr(body, TRUE));
		message->Trim();
        call_user_data user_data(PJSUA_INVALID_ID);
        user_data.callerID = GetPAI(rdata);
        SIPURI sipuri;
        ParseCallSIPURI(*number, &user_data, &sipuri);
        //-- fix domain
		if (accountSettings.accountId) {
			if (MSIP::IsIP(sipuri.domain)) {
				sipuri.domain = get_account_domain();
			}
		}
        //--
        number->SetString(MSIP::BuildSIPURI(&sipuri));
		mainDlg->PostMessage(UM_ON_PAGER, (WPARAM)number, (LPARAM)message);
	}
}

static void on_pager_status2(pjsua_call_id call_id, const pj_str_t * to, const pj_str_t * body, void* user_data, pjsip_status_code status, const pj_str_t * reason, pjsip_tx_data * tdata, pjsip_rx_data * rdata, pjsua_acc_id acc_id)
{
	if (status != 200) {
		if (IsWindow(mainDlg->m_hWnd)) {
			CString* number = new CString();
			CString* message = new CString();
			number->SetString(MSIP::PjToStr(to, TRUE));
			message->SetString(MSIP::PjToStr(reason, TRUE));
			message->Trim();
            call_user_data user_data(PJSUA_INVALID_ID);
            user_data.callerID = GetPAI(rdata);
            SIPURI sipuri;
            ParseCallSIPURI(*number, &user_data, &sipuri);
            //-- fix domain
            if (accountSettings.accountId) {
                if (MSIP::IsIP(sipuri.domain)) {
                    sipuri.domain = get_account_domain();
                }
            }
            //--
            number->SetString(MSIP::BuildSIPURI(&sipuri));
			mainDlg->PostMessage(UM_ON_PAGER_STATUS, (WPARAM)number, (LPARAM)message);
		}
	}
}

static void on_call_transfer_status(pjsua_call_id call_id,
	int status_code,
	const pj_str_t * status_text,
	pj_bool_t final,
	pj_bool_t * p_cont)
{
	pjsua_call_info* call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}

	call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_info->id);
	if (!user_data) {
		user_data = new call_user_data(call_info->id);
		pjsua_call_set_user_data(call_info->id, user_data);
	}

	CString* str = new CString();
	str->Format(_T("%s: %s"),
		Translate(_T("Call Transfer")),
		MSIP::PjToStr(status_text, TRUE)
	);
	if (final) {
		str->AppendFormat(_T(" [%s]"), Translate(_T("Final")));
	}

	if (status_code / 100 == 2) {
		*p_cont = PJ_FALSE;
	}

	call_info->last_status = (pjsip_status_code)status_code;

	call_info->call_id.ptr = (char*)user_data;
	call_info->call_id.slen = 0;

	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_TRANSFER_STATUS, (WPARAM)call_info, (LPARAM)str);
}

LRESULT CmainDlg::onCallTransferStatus(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_info* call_info = (pjsua_call_info*)wParam;
	call_user_data* user_data = (call_user_data*)call_info->call_id.ptr;
	CString* str = (CString*)lParam;


	MessagesContact* messagesContact = NULL;
	CString number = MSIP::PjToStr(&call_info->remote_info, TRUE);
	messagesContact = mainDlg->messagesDlg->AddTab(number, FALSE, call_info, user_data, TRUE, TRUE);
	if (messagesContact) {
		mainDlg->messagesDlg->AddMessage(messagesContact, *str);
	}
	if (call_info->last_status / 100 == 2) {
		if (messagesContact) {
			messagesDlg->AddMessage(messagesContact, Translate(_T("Call transfered successfully, disconnecting call")));
		}
		msip_call_hangup_fast(call_info->id);
	}
	delete call_info;
	delete str;
	return 0;
}

static void on_call_transfer_request2(pjsua_call_id call_id, const pj_str_t * dst, pjsip_status_code * code, pjsua_call_setting * opt)
{
	SIPURI sipuri;
	MSIP::ParseSIPURI(MSIP::PjToStr(dst, TRUE), &sipuri);
	pj_bool_t cont;
	CString number = sipuri.user;
	if (number.IsEmpty()) {
		number = sipuri.domain;
	}
	else if (!accountSettings.accountId || sipuri.domain != get_account_domain()) {
		number.Append(_T("@") + sipuri.domain);
	}
	char* buf = MSIP::WideCharToPjStr(number);
	on_call_transfer_status(call_id,
		0,
		&pj_str(buf),
		PJ_FALSE,
		&cont);
	free(buf);
	//--
	if (!code) {
		// if our function call
		return;
	}
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS || call_info.state != PJSIP_INV_STATE_CONFIRMED) {
		*code = PJSIP_SC_DECLINE;
	}
	if (*code != PJSIP_SC_DECLINE) {
		// deny transfer if we already have a call with same dest address
		pjsua_call_id call_ids[PJSUA_MAX_CALLS];
		unsigned calls_count = PJSUA_MAX_CALLS;
		if (pjsua_enum_calls(call_ids, &calls_count) == PJ_SUCCESS) {
			for (unsigned i = 0; i < calls_count; ++i) {
				pjsua_call_info call_info_curr;
				if (pjsua_call_get_info(call_ids[i], &call_info_curr) == PJ_SUCCESS) {
                    call_user_data* user_data_curr = (call_user_data*)pjsua_call_get_user_data(call_info_curr.id);
                    SIPURI sipuri_curr;
                    ParseCallSIPURI(&call_info_curr, user_data_curr, &sipuri_curr);
					if (sipuri.user + _T("@") + sipuri.domain == sipuri_curr.user + _T("@") + sipuri_curr.domain
						) {
						*code = PJSIP_SC_DECLINE;
						break;
					}
				}
			}
		}
	}
}

static void on_call_replace_request2(pjsua_call_id call_id, pjsip_rx_data * rdata, int* st_code, pj_str_t * st_text, pjsua_call_setting * opt)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
		if (!call_info.rem_vid_cnt) {
			opt->vid_cnt = 0;
		}
	}
	else {
		opt->vid_cnt = 0;
	}
}

static void on_call_replaced(pjsua_call_id old_call_id, pjsua_call_id new_call_id)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(new_call_id, &call_info) == PJ_SUCCESS) {
		on_call_transfer_request2(old_call_id, &call_info.remote_info, NULL, NULL);
	}
}

static void on_mwi_info(pjsua_acc_id acc_id, pjsua_mwi_info * mwi_info)
{
	bool hasMail = false;
	if (mwi_info->rdata->msg_info.ctype) {
		const pjsip_ctype_hdr* ctype = mwi_info->rdata->msg_info.ctype;
		if (pj_strcmp2(&ctype->media.type, "application") != 0 || pj_strcmp2(&ctype->media.subtype, "simple-message-summary") != 0) {
			return;
		}
	}
	if (!mwi_info->rdata->msg_info.msg->body || !mwi_info->rdata->msg_info.msg->body->len) {
		return;
	}
	pjsip_msg_body* body = mwi_info->rdata->msg_info.msg->body;
	LPARAM lParam = 0;
	pj_scanner scanner;
	pj_scan_init(&scanner, (char*)body->data, body->len, PJ_SCAN_AUTOSKIP_WS, 0);
	while (!pj_scan_is_eof(&scanner)) {
		pj_str_t key;
		pj_scan_get_until_chr(&scanner, ":", &key);
		pj_strtrim(&key);
		if (key.slen && !pj_scan_is_eof(&scanner)) {
			scanner.curptr++;
			pj_str_t value;
			pj_scan_get_until_chr(&scanner, "\r\n", &value);
			pj_strtrim(&value);
			if (pj_stricmp2(&key, "Messages-Waiting") == 0) {
				hasMail = pj_stricmp2(&value, "yes") == 0;
				break;
			}
		}
	}
	pj_scan_fini(&scanner);
	PostMessage(mainDlg->m_hWnd, UM_ON_MWI_INFO, (WPARAM)hasMail, lParam);
}

LRESULT CmainDlg::onMWIInfo(WPARAM wParam, LPARAM lParam)
{
	bool hasMail = (bool)wParam;
	pageDialer->UpdateVoicemailButton(hasMail);
	return 0;
}

static void on_dtmf_digit(pjsua_call_id call_id, int digit)
{
	char signal[2];
	signal[0] = digit;
	signal[1] = 0;
	call_play_digit(-1, signal);
}

static void on_call_tsx_state(pjsua_call_id call_id, pjsip_transaction * tsx, pjsip_event * e)
{
	if (tsx->role == PJSIP_ROLE_UAS) {
		const pjsip_method update_method = {
			PJSIP_OTHER_METHOD,
			{ "UPDATE", 6 }
		};
		if (tsx->method.id == PJSIP_INVITE_METHOD || pjsip_method_cmp(&tsx->method, &update_method) == 0) {
			/*
			* Handle INVITE/UPDATE method.
			*/
			if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
				pjsip_rx_data* rdata = e->body.rx_msg.rdata;
				// --
				pjsip_generic_string_hdr* hsr;
				const pj_str_t headerCallerID = { "P-Asserted-Identity",19 };
				CString headerName = _T("P-Asserted-Identity");
				hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerCallerID, NULL);
				if (!hsr) {
					const pj_str_t headerCallerID = { "Remote-Party-Id",15 };
					headerName = _T("Remote-Party-Id");
					hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerCallerID, NULL);
				}
				if (hsr) {
					call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_id);
					if (user_data) {
						bool identityChanged = false;
						CString oldCallerID;
						CString newCallerID = normalize_caller_identity(&hsr->hvalue);
						user_data->CS.Lock();
						oldCallerID = user_data->callerID;
						if (oldCallerID.CompareNoCase(newCallerID) != 0) {
							identityChanged = true;
							user_data->callerIDPrev = oldCallerID;
							user_data->callerIDHeader = headerName;
							user_data->callerID = newCallerID;
							// Invalidate cached name so the visible label is recomputed from updated signaling.
							user_data->name.Empty();
						}
						user_data->CS.Unlock();
						if (identityChanged) {
							pjsua_call_info call_info;
							if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
								CString oldEffectiveIdentity = effective_call_identity_for_log(&call_info, oldCallerID);
								CString newEffectiveIdentity = effective_call_identity_for_log(&call_info, newCallerID);
								CStringA oldEffectiveIdentityA = MSIP::Utf8EncodeUni(oldEffectiveIdentity);
								CStringA newHeaderIdentityA = MSIP::Utf8EncodeUni(newCallerID);
								CStringA newEffectiveIdentityA = MSIP::Utf8EncodeUni(newEffectiveIdentity);
								PJ_LOG(4, (THIS_FILENAME,
									"Call %d connected identity changed: old_effective='%s' new_header='%s' new_effective='%s'; cached name invalidated",
									call_id,
									oldEffectiveIdentityA.GetString(),
									newHeaderIdentityA.GetString(),
									newEffectiveIdentityA.GetString()));
							}
							PostMessage(mainDlg->m_hWnd, UM_TAB_ICON_UPDATE, (WPARAM)call_id, 1);
						}
					}
				}
				// -- end reason
			}
			return;
		}
	}
	const pjsip_method info_method = {
		PJSIP_OTHER_METHOD,
		{ "INFO", 4 }
	};
	if (pjsip_method_cmp(&tsx->method, &info_method) == 0) {
		/*
		* Handle INFO method.
		*/
		if (tsx->role == PJSIP_ROLE_UAS && tsx->state == PJSIP_TSX_STATE_TRYING) {
			if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
				pjsip_rx_data* rdata = e->body.tsx_state.src.rdata;
				pjsip_msg_body* body = rdata->msg_info.msg->body;
				int code = 0;
				if (body && body->len
					&& pj_strcmp2(&body->content_type.type, "application") == 0
					&& pj_strcmp2(&body->content_type.subtype, "dtmf-relay") == 0) {
					code = 400;
					pj_scanner scanner;
					pj_scan_init(&scanner, (char*)body->data, body->len, PJ_SCAN_AUTOSKIP_WS, 0);
					char digit;
					int duration = 250;
					while (!pj_scan_is_eof(&scanner)) {
						pj_str_t key;
						pj_scan_get_until_chr(&scanner, "=", &key);
						pj_strtrim(&key);
						if (key.slen && !pj_scan_is_eof(&scanner)) {
							scanner.curptr++;
							pj_str_t value;
							pj_scan_get_until_chr(&scanner, "\r\n", &value);
							pj_strtrim(&value);
							if (pj_stricmp2(&key, "Signal") == 0) {
								if (value.slen == 1) {
									digit = *value.ptr;
									code = 200;
								}
							}
							else if (pj_stricmp2(&key, "Duration") == 0) {
								int res = 0;
								for (int i = 0; i < (unsigned)value.slen; ++i) {
									res = res * 10 + (value.ptr[i] - '0');
									res = res;
								}
								if (res >= 100 || res <= 5000) {
									duration = res;
								}
							}
						}
					}
					pj_scan_fini(&scanner);
					if (code == 200) {
						on_dtmf_digit(-1, digit);
					}
				}
				else if (!body || !body->len) {
					/* 200/OK */
					code = 200;
				}
				if (code) {
					/* Answer incoming INFO */
					pjsip_tx_data* tdata;
					if (pjsip_endpt_create_response(tsx->endpt, rdata,
						code, NULL, &tdata) == PJ_SUCCESS
						) {
						pjsip_tsx_send_msg(tsx, tdata);
					}
				}
			}
		}
		return;
	}
	const pjsip_method cancel_method = {
		PJSIP_CANCEL_METHOD,
		{ "CANCEL", 6 }
	};
	if (pjsip_method_cmp(&tsx->method, &cancel_method) == 0) {
		/*
		* Handle CANCEL method.
		*/
		if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
			pjsip_rx_data* rdata = e->body.rx_msg.rdata;
			// -- reason
			const pj_str_t headerReason = { "Reason",6 };
			pjsip_generic_string_hdr* hsr;
			hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerReason, NULL);
			if (hsr) {
				CString str = MSIP::PjToStr(&hsr->hvalue, true);
				int pos = str.Find(_T("text=\""));
				if (pos != -1) {
					str = str.Mid(pos + 6);
					pos = str.Find(_T("\""));
					if (pos != -1) {
						str = str.Left(pos);
						call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_id);
						if (user_data) {
							user_data->CS.Lock();
							user_data->reason = str;
							user_data->CS.Unlock();
						}
					}
				}
			}
			// -- end reason
		}
		return;
	}
	if (tsx->state == PJSIP_TSX_STATE_COMPLETED) {
		// display declined REFER status
		const pjsip_method refer_method = {
			PJSIP_OTHER_METHOD,
			{ "REFER", 5 }
		};
		if (pjsip_method_cmp(&tsx->method, &refer_method) == 0 && tsx->status_code / 100 != 2) {
			pj_bool_t cont;
			on_call_transfer_status(call_id,
				tsx->status_code,
				&tsx->status_text,
				PJ_FALSE,
				&cont);
		}
	}
}

static pjsip_redirect_op on_call_redirected(pjsua_call_id call_id,
	const pjsip_uri * target,
	const pjsip_event * e)
{
	return PJSIP_REDIRECT_ACCEPT_REPLACE;
}

static DWORD WINAPI NetworkChangeThread(LPVOID lpParam)
{
	while (NotifyAddrChange(NULL, NULL) == NO_ERROR) {
		PostMessage(mainDlg->m_hWnd, UM_NETWORK_CHANGE, 0, 0);
	}
	return 0;
}

CmainDlg::~CmainDlg(void)
{
	if (m_freepbxFooter) {
		m_freepbxFooter->DestroyWindow();
		delete m_freepbxFooter;
		m_freepbxFooter = NULL;
	}
	if (m_gdiplusToken) {
		Gdiplus::GdiplusShutdown(m_gdiplusToken);
		m_gdiplusToken = 0;
	}
}

void CmainDlg::OnDestroy()
{
	StopDialTone();
	AppBarRemove();
	if (mmNotificationClient) {
		delete mmNotificationClient;
	}
	WTSUnRegisterSessionNotification(m_hWnd);

	PJDestroy(true);

	accountSettings.SettingsSave();

	RemoveJumpList();
	if (tnd.hWnd) {
		Shell_NotifyIcon(NIM_DELETE, &tnd);
	}
	UnloadLangPackModule();

	CBaseDialog::OnDestroy();
}

void CmainDlg::PostNcDestroy()
{
	CBaseDialog::PostNcDestroy();
	delete this;
}

void CmainDlg::DoDataExchange(CDataExchange * pDX)
{
	CBaseDialog::DoDataExchange(pDX);
	//	DDX_Control(pDX, IDD_MAIN, *mainDlg);
	DDX_Control(pDX, IDC_MAIN_MENU, m_ButtonMenu);
	DDX_Control(pDX, IDC_MAIN_TAB, m_tabCtrl);
}

BEGIN_MESSAGE_MAP(CmainDlg, CBaseDialog)
	ON_WM_CREATE()
	ON_WM_SYSCOMMAND()
	ON_WM_QUERYENDSESSION()
	ON_WM_TIMER()
	ON_WM_MOVE()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_EXITSIZEMOVE()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_MOVING()
	ON_WM_GETMINMAXINFO()
	ON_WM_WINDOWPOSCHANGING()
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
	ON_WM_CONTEXTMENU()
	ON_WM_DEVICECHANGE()
	ON_WM_WTSSESSION_CHANGE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_MAIN_MENU, OnBnClickedMenu)
	ON_MESSAGE(UM_UPDATEWINDOWTEXT, OnUpdateWindowText)
	ON_MESSAGE(UM_NOTIFYICON, onTrayNotify)
	ON_MESSAGE(UM_CREATE_RINGING, onCreateRingingDlg)
	ON_MESSAGE(UM_REFRESH_LEVELS, onRefreshLevels)
	ON_MESSAGE(UM_ON_REG_STATE2, onRegState2)
	ON_MESSAGE(UM_DIAL_TONE_OPTIONS, onDialToneOptions)
	ON_MESSAGE(UM_ON_CALL_STATE, onCallState)
	ON_MESSAGE(UM_ON_INCOMING_CALL, onIncomingCall)
	ON_MESSAGE(UM_CALL_FORWARDING_RESULT, onCallForwardingResult)
	ON_MESSAGE(UM_ON_MWI_INFO, onMWIInfo)
	ON_MESSAGE(UM_ON_CALL_MEDIA_STATE, onCallMediaState)
	ON_MESSAGE(UM_ON_CALL_TRANSFER_STATUS, onCallTransferStatus)
	ON_MESSAGE(UM_ON_PLAYER_STOP, onPlayerStop)
	ON_MESSAGE(UM_ON_COMMAND_LINE, onCommandLine)
	ON_MESSAGE(UM_ON_PAGER, onPager)
	ON_MESSAGE(UM_ON_PAGER_STATUS, onPagerStatus)
	ON_MESSAGE(UM_ON_BUDDY_STATE, onBuddyState)
	ON_MESSAGE(UM_USERS_DIRECTORY, onUsersDirectoryLoaded)
	ON_MESSAGE(UM_CUSTOM, onCustomLoaded)
	ON_MESSAGE(UM_NETWORK_CHANGE, OnNetworkChange)
	ON_MESSAGE(UM_RESTART, OnRestart)
	ON_MESSAGE(WM_POWERBROADCAST, OnPowerBroadcast)
	ON_MESSAGE(WM_COPYDATA, onCopyData)
	ON_MESSAGE(UM_CALL_ANSWER, onCallAnswer)
	ON_MESSAGE(UM_CALL_HANGUP, onCallHangup)
	ON_MESSAGE(UM_TAB_ICON_UPDATE, onTabIconUpdate)
	ON_MESSAGE(UM_ON_ACCOUNT, OnAccount)
	ON_COMMAND(ID_ACCOUNT_ADD, OnMenuAccountAdd)
	ON_COMMAND_RANGE(ID_ACCOUNT_EDIT_RANGE, ID_ACCOUNT_EDIT_RANGE + 99, OnMenuAccountEdit)
	ON_COMMAND_RANGE(ID_ACCOUNT_CHANGE_RANGE, ID_ACCOUNT_CHANGE_RANGE + 99, OnMenuAccountChange)
	ON_COMMAND(ID_ACCOUNT_EDIT_LOCAL, OnMenuAccountLocalEdit)
	ON_COMMAND_RANGE(ID_CUSTOM_RANGE, ID_CUSTOM_RANGE + 99, OnMenuCustomRange)
	ON_COMMAND(ID_UPDATES, OnCheckUpdates)
	ON_MESSAGE(UM_UPDATE_CHECKER_LOADED, OnUpdateCheckerLoaded)
	ON_COMMAND(ID_SETTINGS, OnMenuSettings)
	ON_COMMAND(ID_SHORTCUTS, OnMenuShortcuts)
	ON_COMMAND(ID_ALWAYS_ON_TOP, OnMenuAlwaysOnTop)
	ON_COMMAND(ID_DARK_MODE, OnMenuDarkMode)
	ON_COMMAND(ID_BRANDING, OnMenuBranding)
	ON_COMMAND(ID_LOG, OnMenuLog)
	ON_COMMAND(ID_EXIT, OnMenuExit)
	ON_NOTIFY(TCN_SELCHANGE, IDC_MAIN_TAB, &CmainDlg::OnTcnSelchangeTab)
	ON_NOTIFY(TCN_SELCHANGING, IDC_MAIN_TAB, &CmainDlg::OnTcnSelchangingTab)
	ON_COMMAND(ID_MENU_WEBSITE, OnMenuWebsite)
	ON_COMMAND(ID_MENU_HELP, OnMenuHelp)
	ON_COMMAND(ID_MENU_ADDL, OnMenuAddl)
	ON_COMMAND(ID_MUTE_INPUT, OnMuteInput)
	ON_COMMAND(ID_MUTE_OUTPUT, OnMuteOutput)
	ON_UPDATE_COMMAND_UI(IDS_STATUSBAR, &CmainDlg::OnUpdatePane)
	ON_UPDATE_COMMAND_UI(IDS_STATUSBAR2, &CmainDlg::OnUpdatePane)
END_MESSAGE_MAP()

LRESULT CmainDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_TASKBARRESTARTMESSAGE) {
		ShowTrayIcon();
	}
	if (message == WM_APPBAR_CALLBACK) {
		if (wParam == ABN_POSCHANGED || wParam == ABN_FULLSCREENAPP) {
			AppBarApplyPosition();
		}
		return 0;
	}
	return CBaseDialog::WindowProc(message, wParam, lParam);
}

BOOL CmainDlg::PreTranslateMessage(MSG * pMsg)
{
	BOOL catched = FALSE;
	if (accountSettings.enableMediaButtons) {
		if (pMsg->message == WM_SHELLHOOKMESSAGE) {
			onShellHookMessage(pMsg->wParam, pMsg->lParam);
		}
	}
	if (!catched) {
		return CBaseDialog::PreTranslateMessage(pMsg);
	}
	else {
		return TRUE;
	}
}

// CmainDlg message handlers

void CmainDlg::OnBnClickedOk()
{
}

void CmainDlg::OnBnClickedMenu()
{
	m_ButtonMenu.ModifyStyle(BS_DEFPUSHBUTTON, BS_PUSHBUTTON);
	MainPopupMenu(true);
	TabFocusSet();
}

CmainDlg::CmainDlg(CWnd * pParent /*=NULL*/)
	: CBaseDialog(CmainDlg::IDD, pParent)
{
#ifdef _DEBUG
	if (AllocConsole()) {
		HANDLE console = NULL;
		console = GetStdHandle(STD_OUTPUT_HANDLE);
		freopen("CONOUT$", "wt", stdout);
	}
#endif

	this->m_hWnd = NULL;
	mmNotificationClient = NULL;
	m_freepbxFooter = NULL;
	m_gdiplusToken = 0;
	updateCheckerShow = false;

	pageDialer = NULL;
	pageCalls = NULL;
	pageContacts = NULL;
	mainDlg = this;
	widthAdd = 0;
	heightAdd = 0;

	m_tabPrev = -1;
	newMessages = false;
	missed = false;
	m_snappingMainWindow = false;
	m_lockedWindowWidth = 0;
	m_appBarRegistered = false;
	m_docked = false;
	m_appBarEdge = ABE_LEFT;
	m_appBarPositioning = false;
	m_appBarMonitor.SetRectEmpty();
	m_dragWindowRect.SetRectEmpty();
	m_dragValid = false;
	m_callTracePanel = NULL;
	m_callTraceCallId = PJSUA_INVALID_ID;
	m_dialToneGeneration = 0;
	m_dialToneBaseState = 2;
	m_dialToneCheckPending = false;
	m_dialToneAudioMs = 0;
	m_dialToneReadyTick = 0;
	m_dialToneReadyAccount = PJSUA_INVALID_ID;
	m_dialToneCallPending = false;
	m_dialToneSampleCount = 0;
	m_dialToneSampleNext = 0;
	memset(m_dialToneOptionsSamples, 0, sizeof(m_dialToneOptionsSamples));
	memset(m_dialToneAudioSamples, 0, sizeof(m_dialToneAudioSamples));

	usersDirectoryLoaded = false;
	shortcutsURLLoaded = false;
	CString audioCodecsCaptions = _T("opus/48000/2;Opus 24 kHz;\
PCMA/8000/1;G.711 A-law;\
PCMU/8000/1;G.711 u-law;\
G722/16000/1;G.722 16 kHz;\
G7221/16000/1;G.722.1 16 kHz;\
G7221/32000/1;G.722.1 32 kHz;\
G723/8000/1;G.723 8 kHz;\
G729/8000/1;G.729 8 kHz;\
GSM/8000/1;GSM 8 kHz;\
GSM-EFR/8000/1;GSM-EFR 8 kHz;\
AMR/8000/1;AMR 8 kHz;\
AMR-WB/16000/1;AMR-WB 16 kHz;\
iLBC/8000/1;iLBC 8 kHz;\
speex/32000/1;Speex 32 kHz;\
speex/16000/1;Speex 16 kHz;\
speex/8000/1;Speex 8 kHz;\
SILK/24000/1;SILK 24 kHz;\
SILK/16000/1;SILK 16 kHz;\
SILK/12000/1;SILK 12 kHz;\
SILK/8000/1;SILK 8 kHz;\
L16/8000/1;LPCM 8 kHz;\
L16/8000/2;LPCM 8 kHz Stereo;\
L16/16000/1;LPCM 16 kHz;\
L16/16000/2;LPCM 16 kHz Stereo;\
L16/44100/1;LPCM 44 kHz;\
L16/44100/2;LPCM 44 kHz Stereo;\
L16/48000/1;LPCM 48 kHz;\
L16/48000/2;LPCM 48 kHz Stereo");
	int pos = 0;
	CString resToken = audioCodecsCaptions.Tokenize(_T(";"), pos);
	while (!resToken.IsEmpty()) {
		audioCodecList.AddTail(resToken);
		resToken = audioCodecsCaptions.Tokenize(_T(";"), pos);
	}

	wchar_t szBuf[STR_SZ];
	wchar_t szLocale[STR_SZ];
	::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SENGLANGUAGE, szBuf, STR_SZ);
	_tcscpy(szLocale, szBuf);
	::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SENGCOUNTRY, szBuf, STR_SZ);
	if (_tcsclen(szBuf) != 0) {
		_tcscat(szLocale, _T("_"));
		_tcscat(szLocale, szBuf);
	}
	::GetLocaleInfo(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, szBuf, STR_SZ);
	if (_tcsclen(szBuf) != 0) {
		_tcscat(szLocale, _T("."));
		_tcscat(szLocale, szBuf);
	}
	_tsetlocale(LC_ALL, szLocale); // e.g. szLocale = "English_United States.1252"

	LoadLangPackModule();

	Create(IDD, pParent);
}

int CmainDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	WM_TASKBARRESTARTMESSAGE = RegisterWindowMessage(_T("TaskbarCreated"));
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	CDC* pDC = GetDC();
	if (pDC) {
		dpiY = GetDeviceCaps(pDC->m_hDC, LOGPIXELSY);
		ReleaseDC(pDC);
	}
	else {
		dpiY = 96;
	}

	bool setpos = false;
	if (accountSettings.noResize) {
		lpCreateStruct->style &= ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
		::SetWindowLong(m_hWnd, GWL_STYLE, lpCreateStruct->style);
		CRect rectStub;
		GetClientRect(&rectStub);
		AdjustWindowRectEx(&rectStub, lpCreateStruct->style, FALSE, lpCreateStruct->dwExStyle);
		lpCreateStruct->cx = rectStub.Width();
		lpCreateStruct->cy = rectStub.Height();
		setpos = true;
	}

	ShortcutsLoad();
	shortcutsEnabled = accountSettings.enableShortcuts;
	shortcutsBottom = accountSettings.shortcutsBottom;
	shortcutsCount = shortcuts.GetCount();
	if (accountSettings.enableShortcuts) {
		if (shortcutsBottom) {
			if (shortcutsCount) {
				if (shortcutsCount > _GLOBAL_SHORTCUTS_QTY / 2) {
					heightAdd += MulDiv(10 + (shortcutsCount + shortcutsCount % 2) * 25 / 2, dpiY, 96);
				}
				else {
					heightAdd += MulDiv(10 + shortcutsCount * 25, dpiY, 96);
				}
			}
		}
		else {
			if (shortcutsCount > 12) {
				widthAdd += MulDiv(200, dpiY, 96);
			}
			else {
				widthAdd += MulDiv(140, dpiY, 96);
			}
		}
	}
	int heightFix = 0;
	if (setpos || widthAdd || heightAdd || heightFix) {
		SetWindowPos(NULL, 0, 0, lpCreateStruct->cx + widthAdd, lpCreateStruct->cy + heightAdd + heightFix, SWP_NOMOVE | SWP_NOZORDER);
	}

	if (langPack.rtl) {
		ModifyStyleEx(0, WS_EX_LAYOUTRTL);
	}

	return CBaseDialog::OnCreate(lpCreateStruct);
}

BOOL CmainDlg::OnInitDialog()
{
	CBaseDialog::OnInitDialog();
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

	WTSRegisterSessionNotification(m_hWnd, NOTIFY_FOR_THIS_SESSION);
	mmNotificationClient = new CMMNotificationClient();

	CreateThread(NULL, 0, NetworkChangeThread, 0, 0, NULL);

	settingsDlg = NULL;
	shortcutsDlg = NULL;

	messagesDlg = new MessagesDlg(this);
	transferDlg = NULL;
	accountDlg = NULL;

	m_lastInputTime = 0;
	m_idleCounter = 0;
	m_PresenceStatus = PJRPID_ACTIVITY_UNKNOWN;

#ifdef _GLOBAL_VIDEO
	previewWin = NULL;
#endif

	SetupJumpList();
	m_hIcon = theApp.LoadIcon(IDI_MAINFRAME);
	iconSmall = (HICON)LoadImage(
		AfxGetInstanceHandle(),
		MAKEINTRESOURCE(IDI_MAINFRAME),
		IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
	PostMessage(WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);

	TranslateDialog(this->m_hWnd);

	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// add tray icon or set tnd.hWnd = NULL;
	ShowTrayIcon();

	CRect mapRect;

	m_bar.Create(this);
	CStatusBarCtrl& statusctrl = m_bar.GetStatusBarCtrl();
	mapRect.bottom = 12;
	MapDialogRect(&mapRect);
	statusctrl.SetMinHeight(mapRect.bottom);
	m_bar.SetIndicators(indicators, sizeof(indicators) / sizeof(indicators[0]));
	m_bar.SetPaneInfo(IDS_STATUSBAR, IDS_STATUSBAR, SBPS_STRETCH, 0);
	m_bar.SetPaneInfo(IDS_STATUSBAR2, IDS_STATUSBAR2, SBPS_NOBORDERS, 0);
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, IDS_STATUSBAR);
	m_freepbxFooter = new CFreepbxFooter();
	m_freepbxFooter->CreateEx(0, AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW), NULL, WS_CHILD | WS_VISIBLE, CRect(0, 0, 0, 0), this, 0);
	m_freepbxFooter->LoadBranding();
	m_callTracePanel = new CCallTracePanel();
	m_callTracePanel->CreatePanel(this);

	AutoMove(m_bar.m_hWnd, 0, 100, 100, 0);
	//--set window pos
	CRect screenRect;
	if (accountSettings.multiMonitor) {
		MSIP::GetScreenRect(&screenRect);
	}
	else {
		SystemParametersInfo(SPI_GETWORKAREA, 0, &screenRect, 0);
	}
	CRect clientRect;
	GetClientRect(&clientRect);
	CRect rect;
	GetWindowRect(&rect);

	int mx;
	int my;
	int mW = accountSettings.mainW > 0 ? accountSettings.mainW : rect.Width();

	int mH = accountSettings.mainH > 0 ? accountSettings.mainH : rect.Height();
	CRect primaryScreenRect;
	SystemParametersInfo(SPI_GETWORKAREA, 0, &primaryScreenRect, 0);
	CRect savedRect(accountSettings.mainX, accountSettings.mainY,
		accountSettings.mainX + mW + widthAdd, accountSettings.mainY + mH);
	// No saved coordinates is the first run; a saved rectangle on no current monitor is stale.
	bool recoverToPrimary = (!accountSettings.mainX && !accountSettings.mainY)
		|| MonitorFromRect(&savedRect, MONITOR_DEFAULTTONULL) == NULL;
	if (recoverToPrimary) {
		// Horizontally centred on the primary work area; height stays taskbar-aware below.
		mx = primaryScreenRect.left + (primaryScreenRect.Width() - mW - widthAdd) / 2;
		my = primaryScreenRect.Height() - mH;
	}
	else {
		int maxLeft = screenRect.right - mW;
		if (accountSettings.mainX > maxLeft) {
			mx = maxLeft;
		}
		else {
			mx = accountSettings.mainX < screenRect.left ? screenRect.left : accountSettings.mainX;
		}
		int maxTop = screenRect.bottom - mH;
		if (accountSettings.mainY > maxTop) {
			my = maxTop;
		}
		else {
			my = accountSettings.mainY < screenRect.top ? screenRect.top : accountSettings.mainY;
		}
	}

	//--set messages window pos/size
	messagesDlg->GetWindowRect(&rect);
	int messagesX;
	int messagesY;
	int messagesW = accountSettings.messagesW > 0 ? accountSettings.messagesW : 550;
	int messagesH = accountSettings.messagesH > 0 ? accountSettings.messagesH : mH;
	// coors not specified, first run
	if (!accountSettings.messagesX && !accountSettings.messagesY) {
		accountSettings.messagesX = mx - messagesW;
		accountSettings.messagesY = my;
	}
	int maxLeft = screenRect.right - messagesW;
	if (accountSettings.messagesX > maxLeft) {
		messagesX = maxLeft;
	}
	else {
		messagesX = accountSettings.messagesX < screenRect.left ? screenRect.left : accountSettings.messagesX;
	}
	int maxTop = screenRect.bottom - messagesH;
	if (accountSettings.messagesY > maxTop) {
		messagesY = maxTop;
	}
	else {
		messagesY = accountSettings.messagesY < screenRect.top ? screenRect.top : accountSettings.messagesY;
	}
	messagesDlg->SetWindowPos(NULL, messagesX, messagesY, messagesW, messagesH, SWP_NOZORDER);
	SetWindowPos(accountSettings.alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost, mx, my, mW, mH, NULL);
	SnapMainWindowToWorkArea();
	GetWindowRect(&rect);
	m_lockedWindowWidth = rect.Width();

	imageListStatus = new CImageList();
	imageListStatus->Create(16, 16, ILC_COLOR32, 3, 3);
	imageListStatus->SetBkColor(RGB(255, 255, 255));
	imageListStatus->Add(LoadImageIcon(IDI_BLANK));
	imageListStatus->Add(LoadImageIcon(IDI_UNKNOWN));
	imageListStatus->Add(LoadImageIcon(IDI_OFFLINE));
	imageListStatus->Add(LoadImageIcon(IDI_AWAY));
	imageListStatus->Add(LoadImageIcon(IDI_ONLINE));
	imageListStatus->Add(LoadImageIcon(IDI_ON_THE_PHONE));
	imageListStatus->Add(LoadImageIcon(IDI_BUSY));
	imageListStatus->Add(LoadImageIcon(IDI_DEFAULT));
	imageListStatus->Add(LoadImageIcon(IDI_UNKNOWN_STARRED));
	imageListStatus->Add(LoadImageIcon(IDI_OFFLINE_STARRED));
	imageListStatus->Add(LoadImageIcon(IDI_AWAY_STARRED));
	imageListStatus->Add(LoadImageIcon(IDI_ONLINE_STARRED));
	imageListStatus->Add(LoadImageIcon(IDI_ON_THE_PHONE_STARRED));
	imageListStatus->Add(LoadImageIcon(IDI_BUSY_STARRED));
	imageListStatus->Add(LoadImageIcon(IDI_DEFAULT_STARRED));

	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	CRect tabRect;
	tab->GetWindowRect(&tabRect);
	ScreenToClient(&tabRect);
	TC_ITEM tabItem;
	CRect lineRect;
	lineRect.bottom = 3;
	MapDialogRect(&lineRect);
	tabRect.top += lineRect.bottom;
	tabRect.bottom += lineRect.bottom;
	tab->SetWindowPos(NULL, tabRect.left, tabRect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	tabItem.mask = TCIF_TEXT | TCIF_PARAM;
	mapRect.right = _GLOBAL_TAB_WIDTH; // tab item width
	mapRect.bottom = 5; // bottom line height
	MapDialogRect(&mapRect);
	CSize size;
	size.SetSize(mapRect.right, tabRect.Height() - mapRect.bottom);
	tab->SetItemSize(size);

	m_ButtonMenu.SetIcon(LoadImageIcon(IDI_DROPDOWN));

	if (widthAdd) {
		CRect pageRect;
		m_ButtonMenu.GetWindowRect(pageRect);
		ScreenToClient(pageRect);
		m_ButtonMenu.SetWindowPos(NULL, pageRect.left + widthAdd, pageRect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
		//--
		tabRect.right += widthAdd;
		tab->SetWindowPos(NULL, 0, 0, tabRect.Width(), tabRect.Height(), SWP_NOZORDER | SWP_NOMOVE);
	}

	AutoMove(tab->m_hWnd, 0, 0, 100, 0);
	AutoMove(m_ButtonMenu.m_hWnd, 100, 0, 0, 0);

	BYTE offset = tabRect.bottom - 1;
	CRect pageRect;

	pageDialer = new Dialer(this);
	pageDialer->ModifyStyle(0, WS_CLIPSIBLINGS);
	tabItem.pszText = Translate(_T("Phone"));
	tabItem.iImage = 0;
	tabItem.lParam = (LPARAM)pageDialer;
	tab->InsertItem(99, &tabItem);
	pageDialer->GetWindowRect(pageRect);
	int pageWidth = pageRect.Width() + (clientRect.Width() - pageRect.Width()) / 3;
	int offsetX = (clientRect.Width() - pageWidth) / 2;
	pageDialer->SetWindowPos(NULL, offsetX, offset, pageWidth, pageRect.Height(), SWP_NOZORDER);

		pageCalls = new Calls(this);
		pageCalls->OnCreated();
		tabItem.pszText = Translate(_T("Logs"));
		tabItem.iImage = 1;
		tabItem.lParam = (LPARAM)pageCalls;
		tab->InsertItem(99, &tabItem);
		pageCalls->GetWindowRect(pageRect);
		pageCalls->SetWindowPos(NULL, 0, offset, pageRect.Width() + widthAdd, pageRect.Height() + heightAdd, SWP_NOZORDER);
		AutoMove(pageCalls->m_hWnd, 0, 0, 100, 100);

		pageContacts = new Contacts(this);
		pageContacts->OnCreated();
		tabItem.pszText = Translate(_T("Contacts"));
		tabItem.iImage = 3;
		tabItem.lParam = (LPARAM)pageContacts;
		tab->InsertItem(99, &tabItem);
		pageContacts->GetWindowRect(pageRect);
		pageContacts->SetWindowPos(NULL, 0, offset, pageRect.Width() + widthAdd, pageRect.Height() + heightAdd, SWP_NOZORDER);
		AutoMove(pageContacts->m_hWnd, 0, 0, 100, 100);

	tab->SetCurSel(accountSettings.activeTab);

	BOOL minimized = !lstrcmp(theApp.m_lpCmdLine, _T("/minimized"));
	if (minimized) {
		theApp.m_lpCmdLine = _T("");
	}
	m_startMinimized = (!firstRun && minimized) || accountSettings.minimized;

	InitUI();
	OnAccountChanged(true);
	LayoutFreepbxFooter();
	ApplyDarkMode();

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CmainDlg::InitUI()
{
	onMWIInfo(0, 0); // voicemail button
	SetPaneText2();
    SetWindowText(_T(_GLOBAL_NAME_VISIBLE));
	UpdateWindowText();
	pageDialer->SetName();
}

void CmainDlg::ShowTrayIcon()
{
	// add tray icon
	tnd.cbSize = sizeof(NOTIFYICONDATA);
	tnd.hWnd = this->GetSafeHwnd();
	tnd.uID = IDI_MAINFRAME;
	tnd.uCallbackMessage = UM_NOTIFYICON;
	tnd.uFlags = NIF_MESSAGE | NIF_ICON;
	iconMissed = LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_MISSED));
	iconInactive = LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_INACTIVE));
	tnd.hIcon = iconInactive;
	DWORD dwMessage = NIM_ADD;
	Shell_NotifyIcon(dwMessage, &tnd);
}

void CmainDlg::OnCreated()
{
	LRESULT pResult;
	mainDlg->OnTcnSelchangeTab(NULL, &pResult);

	if (!m_startMinimized) {
		ShowWindow(SW_SHOW);
		TabFocusSet();
	}

	PJCreate();

	if (lstrlen(theApp.m_lpCmdLine)) {
		CommandLine(theApp.m_lpCmdLine);
		theApp.m_lpCmdLine = NULL;
	}
	PJAccountAdd();
	//--
	WM_SHELLHOOKMESSAGE = RegisterWindowMessage(_T("SHELLHOOK"));
	if (WM_SHELLHOOKMESSAGE) {
		RegisterShellHookWindow(m_hWnd);
	}
}

void CmainDlg::TrayIconUpdateTip()
{
	if (tnd.hWnd) {
		CString tip;
		tip = _T(_GLOBAL_NAME_VISIBLE);
		if (accountSettings.accountId) {
			if (!accountSettings.account.label.IsEmpty()) {
				//tip.AppendFormat(_T("\r\n%s: %s"), Translate(_T("Account")), accountSettings.account.label);
				tip.AppendFormat(_T("\r\n%s"), accountSettings.account.label);
			}
			else if (!accountSettings.account.username.IsEmpty()) {
				tip.AppendFormat(_T("\r\n%s"), accountSettings.account.username);
			}
			if (!accountSettings.account.displayName.IsEmpty()) {
				tip.AppendFormat(_T("\r\n%s"), accountSettings.account.displayName);
			}
		}
		lstrcpyn(tnd.szTip, (LPCTSTR)tip, sizeof(tnd.szTip));
		tnd.uFlags = NIF_TIP;
		DWORD dwMessage = NIM_MODIFY;
		Shell_NotifyIcon(dwMessage, &tnd);
	}
}

void CmainDlg::BaloonPopup(CString title, CString message, DWORD flags)
{
	if (tnd.hWnd) {
		lstrcpyn(tnd.szInfo, message, sizeof(tnd.szInfo));
		lstrcpyn(tnd.szInfoTitle, title, sizeof(tnd.szInfoTitle));
		tnd.uFlags = NIF_INFO | NIF_ICON;
		tnd.hIcon = iconSmall;
		tnd.dwInfoFlags = flags;
		DWORD dwMessage = NIM_MODIFY;
		Shell_NotifyIcon(dwMessage, &tnd);
	}
}

void CmainDlg::SwitchDND(int state, bool update)
{
	if (state == -1) {
		accountSettings.DND = !accountSettings.DND;
	}
	else {
		accountSettings.DND = state;
	}
	pageDialer->SetCheckDND(accountSettings.DND);
	AccountSettingsPendingSave();
	mainDlg->PublishStatus();
	if (update) {
		return;
	}
}

void CmainDlg::OnMenuAccountAdd()
{
    if (!accountDlg) {
        accountDlg = new AccountDlg(this);
    }
    else {
        accountDlg->SetForegroundWindow();
    }
    if (accountDlg) {
        accountDlg->Load(-1);
    }
}

void CmainDlg::OnMenuAccountEdit(UINT nID)
{
	if (!accountDlg) {
		accountDlg = new AccountDlg(this);
	}
	else {
		accountDlg->SetForegroundWindow();
	}
	if (accountDlg) {
		int id = accountSettings.accountId > 0 ? accountSettings.accountId : nID - ID_ACCOUNT_EDIT_RANGE + 1;
		accountDlg->Load(id ? id : -1);
	}
}
void CmainDlg::OnMenuAccountChange(UINT nID)
{
	if (accountSettings.accountId) {
		PJAccountDelete(true);
	}
	int idNew = nID - ID_ACCOUNT_CHANGE_RANGE + 1;
	if (accountSettings.accountId != idNew) {
		accountSettings.accountId = idNew;
		accountSettings.AccountLoad(accountSettings.accountId, &accountSettings.account);
	}
	else {
			accountSettings.accountId = 0;
			InitUI();
	}
	OnAccountChanged();
	accountSettings.SettingsSave();
	mainDlg->PJAccountAdd();
}

void CmainDlg::OnMenuAccountLocalEdit()
{
	if (MACRO_ENABLE_LOCAL_ACCOUNT) {
		if (!accountDlg) {
			accountDlg = new AccountDlg(this);
		}
		else {
			accountDlg->SetForegroundWindow();
		}
		if (accountDlg) {
			accountDlg->Load(0);
		}
	}
}

void CmainDlg::OnMenuCustomRange(UINT nID)
{
}

void CmainDlg::OnMenuSettings()
{
    if (!settingsDlg) {
        bool showDlg = true;
        if (showDlg) {
            settingsDlg = new SettingsDlg(this);
        }
    }
    else {
        settingsDlg->SetForegroundWindow();
    }
}

void CmainDlg::OnMenuShortcuts()
{
    if (!shortcutsDlg) {
        shortcutsDlg = new ShortcutsDlg(this);
    }
    else {
        shortcutsDlg->SetForegroundWindow();
    }
}

void CmainDlg::OnMenuAlwaysOnTop()
{
	accountSettings.alwaysOnTop = 1 - accountSettings.alwaysOnTop;
	AccountSettingsPendingSave();
	SetWindowPos(accountSettings.alwaysOnTop ? &this->wndTopMost : &this->wndNoTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void CmainDlg::ApplyDarkMode()
{
	BOOL enabled = accountSettings.darkMode ? TRUE : FALSE;
	if (FAILED(DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled)))) {
		enabled = FALSE;
	}
	SetWindowTheme(m_ButtonMenu.m_hWnd, accountSettings.darkMode ? L"DarkMode_Explorer" : NULL, NULL);
	SetWindowTheme(m_bar.m_hWnd, accountSettings.darkMode ? L"DarkMode_Explorer" : NULL, NULL);
	m_tabCtrl.SetDarkMode(accountSettings.darkMode);
	CStatusBarCtrl& statusctrl = m_bar.GetStatusBarCtrl();
	statusctrl.SetBkColor(accountSettings.darkMode ? DarkPalette::Window() : GetSysColor(COLOR_3DFACE));
	if (pageDialer) {
		pageDialer->SetDarkMode(accountSettings.darkMode);
	}
	if (pageCalls) {
		pageCalls->SetDarkMode(accountSettings.darkMode);
	}
	if (pageContacts) {
		pageContacts->SetDarkMode(accountSettings.darkMode);
	}
	if (imageListStatus) {
		imageListStatus->SetBkColor(accountSettings.darkMode ? DarkPalette::Window() : RGB(255, 255, 255));
	}
	if (m_freepbxFooter) {
		m_freepbxFooter->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	}
	if (m_callTracePanel && ::IsWindow(m_callTracePanel->m_hWnd)) {
		m_callTracePanel->SetDarkMode(accountSettings.darkMode);
	}
	m_ButtonMenu.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	m_bar.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
}

void CmainDlg::OnMenuDarkMode()
{
	accountSettings.darkMode = !accountSettings.darkMode;
	AccountSettingsPendingSave();
	ApplyDarkMode();
}

void CmainDlg::OnMenuBranding()
{
	CBrandingDlg dialog(this);
	if (dialog.DoModal() == IDOK && m_freepbxFooter) {
		m_freepbxFooter->LoadBranding();
		m_freepbxFooter->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
	}
}

// All trace emitters run on the UI thread from already-marshalled call data.
void CmainDlg::CallTraceOnCallState(pjsua_call_info* call_info)
{
	if (!m_callTracePanel || !::IsWindow(m_callTracePanel->m_hWnd) || !call_info) {
		StopDialTone(_T("A call state began · dial tone stopped"));
		return;
	}
	bool preserveQualification = call_info->role == PJSIP_ROLE_UAC && m_dialToneCallPending
		&& call_info->acc_id == m_dialToneReadyAccount && m_dialToneReadyTick
		&& GetTickCount() - m_dialToneReadyTick <= 60000;
	StopDialTone(_T("INVITE started · dial tone stopped"), preserveQualification);
	m_dialToneCallPending = false;
	if (!m_callTracePanel->BeginCall(call_info, preserveQualification)) {
		return;
	}
	CString line;
	if (m_callTraceCallId != call_info->id) {
		m_callTraceCallId = call_info->id;
		if (!preserveQualification) {
			m_callTracePanel->Clear();
		}
		else {
			m_callTracePanel->Append(_T("Qualification associated with this outgoing call"));
			m_callTracePanel->Append(_T("INVITE started"));
		}
		m_callTracePanel->Append(call_info->role == PJSIP_ROLE_UAS
			? _T("Incoming call created") : _T("Outgoing call created"));
		line.Format(_T("Call ID: %d"), call_info->id);
		m_callTracePanel->Append(line);
		line.Format(_T("Account: %d %s"), call_info->acc_id, accountSettings.account.username);
		m_callTracePanel->Append(line);
		line.Format(_T("Server: %s"), get_account_server());
		m_callTracePanel->Append(line);
		line.Format(_T("SIP Call-ID: %s"), CString(call_info->call_id.ptr, call_info->call_id.slen));
		m_callTracePanel->Append(line);
		line.Format(_T("Local URI: %s"), CString(call_info->local_info.ptr, call_info->local_info.slen));
		m_callTracePanel->Append(line);
		line.Format(_T("Remote URI: %s"), CString(call_info->remote_info.ptr, call_info->remote_info.slen));
		m_callTracePanel->Append(line);
		if (call_info->remote_contact.slen) {
			line.Format(_T("Remote contact: %s"), CString(call_info->remote_contact.ptr, call_info->remote_contact.slen));
			m_callTracePanel->Append(line);
		}
	}
	line.Format(_T("State: %s"), CString(call_info->state_text.ptr, call_info->state_text.slen));
	m_callTracePanel->Append(line);
	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		line.Format(_T("SIP status: %d / %s"), call_info->last_status,
			CString(call_info->last_status_text.ptr, call_info->last_status_text.slen));
		m_callTracePanel->Append(line);
		m_callTracePanel->EndCall(call_info);
	}
}

void CmainDlg::CallTraceOnMediaState(pjsua_call_info* call_info)
{
	if (!m_callTracePanel || !::IsWindow(m_callTracePanel->m_hWnd) || !call_info) {
		return;
	}
	CString line;
	switch (call_info->media_status) {
	case PJSUA_CALL_MEDIA_ACTIVE:
		m_callTracePanel->Append(_T("Media: active"));
		break;
	case PJSUA_CALL_MEDIA_LOCAL_HOLD:
		m_callTracePanel->Append(_T("Media: local hold"));
		break;
	case PJSUA_CALL_MEDIA_REMOTE_HOLD:
		m_callTracePanel->Append(_T("Media: remote hold"));
		break;
	case PJSUA_CALL_MEDIA_ERROR:
		m_callTracePanel->Append(_T("Media: error"));
		break;
	default:
		m_callTracePanel->Append(_T("Media: none"));
		break;
	}
	if (call_info->media_status != PJSUA_CALL_MEDIA_ACTIVE) {
		return;
	}
	pjsua_stream_info stream_info;
	if (pjsua_call_get_stream_info(call_info->id, 0, &stream_info) == PJ_SUCCESS
		&& stream_info.type == PJMEDIA_TYPE_AUDIO) {
		line.Format(_T("Audio: %s/%d/%d"),
			CString(stream_info.info.aud.fmt.encoding_name.ptr, stream_info.info.aud.fmt.encoding_name.slen),
			stream_info.info.aud.fmt.clock_rate, stream_info.info.aud.fmt.channel_cnt);
		m_callTracePanel->Append(line);
		char addr[PJ_INET6_ADDRSTRLEN + 10];
		if (pj_sockaddr_print(&stream_info.info.aud.rem_addr, addr, sizeof(addr), 3)) {
			line.Format(_T("Remote RTP: %s"), CString(addr));
			m_callTracePanel->Append(line);
		}
	}
	pjsua_stream_stat stat;
	if (pjsua_call_get_stream_stat(call_info->id, 0, &stat) == PJ_SUCCESS) {
		line.Format(_T("RTP rx=%u tx=%u loss_rx=%u jitter_rx=%.1f ms rtt=%.1f ms"),
			(unsigned)stat.rtcp.rx.pkt, (unsigned)stat.rtcp.tx.pkt, (unsigned)stat.rtcp.rx.loss,
			stat.rtcp.rx.jitter.last / 1000.0f, stat.rtcp.rtt.last / 1000.0f);
		m_callTracePanel->Append(line);
	}
}

void CmainDlg::CallTraceOnIdentityChange(pjsua_call_id call_id)
{
	if (!m_callTracePanel || !::IsWindow(m_callTracePanel->m_hWnd) || call_id != m_callTraceCallId) {
		return;
	}
	call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_id);
	if (!user_data) {
		return;
	}
	user_data->CS.Lock();
	CString header = user_data->callerIDHeader;
	CString previous = user_data->callerIDPrev;
	CString current = user_data->callerID;
	user_data->CS.Unlock();
	CString line;
	if (!header.IsEmpty() && !current.IsEmpty()) {
		line.Format(_T("%s received: %s"), header, current);
		m_callTracePanel->Append(line);
	}
	line.Format(_T("Connected identity changed: %s -> %s"),
		previous.IsEmpty() ? _T("-") : previous, current.IsEmpty() ? _T("-") : current);
	m_callTracePanel->Append(line);
}

LRESULT CmainDlg::onCallForwardingResult(WPARAM, LPARAM lParam)
{
	CString* message = (CString*)lParam;
	if (message) {
		if (m_callTracePanel && ::IsWindow(m_callTracePanel->m_hWnd)) {
			m_callTracePanel->Append(*message);
		}
		delete message;
	}
	return 0;
}

void CmainDlg::OnMenuLog()
{
	MSIP::OpenFile(accountSettings.logFile);
}

void CmainDlg::OnMenuExit()
{
	this->DestroyWindow();
}

LRESULT CmainDlg::onTrayNotify(WPARAM wParam, LPARAM lParam)
{
	UINT uMsg = (UINT)lParam;
	switch (uMsg)
	{
	case NIN_BALLOONUSERCLICK:
		onTrayNotify(NULL, WM_LBUTTONUP);
		break;
	case WM_LBUTTONUP:
		if (this->IsWindowVisible() && !IsIconic())
		{
			if (wParam) {
				ShowWindow(SW_HIDE);
			}
			else {
				//set up a generic keyboard event
				INPUT keyInput;
				keyInput.type = INPUT_KEYBOARD;
				keyInput.ki.wScan = 0; //hardware scan code for key
				keyInput.ki.time = 0;
				keyInput.ki.dwExtraInfo = 0;

				//set focus to the hWnd (sending Alt allows to bypass limitation)
				keyInput.ki.wVk = VK_MENU;
				keyInput.ki.dwFlags = 0;   //0 for key press
				SendInput(1, &keyInput, sizeof(INPUT));

				SetForegroundWindow(); //sets the focus

				keyInput.ki.wVk = VK_MENU;
				keyInput.ki.dwFlags = KEYEVENTF_KEYUP;  //for key release
				SendInput(1, &keyInput, sizeof(INPUT));
			}
		}
		else
		{
			bool blockRestore = false;
			if (!blockRestore) {
				if (IsIconic()) {
					ShowWindow(SW_RESTORE);
				}
				else {
					ShowWindow(SW_SHOW);
				}
				SetForegroundWindow();
				if (missed) {
					GotoTabLParam((LPARAM)pageCalls);
					missed = false;
					UpdateWindowText();
				}
				// -- show ringing dialogs
				int count = ringinDlgs.GetCount();
				for (int i = 0; i < count; i++) {
					RinginDlg* ringinDlg = ringinDlgs.GetAt(i);
					ringinDlg->ShowWindow(SW_SHOWNORMAL);
				}
				// -- show messages dialog
				bool showMessagesDialog = (!accountSettings.singleMode && messagesDlg->GetCallsCount()) || newMessages;
				if (showMessagesDialog) {
					newMessages = false;
					messagesDlg->ShowWindow(SW_SHOW);
				}
				//--
				TabFocusSet();
			}
		}
		break;
	case WM_RBUTTONUP:
		MainPopupMenu();
		break;
	}
	return TRUE;
}

void CmainDlg::MainPopupMenu(bool isMenuButton)
{
	CString str;
	CPoint point;
	if (isMenuButton) {
		CWnd* menuButton = mainDlg->GetDlgItem(IDC_MAIN_MENU);
		CRect rect;
		menuButton->GetWindowRect(rect);
		point = rect.TopLeft();
	}
	else {
		GetCursorPos(&point);
	}
	CMenu menu;
	menu.CreatePopupMenu();
	CMenu* tracker = &menu;
	bool basic = false;
    if (!basic) {

				// -- add
				tracker->AppendMenu(MF_STRING, ID_ACCOUNT_ADD, Translate(_T("Add Account...")));
				//-- edit
				CMenu editMenu;
				editMenu.CreatePopupMenu();
				bool checked = false;
				Account acc;
				int i = 0;
				while (true) {
					if (!accountSettings.AccountLoad(i + 1, &acc)) {
						break;
					}
					if (!acc.label.IsEmpty()) {
						str = acc.label;
					}
					else {
						str.Format(_T("%s@%s"), acc.username, acc.domain);
					}
					tracker->InsertMenu(ID_ACCOUNT_ADD, (accountSettings.accountId == i + 1 ? MF_CHECKED : 0), ID_ACCOUNT_CHANGE_RANGE + i, str);
					editMenu.AppendMenu(MF_STRING, ID_ACCOUNT_EDIT_RANGE + i, str);
					if (!checked) {
						checked = accountSettings.accountId == i + 1;
					}
					i++;
				}
				if (i == 1) {
						MENUITEMINFO menuItemInfo;
						menuItemInfo.cbSize = sizeof(MENUITEMINFO);
						menuItemInfo.fMask = MIIM_STRING;
						menuItemInfo.dwTypeData = Translate(_T("Make Active"));
						tracker->SetMenuItemInfo(ID_ACCOUNT_CHANGE_RANGE, &menuItemInfo);
				}
				str = Translate(_T("Edit Account"));
				str.Append(_T("\tCtrl+M"));
				if (i == 1) {
						tracker->InsertMenu(ID_ACCOUNT_ADD, 0, ID_ACCOUNT_EDIT_RANGE, str);
				}
				else if (i > 1) {
					tracker->InsertMenu(ID_ACCOUNT_ADD, MF_SEPARATOR);
					if (checked) {
						tracker->InsertMenu(ID_ACCOUNT_ADD, 0, ID_ACCOUNT_EDIT_RANGE, str);
					}
					else {
						tracker->InsertMenu(ID_ACCOUNT_ADD, MF_POPUP, (UINT_PTR)editMenu.m_hMenu, Translate(_T("Edit Account")));
					}
				}

		if (accountSettings.enableLocalAccount && MACRO_ENABLE_LOCAL_ACCOUNT) {
			str = Translate(_T("Edit Local Account"));
			str.Append(_T("\tCtrl+L"));
			tracker->AppendMenu(MF_STRING, ID_ACCOUNT_EDIT_LOCAL, str);
		}

					str = Translate(_T("Settings"));
					str.Append(_T("\tCtrl+P"));
					tracker->AppendMenu(MF_STRING, ID_SETTINGS, str);
		tracker->AppendMenu(MF_SEPARATOR);
		str = Translate(_T("Shortcuts"));
		str.Append(_T("\tCtrl+S"));
		tracker->AppendMenu(MF_STRING, ID_SHORTCUTS, str);
	}

    bool separator = false;
        if (!separator) {
            tracker->AppendMenu(MF_SEPARATOR);
            separator = true;
        }
        tracker->AppendMenu(MF_STRING | (accountSettings.alwaysOnTop ? MF_CHECKED : 0), ID_ALWAYS_ON_TOP, Translate(_T("Always on Top")));
		tracker->AppendMenu(MF_STRING | (accountSettings.darkMode ? MF_CHECKED : 0), ID_DARK_MODE, Translate(_T("Dark Mode")));
		tracker->AppendMenu(MF_STRING, ID_BRANDING, _T("Branding..."));
			if (!separator) {
				tracker->AppendMenu(MF_SEPARATOR);
				separator = true;
			}
			tracker->AppendMenu(MF_STRING | (!accountSettings.enableLog ? MF_DISABLED | MF_GRAYED : 0), ID_LOG, Translate(_T("View Log File")));

	separator = false;

	if (!separator) {
		tracker->AppendMenu(MF_SEPARATOR);
		separator = true;
	}
	str = Translate(_T("Visit Website"));
	str.Append(_T("\tCtrl+W"));
	tracker->AppendMenu(MF_STRING, ID_MENU_WEBSITE, str);
	separator = false;

	if (!separator) {
		tracker->AppendMenu(MF_SEPARATOR);
		separator = true;
	}
	str = Translate(_T("Help"));
    str.AppendFormat(_T("\tv%s"), _T(_GLOBAL_VERSION));
	tracker->AppendMenu(MF_STRING, ID_MENU_HELP, str);
	separator = false;

	tracker->AppendMenu(MF_SEPARATOR);
	str = Translate(_T("Exit"));
	str.Append(_T("\tCtrl+Q"));
	tracker->AppendMenu(MF_STRING, ID_EXIT, str);

	MENUITEMINFO menuItemInfo;
	menuItemInfo.cbSize = sizeof(MENUITEMINFO);
	menuItemInfo.fMask = MIIM_FTYPE;
	tracker->GetMenuItemInfo(0, &menuItemInfo, TRUE);
	if (menuItemInfo.fType == MFT_SEPARATOR) {
		tracker->RemoveMenu(0, MF_BYPOSITION);
	}

	SetForegroundWindow();
	tracker->TrackPopupMenu(0, point.x, point.y, this);
	PostMessage(WM_NULL, 0, 0);
}

LRESULT CmainDlg::onCreateRingingDlg(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_id call_id = wParam;
	pjsua_call_info call_info;

	if (!is_pjsua_running() || pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS) {
		int count = ringinDlgs.GetCount();
		if (!count) {
			PlayerStop();
		}
		return  0;
	}

	call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_info.id);
	if (!user_data) {
		return  0;
	}

	user_data->CS.Lock();

	RinginDlg* ringinDlg = new RinginDlg(this);

	ringinDlg->remoteHasVideo = call_info.rem_vid_cnt;
#ifdef _GLOBAL_VIDEO
	if (call_info.rem_vid_cnt) {
		((CButton*)ringinDlg->GetDlgItem(IDC_VIDEO))->EnableWindow(TRUE);
	}
#endif
	ringinDlg->SetCallId(call_info.id);

    SIPURI sipuri;

    CString name = user_data->name;

	ringinDlg->GetDlgItem(IDC_CALLER_NAME)->SetWindowText(name);
	ringinDlg->GetDlgItem(IDC_RINGIN_NAME_BLIND)->SetWindowText(name);

    CString str;
    ParseCallSIPURI(&call_info, user_data, &sipuri);
    CString info = (!sipuri.user.IsEmpty() ? sipuri.user + _T("@") : _T("")) + sipuri.domain;
	if (!sipuri.name.IsEmpty() && sipuri.name != name) {
		info = sipuri.name + _T(" <") + info + _T(">");
	}
	str.AppendFormat(_T("%s\r\n"), info);
	if (!user_data->userAgent.IsEmpty()) {
		str.AppendFormat(_T("%s\r\n"), user_data->userAgent);
	}
	str.Append(_T("\r\n"));
	info = MSIP::PjToStr(&call_info.local_info, TRUE);
	MSIP::ParseSIPURI(info, &sipuri);
	info = (!sipuri.user.IsEmpty() ? sipuri.user + _T("@") : _T("")) + sipuri.domain;
	str.AppendFormat(_T("%s: %s\r\n"), Translate(_T("To")), info);

	if (!user_data->diversion.IsEmpty()) {
		str.AppendFormat(_T("%s: %s\r\n"), Translate(_T("Diversion")), user_data->diversion);
	}
	if (str == name) {
		str.Empty();
	}
	if (!str.IsEmpty()) {
		ringinDlg->GetDlgItem(IDC_CALLER_ADDR)->SetWindowText(str);
	}
	else {
		ringinDlg->GetDlgItem(IDC_CALLER_ADDR)->EnableWindow(FALSE);
	}

	ringinDlgs.Add(ringinDlg);
	if (!accountSettings.bringToFrontOnIncoming) {
		if (GetForegroundWindow()->GetTopLevelParent() != this) {
			BaloonPopup(Translate(_T("Incoming Call")), name, NIIF_INFO);
		}
	}
	user_data->CS.Unlock();
	return 0;
}

LRESULT CmainDlg::onRefreshLevels(WPARAM wParam, LPARAM lParam)
{
	pageDialer->OnHScroll(0, 0, NULL);
	return 0;
}

LRESULT CmainDlg::onPager(WPARAM wParam, LPARAM lParam)
{
	CString* number = (CString*)wParam;
	CString* message = (CString*)lParam;
	MessagesIncoming(number, message);
	delete number;
	delete message;
	return 0;
}

void CmainDlg::MessagesIncoming(CString * number, CString * message, CTime * pTime)
{
	bool doNotShowMessagesWindow = !mainDlg->IsWindowVisible();
	if (doNotShowMessagesWindow) {
		newMessages = true;
	}
	MessagesContact* messagesContact = messagesDlg->AddTab(*number,
		FALSE, NULL, NULL,
		doNotShowMessagesWindow
	);
	if (messagesContact) {
		messagesDlg->AddMessage(messagesContact, *message, MSIP_MESSAGE_TYPE_REMOTE, FALSE, pTime);
		onPlayerPlay(MSIP_SOUND_MESSAGE_IN, 0);
	}
}

LRESULT CmainDlg::onPagerStatus(WPARAM wParam, LPARAM lParam)
{
	CString* number = (CString*)wParam;
	CString* message = (CString*)lParam;
	bool doNotShowMessagesWindow = !mainDlg->IsWindowVisible();
	MessagesContact* messagesContact = mainDlg->messagesDlg->AddTab(*number,
		FALSE, NULL, NULL,
		doNotShowMessagesWindow);
	if (messagesContact) {
		mainDlg->messagesDlg->AddMessage(messagesContact, *message);
	}
	delete number;
	delete message;
	return 0;
}

LRESULT CmainDlg::OnNetworkChange(WPARAM wParam, LPARAM lParam)
{
	StopDialTone(_T("Network changed · READY left and dial tone stopped"));
	KillTimer(IDT_TIMER_NETWORK_CHANGED);
	SetTimer(IDT_TIMER_NETWORK_CHANGED, 1000, 0);
	return TRUE;
}

LRESULT CmainDlg::OnRestart(WPARAM wParam, LPARAM lParam)
{
	DestroyWindow();
	ShellExecute(NULL, NULL, accountSettings.exeFile, NULL, NULL, SW_SHOWDEFAULT);
	return TRUE;
}

void CmainDlg::OnTimerNetworkChange()
{
	if (!is_pjsua_running()) {
		return;
	}
	if (!MSIP::IsConnectedToNetwork()) {
		return;
	}
	MSIP::PortKnock();
	if (accountSettings.networkChanges) {
		//PJ_LOG(3, (THIS_FILENAME, "NETWORK CHANGED, update transports, accounts and calls"));
		pjsua_ip_change_param param;
		pjsua_ip_change_param_default(&param);
		if (pjsua_handle_ip_change(&param) == PJ_SUCCESS) {
			pjsua_acc_id ids[PJSUA_MAX_ACC];
			unsigned count = PJSUA_MAX_ACC;
			if (pjsua_enum_accs(ids, &count) == PJ_SUCCESS) {
				for (unsigned i = 0; i < count; i++) {
					pj_pool_t* tmp_pool = pjsua_pool_create("msip_ipch", 256, 256);
					if (!tmp_pool) continue;
					pjsua_acc_config acc_cfg;
					pjsua_acc_config_default(&acc_cfg);
					if (pjsua_acc_get_config(ids[i], tmp_pool, &acc_cfg) == PJ_SUCCESS) {
						if (acc_cfg.rtp_cfg.public_addr.slen > 0) {
							// update public address for account
							Account accountTmp;
							accountTmp.publicAddr = MSIP::PjToStr(&acc_cfg.rtp_cfg.public_addr);
							CStringA str = CStringA(get_public_addr(&accountTmp));
							pj_str_t new_pub_addr = pj_str((char*)str.GetBuffer());
							if (pj_strcmp(&acc_cfg.rtp_cfg.public_addr, &new_pub_addr) != 0) {
								pj_strdup(tmp_pool, &acc_cfg.rtp_cfg.public_addr, &new_pub_addr);
								pjsua_acc_modify(ids[i], &acc_cfg);
							}
						}
					}
					pj_pool_release(tmp_pool);
				}
			}
		}
	}
}

LRESULT CmainDlg::OnPowerBroadcast(WPARAM wParam, LPARAM lParam)
{
	if (wParam == PBT_APMRESUMEAUTOMATIC) {
		PJCreate();
		PJAccountAdd();
	}
	else if (wParam == PBT_APMSUSPEND) {
		PJDestroy();
	}
	return TRUE;
}

LRESULT CmainDlg::OnAccount(WPARAM wParam, LPARAM lParam)
{
	if (!accountDlg) {
		accountDlg = new AccountDlg(this);
	}
	else {
		accountDlg->SetForegroundWindow();
	}
	if (accountDlg) {
		accountDlg->Load(accountSettings.accountId ? accountSettings.accountId : -1);
		if (wParam && accountDlg) {
			CEdit* edit = (CEdit*)accountDlg->GetDlgItem(IDC_EDIT_PASSWORD);
			if (edit) {
				edit->SetFocus();
				int nLength = edit->GetWindowTextLength();
				edit->SetSel(nLength, nLength);
			}
		}
	}
	return 0;
}

void CmainDlg::OnTimerProgress()
{
}

void CmainDlg::OnTimerCall()
{
	pjsua_call_id call_id;
	int duration = messagesDlg->GetCallDuration(&call_id);
	if (duration != -1) {
		CString str;
		unsigned icon = IDI_ACTIVE;
		if (call_id != PJSUA_INVALID_ID) {
			int holdFrom = -1;
			if (is_pjsua_running()) {
				call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_id);
				if (user_data) {
					user_data->CS.Lock();
					holdFrom = user_data->holdFrom;
					user_data->CS.Unlock();
				}
			}
			if (holdFrom != -1) {
				icon = IDI_HOLD;
				str.Format(_T("%s %s / %s"), Translate(_T("Hold")), MSIP::GetDuration(duration - holdFrom, true), MSIP::GetDuration(duration, true));
			}
			else {
				str.Format(_T("%s %s"), Translate(_T("Connected")), MSIP::GetDuration(duration, true));
			}
		}
		else {
			str.Format(_T("%s (%d)"), Translate(_T("Connected")), duration);
		}
		if (call_id != PJSUA_INVALID_ID && icon != IDI_HOLD) {
			call_user_data* user_data;
			if (is_pjsua_running()) {
				user_data = (call_user_data*)pjsua_call_get_user_data(call_id);
			}
			else {
				user_data = NULL;
			}
			if (user_data) {
				user_data->CS.Lock();
				if (user_data->srtp == MSIP_SRTP) {
					icon = IDI_ACTIVE_SECURE;
				}
				float MOS;
				if (duration > 0 && user_data && msip_call_statistics(user_data, &MOS)) {
					if (MOS <= 2) {
						icon = (icon == IDI_ACTIVE_SECURE ? IDI_ACTIVE_SECURE_RED : IDI_ACTIVE_RED);
					}
					else if (MOS <= 3) {
						icon = (icon == IDI_ACTIVE_SECURE ? IDI_ACTIVE_SECURE_YELLOW : IDI_ACTIVE_YELLOW);
					}
				}
				user_data->CS.Unlock();
			}
		}
		UpdateWindowText(str, icon);
	}
	else {
		KillTimer(IDT_TIMER_CALL);
	}
}

void CmainDlg::OnTimer(UINT_PTR TimerVal)
{
	if (TimerVal == IDT_TIMER_DIAL_TONE_OPTIONS) {
		KillTimer(IDT_TIMER_DIAL_TONE_OPTIONS);
		++m_dialToneGeneration;
		m_dialToneCheckPending = false;
		SetDialToneSessionActive(false);
		m_dialToneReadyTick = 0;
		m_dialToneReadyAccount = PJSUA_INVALID_ID;
		if (m_callTracePanel && ::IsWindow(m_callTracePanel->m_hWnd)) {
			m_callTracePanel->Append(_T("SIP           NOT READY · no OPTIONS response · 3000 ms"));
			m_callTracePanel->Append(_T("Dial-Tone     NOT READY"));
		}
	}
	else if (TimerVal == IDT_TIMER_AUTOANSWER) {
		KillTimer(IDT_TIMER_AUTOANSWER);
		if (autoAnswerTimerCallId != PJSUA_INVALID_ID) {
			AutoAnswer(autoAnswerTimerCallId);
			autoAnswerTimerCallId = PJSUA_INVALID_ID;
		}
	}
	else if (TimerVal == IDT_TIMER_FORWARDING) {
		KillTimer(IDT_TIMER_FORWARDING);
		if (forwardingTimerCallId != PJSUA_INVALID_ID) {
			messagesDlg->CallAction(MSIP_ACTION_FORWARD, _T(""), forwardingTimerCallId);
			forwardingTimerCallId = PJSUA_INVALID_ID;
		}
	}
	else if (TimerVal == IDT_TIMER_NETWORK_CHANGED) {
		KillTimer(IDT_TIMER_NETWORK_CHANGED);
		OnTimerNetworkChange();
	}
	else if (TimerVal == IDT_TIMER_SWITCH_DEVICES) {
		KillTimer(IDT_TIMER_SWITCH_DEVICES);
		if (is_pjsua_running()) {
			PJ_LOG(3, (THIS_FILENAME, "Execute refresh devices"));
			bool snd_is_active = pjsua_snd_is_active();
			bool is_ring;
			if (snd_is_active) {
				int in, out;
				if (pjsua_get_snd_dev(&in, &out) == PJ_SUCCESS) {
					is_ring = (out == msip_audio_ring);
				}
				else {
					is_ring = false;
				}
				pjsua_set_null_snd_dev();
			}
			pjmedia_aud_dev_refresh();
			UpdateSoundDevicesIds();
			if (snd_is_active) {
				msip_set_sound_device(is_ring ? msip_audio_ring : msip_audio_output, true);
			}
#ifdef _GLOBAL_VIDEO
			pjmedia_vid_subsys* vid_subsys = pjmedia_get_vid_subsys();
			if (vid_subsys->init_count) {
				pjmedia_vid_dev_refresh();
			}
#endif
			if (accountSettings.headsetSupport) {
				Hid::OpenDevice();
			}
		}
	}
	else if (TimerVal == IDT_TIMER_SAVE) {
		KillTimer(IDT_TIMER_SAVE);
		accountSettings.SettingsSave();
	}
	else if (TimerVal == IDT_TIMER_DIRECTORY) {
		UsersDirectoryLoad(true);
	}
	else if (TimerVal == IDT_TIMER_PROGRESS) {
		OnTimerProgress();
	}
	else if (TimerVal == IDT_TIMER_CALL) {
		OnTimerCall();
	}
	else
								if (TimerVal == IDT_TIMER_IDLE) {
								if (is_pjsua_running() && m_PresenceStatus != PJRPID_ACTIVITY_BUSY) {
									//--
									LASTINPUTINFO lii;
									lii.cbSize = sizeof(LASTINPUTINFO);
									if (GetLastInputInfo(&lii)) {
										if (lii.dwTime != m_lastInputTime) {
											m_lastInputTime = lii.dwTime;
											m_idleCounter = 0;
											if (m_PresenceStatus == PJRPID_ACTIVITY_AWAY) {
												PublishStatus();
											}
										}
										else {
											m_idleCounter++;
											if (m_idleCounter == 120) {
												PublishStatus(false);
											}
										}
									}
									//--
								}
							}
							else
								if (TimerVal = IDT_TIMER_TONE) {
									onPlayerPlay(MSIP_SOUND_RINGING, 0);
								}
}

void CmainDlg::PJCreate()
{
	while (!is_pjsua_running()) {
		PJCreateRaw();
		if (is_pjsua_running()) {
			break;
		}
		UpdateWindowText();
		if (AfxMessageBox(Translate(_T("Unable to initialize network sockets.")), MB_RETRYCANCEL | MB_ICONEXCLAMATION) != IDRETRY) {
			OnMenuSettings();
			break;
		}
	}
}

void CmainDlg::PJCreateRaw()
{
	player_eof_data = NULL;
	autoAnswerTimerCallId = PJSUA_INVALID_ID;
	autoAnswerPlayCallId = PJSUA_INVALID_ID;
	forwardingTimerCallId = PJSUA_INVALID_ID;

	isSubscribed = false;
	if (accountSettings.audioCodecs.IsEmpty())
	{
		accountSettings.audioCodecs = _T(_GLOBAL_CODECS_ENABLED);
	}

	// check updates
	if (upstream_updates_enabled() && accountSettings.updatesInterval != _T("never"))
	{
		CTime t = CTime::GetCurrentTime();
		time_t time = t.GetTime();
		int days;
		if (accountSettings.updatesInterval == _T("daily"))
		{
			days = 1;
		}
		else if (accountSettings.updatesInterval == _T("monthly"))
		{
			days = 30;
		}
		else if (accountSettings.updatesInterval == _T("quarterly"))
		{
			days = 90;
		}
		else
		{
			days = 7;
		}
		if (accountSettings.updatesInterval == _T("always") || accountSettings.checkUpdatesTime + days * 86400 < time) {
			CheckUpdates();
			accountSettings.checkUpdatesTime = time;
			accountSettings.SettingsSave();
		}
	}

	// pj create
	pj_status_t status;
	pjsua_config         ua_cfg;
	pjsua_media_config   media_cfg;
	pjsua_transport_config cfg;

	// Must create pjsua before anything else!
	status = pjsua_create();
	if (status != PJ_SUCCESS) {
		return;
	}

    pjsip_cfg()->endpt.disable_rport = accountSettings.rport ? PJ_FALSE : PJ_TRUE;

	// Initialize configs with default settings.
	pjsua_config_default(&ua_cfg);
	pjsua_media_config_default(&media_cfg);

	char* ua_cfg_user_agent;
	if (accountSettings.userAgent.IsEmpty()) {
		CString userAgent;
		userAgent.Format(_T("%s/%s"), _T(_GLOBAL_NAME), _T(_GLOBAL_VERSION));
		ua_cfg_user_agent = MSIP::WideCharToPjStr(userAgent);
		pj_strset2(&ua_cfg.user_agent, ua_cfg_user_agent);
	}
	else {
		ua_cfg_user_agent = MSIP::WideCharToPjStr(accountSettings.userAgent);
		pj_strset2(&ua_cfg.user_agent, ua_cfg_user_agent);
	}

	ua_cfg.cb.on_reg_started2 = &on_reg_started2;
	ua_cfg.cb.on_reg_state2 = &on_reg_state2;
	ua_cfg.cb.on_acc_send_request = &on_acc_send_request;
	ua_cfg.cb.on_call_state = &on_call_state;
	ua_cfg.cb.on_dtmf_digit = &on_dtmf_digit;
	ua_cfg.cb.on_call_tsx_state = &on_call_tsx_state;

	ua_cfg.cb.on_call_redirected = &on_call_redirected;

	ua_cfg.cb.on_call_media_state = &on_call_media_state;
	ua_cfg.cb.on_call_media_event = &on_call_media_event;
	ua_cfg.cb.on_incoming_call = &on_incoming_call;
	ua_cfg.cb.on_nat_detect = &on_nat_detect;
	ua_cfg.cb.on_buddy_state = &on_buddy_state;
	ua_cfg.cb.on_pager2 = &on_pager2;
	ua_cfg.cb.on_pager_status2 = &on_pager_status2;
	ua_cfg.cb.on_call_transfer_request2 = &on_call_transfer_request2;
	ua_cfg.cb.on_call_transfer_status = &on_call_transfer_status;

	ua_cfg.cb.on_call_replace_request2 = &on_call_replace_request2;
	ua_cfg.cb.on_call_replaced = &on_call_replaced;

	ua_cfg.cb.on_mwi_info = &on_mwi_info;

	ua_cfg.srtp_secure_signaling = 0;

	/*
	TODO: accountSettings.account: public_addr
	*/

	if (accountSettings.enableSTUN && !accountSettings.stun.IsEmpty()) {
		int pos = 0;
		int i = 0;
		while (i < 8) {
			CString resToken = accountSettings.stun.Tokenize(_T(";,"), pos);
			if (pos == -1) {
				break;
			}
			resToken.Trim();
			if (!resToken.IsEmpty()) {
				ua_cfg.stun_srv[i] = MSIP::StrToPjStr(resToken);
				i++;
			}
		}
		ua_cfg.stun_srv_cnt = i;
	}

	media_cfg.enable_ice = PJ_FALSE;

	media_cfg.no_vad = accountSettings.vad ? PJ_FALSE : PJ_TRUE;
	media_cfg.ec_tail_len = accountSettings.ec && !accountSettings.opusStereo ? 20 : 0;

	int maxClockRate = 8000;
	int maxChannelCount = 1;
	int curPos = 0;
	CString resToken = accountSettings.audioCodecs.Tokenize(_T(" "), curPos);
	while (!resToken.IsEmpty()) {
		int pos = 0;
		bool isOpus = resToken.Tokenize(_T("/"), pos) == _T("opus");
		int clockRate = 0;
		if (isOpus) {
			clockRate = 24000;
		}
		else {
			if (pos != -1) {
				clockRate = _wtoi(resToken.Tokenize(_T("/"), pos));
			}
		}
		if (clockRate > maxClockRate) {
			maxClockRate = clockRate;
		}
		if (!accountSettings.ec && !isOpus) {
			if (pos != -1) {
				BYTE channelCount = resToken.Tokenize(_T("/"), pos) == _T("2") ? 2 : 1;
				if (channelCount > maxChannelCount) {
					maxChannelCount = channelCount;
				}
			}
		}
		resToken = accountSettings.audioCodecs.Tokenize(_T(" "), curPos);
	}
	media_cfg.clock_rate = maxClockRate;
	if (accountSettings.opusStereo) {
		media_cfg.channel_count = 2;
	}
	else {
		media_cfg.channel_count = maxChannelCount;
	}

	if (accountSettings.dnsSrv && !accountSettings.dnsSrvNs.IsEmpty()) {
		int pos = 0;
		int i = 0;
		while (i < 4) {
			CString resToken = accountSettings.dnsSrvNs.Tokenize(_T(";,"), pos);
			if (pos == -1) {
				break;
			}
			resToken.Trim();
			if (!resToken.IsEmpty()) {
				ua_cfg.nameserver[i] = MSIP::StrToPjStr(resToken);
				i++;
			}
		}
		ua_cfg.nameserver_count = i;
	}

	// Initialize pjsua
	if (accountSettings.enableLog) {
		pjsua_logging_config log_cfg;
		pjsua_logging_config_default(&log_cfg);
		log_cfg.decor |= PJ_LOG_HAS_CR;
		char* buf = MSIP::WideCharToPjStr(accountSettings.logFile);
		log_cfg.log_filename = pj_str(buf);
		status = pjsua_init(&ua_cfg, &log_cfg, &media_cfg);
		free(buf);
	}
	else {
		status = pjsua_init(&ua_cfg, NULL, &media_cfg);
	}

	free(ua_cfg_user_agent);

	if (status != PJ_SUCCESS) {
		pjsua_destroy();
		return;
	}

	// Start pjsua
	status = pjsua_start();

	if (status != PJ_SUCCESS) {
		pjsua_destroy();
		return;
	}

    set_pjsua_running(true);

	// Set snd devices
	UpdateSoundDevicesIds();

	PJAudioCodecs();
#ifdef _GLOBAL_VIDEO
	PJVideoCodecs();
#endif

	// Create transport
	PJ_LOG(3, (THIS_FILENAME, "Create transport"));
	transport_udp_local = -1;
	transport_udp = -1;
	transport_tcp = -1;
	transport_tls = -1;

	pjsua_transport_config_default(&cfg);
	if (accountSettings.sourcePort) {
		cfg.port = accountSettings.sourcePort;
		status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		if (status != PJ_SUCCESS) {
			cfg.port = 0;
			pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		}
		if (MACRO_ENABLE_LOCAL_ACCOUNT) {
			if (accountSettings.sourcePort == 5060) {
				transport_udp_local = transport_udp;
			}
			else {
				cfg.port = 5060;
				status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp_local);
				if (status != PJ_SUCCESS) {
					transport_udp_local = transport_udp;
				}
			}
		}
	}
	else {
		if (MACRO_ENABLE_LOCAL_ACCOUNT) {
			cfg.port = 5060;
			status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp_local);
			if (status != PJ_SUCCESS) {
				transport_udp_local = -1;
			}
		}
		cfg.port = 0;
		pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		if (transport_udp_local == -1) {
			transport_udp_local = transport_udp;
		}
	}

		cfg.port = MACRO_ENABLE_LOCAL_ACCOUNT ? 5060 : 0;
		status = pjsua_transport_create(PJSIP_TRANSPORT_TCP, &cfg, &transport_tcp);
		if (status != PJ_SUCCESS && cfg.port) {
			cfg.port = 0;
			pjsua_transport_create(PJSIP_TRANSPORT_TCP, &cfg, &transport_tcp);
		}
		cfg.port = MACRO_ENABLE_LOCAL_ACCOUNT ? 5061 : 0;
		status = pjsua_transport_create(PJSIP_TRANSPORT_TLS, &cfg, &transport_tls);
		if (status != PJ_SUCCESS && cfg.port) {
			cfg.port = 0;
			pjsua_transport_create(PJSIP_TRANSPORT_TLS, &cfg, &transport_tls);
		}

	if (accountSettings.usersDirectory.Find(_T("%s")) == -1 && accountSettings.usersDirectory.Find(_T("{")) == -1) {
		UsersDirectoryLoad();
	}

	SetTimer(IDT_TIMER_IDLE, 5000, NULL);

	account = PJSUA_INVALID_ID;
	account_local = PJSUA_INVALID_ID;

	PJAccountAddLocal();

	if (accountSettings.headsetSupport) {
		Hid::OpenDevice();
	}
}

void CmainDlg::PJAudioCodecs()
{
	if (!is_pjsua_running()) {
		return;
	}
	//Set aud codecs prio
	PJ_LOG(3, (THIS_FILENAME, "Set audio codecs"));
	if (accountSettings.audioCodecs.GetLength())
	{
		// add unknown new codecs to the list
		unsigned count = PJMEDIA_CODEC_MGR_MAX_CODECS;
		pjsua_codec_info codec_info[PJMEDIA_CODEC_MGR_MAX_CODECS];
		if (pjsua_enum_codecs(codec_info, &count) == PJ_SUCCESS) {
			for (unsigned i = 0; i < count; i++) {
				pjsua_codec_set_priority(&codec_info[i].codec_id, PJMEDIA_CODEC_PRIO_DISABLED);
				CString rab = MSIP::PjToStr(&codec_info[i].codec_id);
				if (!audioCodecList.Find(rab)) {
					audioCodecList.AddTail(rab);
					rab.Append(_T("~"));
					audioCodecList.AddTail(rab);
				}
			}
		}
		// remove unsupported codecs from list
		POSITION pos = audioCodecList.GetHeadPosition();
		while (pos) {
			POSITION posKey = pos;
			CString key = audioCodecList.GetNext(pos);
			POSITION posValue = pos;
			CString value = audioCodecList.GetNext(pos);
			pj_str_t codec_id = MSIP::StrToPjStr(key);
			pjmedia_codec_param param;
			if (pjsua_codec_get_param(&codec_id, &param) != PJ_SUCCESS) {
				audioCodecList.RemoveAt(posKey);
				audioCodecList.RemoveAt(posValue);
			}
		};

		int curPos = 0;
		int i = PJMEDIA_CODEC_PRIO_NORMAL;
		CString resToken = accountSettings.audioCodecs.Tokenize(_T(" "), curPos);
		while (!resToken.IsEmpty()) {
			int pos = resToken.Find('/', 0);
			if (pos > 0 && resToken.Find('/', pos + 1) > 0) {
				pj_str_t codec_id = MSIP::StrToPjStr(resToken);
				pjmedia_codec_param param;
				if (pjsua_codec_get_param(&codec_id, &param) == PJ_SUCCESS) {
					if (accountSettings.opusStereo) {
						if (pj_strcmp2(&codec_id, "opus/48000/2") == 0) {
							for (int j = 0; j < param.setting.dec_fmtp.cnt; j++) {
								if (pj_strcmp2(&param.setting.dec_fmtp.param[j].name, "maxaveragebitrate") == 0) {
									param.setting.dec_fmtp.param[j].val = pj_str("96000");
								}
							}
							param.info.avg_bps = 96000;
							param.info.max_bps = 96000;
							param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].name = pj_str("stereo");
							param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].val = pj_str("1");
							param.setting.dec_fmtp.cnt++;
							pjsua_codec_set_param(&codec_id, &param);
						}
					}
					pjsua_codec_set_priority(&codec_id, i);
				}
			}
			resToken = accountSettings.audioCodecs.Tokenize(_T(" "), curPos);
			i--;
		}
	}
}

#ifdef _GLOBAL_VIDEO
void CmainDlg::PJVideoCodecs()
{
	if (!is_pjsua_running()) {
		return;
	}
	//Set vid codecs prio
	PJ_LOG(3, (THIS_FILENAME, "Set video codecs"));
	if (accountSettings.videoCodec.GetLength())
	{
		pj_str_t codec_id = MSIP::StrToPjStr(accountSettings.videoCodec);
		pjsua_vid_codec_set_priority(&codec_id, 255);
	}
	int bitrate;
	if (!accountSettings.videoH264) {
		pjsua_vid_codec_set_priority(&pj_str("H264/99"), 0);
	}
	else
	{
		const pj_str_t codec_id = { "H264/99", 7 };
		pjmedia_vid_codec_param param;
		pjsua_vid_codec_get_param(&codec_id, &param);
		if (accountSettings.videoBitrate) {
			bitrate = 1000 * accountSettings.videoBitrate;
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
		}
		pjsua_vid_codec_set_param(&codec_id, &param);
	}
	if (!accountSettings.videoH263) {
		pjsua_vid_codec_set_priority(&pj_str("H263-1998/98"), 0);
	}
	else {
		if (accountSettings.videoBitrate) {
			bitrate = 1000 * accountSettings.videoBitrate;
			const pj_str_t codec_id = { "H263-1998/98", 12 };
			pjmedia_vid_codec_param param;
			pjsua_vid_codec_get_param(&codec_id, &param);
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
			pjsua_vid_codec_set_param(&codec_id, &param);
		}
	}
	if (!accountSettings.videoVP8) {
		pjsua_vid_codec_set_priority(&pj_str("VP8/100"), 0);
	}
	else {
		if (accountSettings.videoBitrate) {
			bitrate = 1000 * accountSettings.videoBitrate;
			const pj_str_t codec_id = { "VP8/100", 7 };
			pjmedia_vid_codec_param param;
			pjsua_vid_codec_get_param(&codec_id, &param);
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
			pjsua_vid_codec_set_param(&codec_id, &param);
		}
	}
	if (!accountSettings.videoVP9) {
		pjsua_vid_codec_set_priority(&pj_str("VP9/101"), 0);
	}
	else {
		if (accountSettings.videoBitrate) {
			bitrate = 1000 * accountSettings.videoBitrate;
			const pj_str_t codec_id = { "VP9/101", 7 };
			pjmedia_vid_codec_param param;
			pjsua_vid_codec_get_param(&codec_id, &param);
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
			pjsua_vid_codec_set_param(&codec_id, &param);
		}
	}
}
#endif

void CmainDlg::UpdateSoundDevicesIds()
{
	msip_audio_input = -1;
	msip_audio_output = -2;
	msip_audio_ring = -2;
	CString audioOutputDevice = accountSettings.audioOutputDevice;
	CString audioInputDevice = accountSettings.audioInputDevice;
	unsigned count = PJMEDIA_AUD_MAX_DEVS;
	pjmedia_aud_dev_info aud_dev_info[PJMEDIA_AUD_MAX_DEVS];
	pjsua_enum_aud_devs(aud_dev_info, &count);
	for (unsigned i = 0; i < count; i++)
	{
		CString audDevName = MSIP::Utf8DecodeUni(aud_dev_info[i].name);
		if (aud_dev_info[i].input_count && !audioInputDevice.Compare(audDevName)) {
			msip_audio_input = i;
		}
		if (aud_dev_info[i].output_count) {
			if (!audioOutputDevice.Compare(audDevName)) {
				msip_audio_output = i;
			}
			if (!accountSettings.audioRingDevice.Compare(audDevName)) {
				msip_audio_ring = i;
			}
		}
	}
}

void CmainDlg::PJDestroy(bool exit)
{
	StopDialTone(_T("SIP stack stopped · READY left and dial tone stopped"));
	KillTimer(IDT_TIMER_IDLE);
	KillTimer(IDT_TIMER_CALL);

	usersDirectoryLoaded = false;
	shortcutsURLLoaded = false;
	if (is_pjsua_running()) {
		if (accountSettings.headsetSupport) {
			Hid::CloseDevice(true);
		}
		Unsubscribe();
		call_deinit_tonegen(-1);

		toneCalls.RemoveAll();

		if (IsWindow(m_hWnd)) {
			KillTimer(IDT_TIMER_TONE);
		}

		PlayerStop();

		if (player_eof_data) {
			pj_pool_release(player_eof_data->pool);
			player_eof_data = NULL;
		}

		if (accountSettings.accountId) {
			PJAccountDelete(false, exit);
		}

        set_pjsua_running(false);

		//if (transport_udp_local!=PJSUA_INVALID_ID && transport_udp_local!=transport_udp) {
		//	pjsua_transport_close(transport_udp_local,PJ_TRUE);
		//}
		if (transport_udp != PJSUA_INVALID_ID) {
			//pjsua_transport_close(transport_udp,PJ_TRUE);
		}
		//if (transport_tcp!=PJSUA_INVALID_ID) {
		//	pjsua_transport_close(transport_tcp,PJ_TRUE);
		//}
		//if (transport_tls!=PJSUA_INVALID_ID) {
		//	pjsua_transport_close(transport_tls,PJ_TRUE);
		//}
		pjsua_destroy();
		pjsua_destroy();
	}
	transport_udp_local = -1;
	transport_udp = -1;
	transport_tcp = -1;
	transport_tls = -1;
}

void CmainDlg::PJAccountConfig(pjsua_acc_config * acc_cfg, Account * account)
{
	bool isLocal = (account == &accountSettings.accountLocal);
	pjsua_acc_config_default(acc_cfg);
	// global
	acc_cfg->ka_interval = account->keepAlive;
#ifdef _GLOBAL_VIDEO
	acc_cfg->vid_in_auto_show = PJ_TRUE;
	acc_cfg->vid_out_auto_transmit = PJ_TRUE;
	acc_cfg->vid_cap_dev = VideoCaptureDeviceId();
	acc_cfg->vid_wnd_flags = PJMEDIA_VID_DEV_WND_BORDER | PJMEDIA_VID_DEV_WND_RESIZABLE;
#endif

	if (accountSettings.rtpPortMin > 0) {
		acc_cfg->rtp_cfg.port = accountSettings.rtpPortMin;
		if (accountSettings.rtpPortMax > accountSettings.rtpPortMin) {
			acc_cfg->rtp_cfg.port_range = accountSettings.rtpPortMax - accountSettings.rtpPortMin;
		}
	}
	// account
	if (account->disableSessionTimer) {
		acc_cfg->use_timer = PJSUA_SIP_TIMER_INACTIVE;
	}

	acc_cfg->reg_timeout = account->registerRefresh;

	if (account->srtp == _T("optional")) {
		acc_cfg->use_srtp = PJMEDIA_SRTP_OPTIONAL;
	}
	else if (account->srtp == _T("mandatory")) {
		acc_cfg->use_srtp = PJMEDIA_SRTP_MANDATORY;
	}
    else if (account->srtp == _T("dtls-sdes")) {
        acc_cfg->use_srtp = PJMEDIA_SRTP_MANDATORY;
        acc_cfg->enable_rtcp_mux = PJ_TRUE;
        acc_cfg->srtp_opt.keying_count = 2;
        acc_cfg->srtp_opt.keying[0] = PJMEDIA_SRTP_KEYING_DTLS_SRTP;
        acc_cfg->srtp_opt.keying[1] = PJMEDIA_SRTP_KEYING_SDES;
    }
    else if (account->srtp == _T("dtls")) {
        acc_cfg->use_srtp = PJMEDIA_SRTP_MANDATORY;
        acc_cfg->enable_rtcp_mux = PJ_TRUE;
        acc_cfg->srtp_opt.keying_count = 1;
        acc_cfg->srtp_opt.keying[0] = PJMEDIA_SRTP_KEYING_DTLS_SRTP;
    }
    else {
		acc_cfg->use_srtp = PJMEDIA_SRTP_DISABLED;
	}
	if (!accountSettings.enableSTUN || accountSettings.stun.IsEmpty()) {
		acc_cfg->rtp_cfg.public_addr = MSIP::StrToPjStr(get_public_addr(account));
	}
	acc_cfg->ice_cfg_use = PJSUA_ICE_CONFIG_USE_CUSTOM;
	acc_cfg->ice_cfg.enable_ice = account->ice ? PJ_TRUE : PJ_FALSE;
	acc_cfg->allow_via_rewrite = account->allowRewrite ? PJ_TRUE : PJ_FALSE;
	acc_cfg->allow_sdp_nat_rewrite = acc_cfg->allow_via_rewrite;
	acc_cfg->allow_contact_rewrite = acc_cfg->allow_via_rewrite ? 2 : PJ_FALSE;
    acc_cfg->contact_rewrite_method = PJSUA_CONTACT_REWRITE_ALWAYS_UPDATE | PJSUA_CONTACT_REWRITE_UNREGISTER;

	acc_cfg->publish_enabled = account->publish ? PJ_TRUE : PJ_FALSE;

	if (!account->voicemailNumber.IsEmpty()) {
		acc_cfg->mwi_enabled = PJ_TRUE;
	}

	if (account->transport == _T("udp") && transport_udp != -1) {
		acc_cfg->transport_id = transport_udp;
	}
	else if (account->transport == _T("tcp") && transport_tcp != -1) {
		if (isLocal) {
			acc_cfg->transport_id = transport_tcp;
		}
	}
	else if (account->transport == _T("tls") && transport_tls != -1) {
		if (isLocal) {
			acc_cfg->transport_id = transport_tls;
		}
	}

	acc_cfg->cred_count = 1;
	acc_cfg->cred_info[0].username = MSIP::StrToPjStr(!account->authID.IsEmpty() ? account->authID : (isLocal ? account->username : get_account_username()));
	acc_cfg->cred_info[0].realm = pj_str("*");
	acc_cfg->cred_info[0].scheme = pj_str("Digest");
	if (!account->digest.IsEmpty()) {
		acc_cfg->cred_info[0].data_type = PJSIP_CRED_DATA_DIGEST;
		acc_cfg->cred_info[0].data = MSIP::StrToPjStr(account->digest);
	}
	else {
		acc_cfg->cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
		acc_cfg->cred_info[0].data = MSIP::StrToPjStr((isLocal ? account->password : get_account_password()));
	}

	CStringList proxies;
	get_account_proxy(account, proxies);
	acc_cfg->proxy_cnt = proxies.GetCount();
	POSITION pos = proxies.GetHeadPosition();
	int i = 0;
	while (pos) {
		CString proxy = proxies.GetNext(pos);
		proxy.Format(_T("sip:%s"), proxy);
		if (account->port > 0) {
			proxy.AppendFormat(_T(":%d"), account->port);
		}
		AddTransportSuffix(proxy, account);
		acc_cfg->proxy[i] = MSIP::StrToPjStr(proxy);
		i++;
	}
	if (isLocal) {
		acc_cfg->sip_stun_use = PJSUA_STUN_USE_DISABLED;
		acc_cfg->media_stun_use = PJSUA_STUN_USE_DISABLED;
	}
}

/**
 * Add account is not exists.
 */
void CmainDlg::PJAccountAdd()
{
	if (!is_pjsua_running() || pjsua_acc_is_valid(account)) {
		return;
	}
	CString str;

	if (!accountSettings.accountId) {
		return;
	}
	if (accountSettings.account.username.IsEmpty()
		) {
		OnAccount(0, 0);
		return;
	}
	PJAccountAddRaw();
}

void CmainDlg::PJAccountAddRaw()
{
	CString str;

	CString title = _T(_GLOBAL_NAME_VISIBLE);
	CString titleAdder;
	CString usernameLocal;
	usernameLocal = accountSettings.account.username;
	if (!accountSettings.account.label.IsEmpty())
	{
		titleAdder = accountSettings.account.label;
	}
	else if (!accountSettings.account.displayName.IsEmpty())
	{
		titleAdder = accountSettings.account.displayName;
	}
	else if (!usernameLocal.IsEmpty())
	{
		titleAdder = usernameLocal;
	}
	if (!titleAdder.IsEmpty()) {
		title.AppendFormat(_T(" - %s"), titleAdder);
	}
	SetPaneText2(get_account_server());
	SetWindowText(title);
	pageDialer->SetName();

	pjsua_acc_config acc_cfg;
	PJAccountConfig(&acc_cfg, &accountSettings.account);

	//-- port knocker
	MSIP::PortKnock();

	//--
	bool ok = false;
	pj_status_t status = -1;
	//--
	CString localURI;
	if (!accountSettings.account.displayName.IsEmpty()) {
		localURI = _T("\"") + accountSettings.account.displayName + _T("\" ");
	}
	localURI += GetSIPURI(get_account_username());
	acc_cfg.id = MSIP::StrToPjStr(localURI);
	//--
	if (get_account_server().IsEmpty()) {
		acc_cfg.register_on_acc_add = PJ_FALSE;
	}
	else {
		CString regURI;
		regURI.Format(_T("sip:%s"), get_account_server());
		AddTransportSuffix(regURI, &accountSettings.account);
		acc_cfg.reg_uri = MSIP::StrToPjStr(regURI);
	}
	//--
	status = pjsua_acc_add(&acc_cfg, PJ_TRUE, &account);
	if (status == PJ_SUCCESS) {
		ok = true;
		if (acc_cfg.register_on_acc_add == PJ_FALSE) {
			Subscribe();
		}
	}
	if (!ok) {
		if (status != -1) {
			MSIP::ShowErrorMessage(status);
		}
		UpdateWindowText(_T(""), IDI_DEFAULT, true);
	}
	PublishStatus(true, acc_cfg.register_on_acc_add);
}

void CmainDlg::PJAccountAddLocal()
{
	if (MACRO_ENABLE_LOCAL_ACCOUNT) {
		pj_status_t status;
		pjsua_acc_config acc_cfg;
		PJAccountConfig(&acc_cfg, &accountSettings.accountLocal);

		CString localURI;
		if (!accountSettings.accountLocal.displayName.IsEmpty()) {
			localURI = _T("\"") + accountSettings.accountLocal.displayName + _T("\" ");
		}
		CString domain;
		if (!accountSettings.accountLocal.domain.IsEmpty()) {
			domain = accountSettings.accountLocal.domain;
		}
		else {
			pjsua_transport_data* t = &pjsua_var.tpdata[0];
			domain = MSIP::PjToStr(&t->local_name.host);
		}
		if (!accountSettings.accountLocal.username.IsEmpty()) {
			localURI.AppendFormat(_T("<sip:%s@%s>"), accountSettings.accountLocal.username, domain);
		}
		else {
			localURI.AppendFormat(_T("<sip:%s>"), domain);
		}

		acc_cfg.id = MSIP::StrToPjStr(localURI);
		acc_cfg.priority--;
		pjsua_acc_add(&acc_cfg, PJ_TRUE, &account_local);
		acc_cfg.priority++;
	}
}

/**
 * Delete account if exists.
 */
void CmainDlg::PJAccountDelete(bool deep, bool exit, CStringA code)
{
	Unsubscribe();
	if (pjsua_acc_is_valid(account)) {
		pjsua_acc_del(account);
		account = PJSUA_INVALID_ID;
	}

}

void CmainDlg::PJAccountDeleteLocal()
{
	if (pjsua_acc_is_valid(account_local)) {
		pjsua_acc_del(account_local);
		account_local = PJSUA_INVALID_ID;
	}
}

void CmainDlg::OnTcnSelchangeTab(NMHDR * pNMHDR, LRESULT * pResult)
{
	StopDialTone(_T("Dialler hidden · READY left and dial tone stopped"));
	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	int nTab = tab->GetCurSel();
	TC_ITEM tci;
	tci.mask = TCIF_PARAM;
	tab->GetItem(nTab, &tci);
	if (tci.lParam > 0) {
		CWnd* pWnd = (CWnd*)tci.lParam;
		if (m_tabPrev != -1) {
			tab->GetItem(m_tabPrev, &tci);
			if (tci.lParam > 0) {
				((CWnd*)tci.lParam)->ShowWindow(SW_HIDE);
			}
		}
		pWnd->ShowWindow(SW_SHOW);
		if (IsWindowVisible()) {
			pWnd->SetFocus();
		}
		if (nTab != accountSettings.activeTab) {
			accountSettings.activeTab = nTab;
			AccountSettingsPendingSave();
		}
		if (pWnd == pageCalls && missed) {
			missed = false;
			UpdateWindowText();
		}
	}
	else {
	}
	LayoutCallTracePanel();
	*pResult = 0;
}

void CmainDlg::OnTcnSelchangingTab(NMHDR * pNMHDR, LRESULT * pResult)
{
	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	m_tabPrev = tab->GetCurSel();
	*pResult = FALSE;
}

LRESULT CmainDlg::OnUpdateWindowText(WPARAM wParam, LPARAM lParam)
{
	if (wParam == 1) {
		bool show = !messagesDlg->GetCallsCount();
		if (show) {
			CString str;
			str.Format(_T("%s..."), Translate(_T("Connecting")));
			UpdateWindowText(str);
		}
	}
	else {
		UpdateWindowText(_T("-"));
	}
	return TRUE;
}

void CmainDlg::TabFocusSet()
{
	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	int nTab = tab->GetCurSel();
	TC_ITEM tci;
	tci.mask = TCIF_PARAM;
	tab->GetItem(nTab, &tci);
	if (tci.lParam > 0) {
		CWnd* pWnd = (CWnd*)tci.lParam;
		pWnd->SetFocus();
	}
}

void CmainDlg::UpdateWindowText(CString text, int icon, bool afterRegister)
{
	if (text.IsEmpty() && is_pjsua_running() && messagesDlg->GetCallsCount()) {
		return;
	}
	CString str;
	bool showAccountDlg = false;
	bool noReg = false;
	bool isOffline = false;
	if (!is_pjsua_running()) {
		isOffline = true;
	}
	else if (text.IsEmpty() || text == _T("-")) {
		pjsua_acc_id acc_id = account;
		if (is_pjsua_running() && pjsua_acc_is_valid(acc_id)) {
			pjsua_acc_info info;
			pjsua_acc_get_info(acc_id, &info);
			str = MSIP::PjToStr(&info.status_text);
			if (str != _T("Default status message")) {
				if (!info.has_registration) {
					icon = IDI_DEFAULT;
					str = Translate(_T("Idle"));
					noReg = true;
				}
				else if (str == _T("OK")) {
					if (m_PresenceStatus == PJRPID_ACTIVITY_BUSY) {
						icon = IDI_BUSY;
						str = Translate(_T("Do Not Disturb"));
					}
					else {
						if (m_PresenceStatus == PJRPID_ACTIVITY_AWAY) {
							icon = IDI_AWAY;
							str = Translate(_T("Away"));
						}
						else {
							if (accountSettings.account.transport == _T("tls") && transport_tls != -1) {
								icon = IDI_SECURE;
							}
							else {
								icon = IDI_ONLINE;
							}
							str = Translate(_T("Online"));
						}
						if (accountSettings.FWD && !accountSettings.forwardingNumber.IsEmpty()) {
							icon = IDI_FORWARDING;
							str = Translate(_T("Call Forwarding"));
						}
						else {
							if (!accountSettings.singleMode && accountSettings.AC) {
								str.AppendFormat(_T(" (%s)"), Translate(_T("Auto Conference")));
							}
							else if (accountSettings.autoAnswer == _T("button") && accountSettings.AA) {
								str.AppendFormat(_T(" (%s)"), Translate(_T("Auto Answer")));
							}
						}
					}
					if (!dialNumberDelayed.IsEmpty()) {
						DialNumber(dialNumberDelayed);
						dialNumberDelayed = _T("");
					}
				}
				else if (str == _T("In Progress")) {
					str.Format(_T("%s..."), Translate(_T("Connecting")));
				}
				else if (info.status == 401 || info.status == 403) {
					icon = IDI_OFFLINE;
					str = Translate(_T("Incorrect Password"));
					if (afterRegister) {
						if (IsWindowVisible() && !IsIconic()) {
							showAccountDlg = true;
						}
						else {
							BaloonPopup(_T(""), str);
						}
					}
				}
				else {
					if (info.status == 502) {
						str = _T("Connection Failed");
						icon = IDI_OFFLINE;
					}
					str = Translate(str.GetBuffer());
				}
			}
			else {
				str.Format(_T("%s %d"), Translate(_T("The server returned an error code:")), info.status);
			}
		}
		else {
			if (afterRegister) {
				showAccountDlg = true;
			}
			isOffline = true;
		}
	}
	else {
		str = text;
	}
	if (isOffline) {
		icon = IDI_DEFAULT;
		if (MACRO_ENABLE_LOCAL_ACCOUNT) {
			str = _T(_GLOBAL_NAME_VISIBLE);
		}
		else {
			str = Translate(_T("Offline"));
			icon = IDI_OFFLINE;
		}
	}
#ifdef _GLOABL_ICON_DEFAULT_OFFLINE
	if (icon == IDI_DEFAULT) {
		icon = IDI_OFFLINE;
	}
#endif

        m_bar.SetPaneText(0, str);

	if (icon != -1) {
		HICON hIcon = (HICON)LoadImage(
			AfxGetInstanceHandle(),
			MAKEINTRESOURCE(icon),
			IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
		m_bar.GetStatusBarCtrl().SetIcon(0, hIcon);
		iconStatusbar = icon;

		//--
		tnd.uFlags = NIF_ICON;
		if ((is_pjsua_running() && !pjsua_acc_is_valid(account) && MACRO_ENABLE_LOCAL_ACCOUNT) || ((icon != IDI_DEFAULT || noReg) && icon != IDI_OFFLINE)) {
			if (missed) {
				if (tnd.hIcon != iconMissed) {
					tnd.hIcon = iconMissed;
					Shell_NotifyIcon(NIM_MODIFY, &tnd);
				}
			}
			else {
				if (tnd.hIcon != iconSmall) {
					tnd.hIcon = iconSmall;
					Shell_NotifyIcon(NIM_MODIFY, &tnd);
				}
			}
		}
		else {
			if (tnd.hIcon != iconInactive) {
				tnd.hIcon = iconInactive;
				Shell_NotifyIcon(NIM_MODIFY, &tnd);
			}
		}
		//--
	}
	if (showAccountDlg) {
		PostMessage(UM_ON_ACCOUNT, 1);
	}
}

void CmainDlg::PublishStatus(bool online, bool init)
{
	if (!is_pjsua_running()) {
		return;
	}
	bool busy = (accountSettings.denyIncoming == _T("button") && accountSettings.DND);
	pjrpid_activity presenceStatusNew;
	pj_str_t note = pj_str("");
	if (m_PresenceStatus == PJRPID_ACTIVITY_BUSY) {
		if (!busy) {
			presenceStatusNew = PJRPID_ACTIVITY_UNKNOWN;
			note = pj_str("Idle");
		}
	}
	else {
		if (busy) {
			presenceStatusNew = PJRPID_ACTIVITY_BUSY;
			note = pj_str("Busy");
		}
		else {
			presenceStatusNew = online ? PJRPID_ACTIVITY_UNKNOWN : PJRPID_ACTIVITY_AWAY;
			note = online ? pj_str("Idle") : pj_str("Away");
		}
	}
	if (note.slen) {
		pjsua_acc_id ids[PJSUA_MAX_ACC];
		unsigned count = PJSUA_MAX_ACC;
		if (pjsua_enum_accs(ids, &count) == PJ_SUCCESS) {
			pjrpid_element pr;
			pr.type = PJRPID_ELEMENT_TYPE_PERSON;
			pr.id = pj_str(NULL);
			pr.note = pj_str(NULL);
			pr.note = note;
			pr.activity = presenceStatusNew;
			for (unsigned i = 0; i < count; i++) {
				pjsua_acc_set_online_status2(ids[i], PJ_TRUE, &pr);
			}
		}
		m_PresenceStatus = presenceStatusNew;
	}
	if (!init) {
		UpdateWindowText();
	}
}

LRESULT CmainDlg::onCopyData(WPARAM wParam, LPARAM lParam)
{
	LRESULT res = TRUE;
	if (is_pjsua_running()) {
		COPYDATASTRUCT* s = (COPYDATASTRUCT*)lParam;
		if (s) {
			CString params = (LPCTSTR)s->lpData;
			if (s->dwData == 1) {
				res = CommandLine(params);
			}
			else if (s->dwData == 2) {
				res = FALSE;
				CString* str = new CString();
				str->SetString(params);
				PostMessage(UM_ON_COMMAND_LINE, 0, (LPARAM)str);
			}
		}
	}
	return res;
}

LRESULT CmainDlg::onCommandLine(WPARAM wParam, LPARAM lParam)
{
	CString* str = (CString*)lParam;
	CommandLine(*str);
	delete str;
	return 0;
}

bool CmainDlg::CommandLine(CString params) {
	bool activate = false;
	params.Trim();
	if (params.GetAt(0) == '"' && params.GetAt(params.GetLength() - 1) == '"') {
		params = params.Mid(1, params.GetLength() - 2);
	}
	if (!params.IsEmpty()) {
		if (params.Find(_T("msip:")) == 0) {
			CString cmd = params.Mid(5);
			if (cmd == _T("minimize")) {
				ShowWindow(SW_HIDE);
			}
			else if (cmd == _T("answer")) {
				msip_call_answer();
			}
			else if (cmd == _T("hangupall")) {
				call_hangup_all_noincoming();
			}
			else if (cmd == _T("hold")) {
				messagesDlg->OnBnClickedHold();
			}
			else if (cmd.Find(_T("transfer_")) == 0) {
				messagesDlg->CallAction(MSIP_ACTION_TRANSFER, cmd.Mid(9));
			}
			else if (cmd == _T("micmute")) {
				pageDialer->MuteInput(true);
			}
			else if (cmd == _T("micunmute")) {
				pageDialer->MuteInput(false);
			}
			else if (cmd == _T("speakmute")) {
				pageDialer->MuteOutput(true);
			}
			else if (cmd == _T("speakunmute")) {
				pageDialer->MuteOutput(false);
			}
			else if (cmd == _T("micmuteclick")) {
				pageDialer->OnBnClickedMuteInput();
			}
			else if (cmd == _T("speakmuteclick")) {
				pageDialer->OnBnClickedMuteOutput();
			}
			else if (cmd == _T("micup")) {
				pageDialer->OnBnClickedPlusInput();
			}
			else if (cmd == _T("micdown")) {
				pageDialer->OnBnClickedMinusInput();
			}
			else if (cmd == _T("speakup")) {
				pageDialer->OnBnClickedPlusOutput();
			}
			else if (cmd == _T("speakdown")) {
				pageDialer->OnBnClickedMinusOutput();
			}
			else if (!cmd.IsEmpty()) {
				DialNumberFromCommandLine(cmd);
			}
			return activate;
		}
		DialNumberFromCommandLine(params);
	}
	return activate;
}

bool CmainDlg::GotoTabLParam(LPARAM lParam) {
	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	for (int i = 0; i < tab->GetItemCount(); i++) {
		TC_ITEM tci;
		tci.mask = TCIF_PARAM;
		tab->GetItem(i, &tci);
		if (tci.lParam == lParam) {
			return GotoTab(i, tab);
		}
	}
	return false;
}

bool CmainDlg::GotoTab(int i, CTabCtrl * tab) {
	if (!tab) {
		tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	}
	int nTab = tab->GetCurSel();
	if (i < 0) {
		int max = tab->GetItemCount() - 1;
		if (i == -1) {
			i = nTab < max ? nTab + 1 : 0;
		}
		else {
			i = nTab == 0 ? max : nTab - 1;
		}
	}
	if (nTab != i) {
		TC_ITEM tci;
		tci.mask = TCIF_PARAM;
		if (tab->GetItem(i, &tci) && tci.lParam < 0) {
			i = 0;
		}
		if (nTab != i) {
			LRESULT pResult;
			OnTcnSelchangingTab(NULL, &pResult);
			tab->SetCurSel(i);
			OnTcnSelchangeTab(NULL, &pResult);
			return true;
		}
	}
	return false;
}

void CmainDlg::ProcessCommand(CString str) {
}

void CmainDlg::DialNumberFromCommandLine(CString params) {
	pjsua_acc_info info;
	if (params.Mid(0, 4).CompareNoCase(_T("tel:")) == 0 || params.Mid(0, 4).CompareNoCase(_T("sip:")) == 0) {
		params = params.Mid(4);
	}
	else if (params.Mid(0, 7).CompareNoCase(_T("callto:")) == 0) {
		params = params.Mid(7);
	}
	else if (params.Mid(0, 8).CompareNoCase(_T("dialpad:")) == 0) {
		params = params.Mid(8);
	}
	else if (params.Mid(0, 5).CompareNoCase(_T("dial:")) == 0) {
		params = params.Mid(5);
	}
	if (params.Mid(0, 2) == _T("//")) {
		params = params.Mid(2);
		if (params.Right(1) == _T("/")) {
			params = params.Mid(0, params.GetLength() - 1);
		}
	}
	int pos = params.Find(_T("/account:"));
	if (pos != -1) {
		CString value = params.Mid(pos + 9);
		int pos2 = -1;
		if (!value.IsEmpty()) {
			pos2 = value.Find(_T(" "));
			if (pos2 != -1) {
				value = value.Left(pos2);
			}
			int accountId = _wtoi(value);
			if (accountId > 0) {
				Account account;
				if (accountSettings.AccountLoad(accountId, &account)) {
					int pos = params.Find(_T("/password:"));
					if (pos != -1) {
						account.password = params.Mid(pos + 10);
						accountSettings.AccountSave(accountId, &account);
						if (accountSettings.accountId == accountId) {
							PJAccountDelete();
							accountSettings.AccountLoad(accountSettings.accountId, &accountSettings.account);
							OnAccountChanged();
							PJAccountAdd();
						}
						params.Empty();
					}
					else {
						if (accountSettings.accountId != accountId) {
							if (accountSettings.accountId) {
								PJAccountDelete();
							}
							accountSettings.accountId = accountId;
							accountSettings.AccountLoad(accountSettings.accountId, &accountSettings.account);
							accountSettings.SettingsSave();
							OnAccountChanged();
							PJAccountAdd();
						}
					}

				}
				else {
					params.Empty();
				}
			}
		}
		if (pos2 == -1) {
			params.Delete(pos - 1, params.GetLength());
		}
		else {
			params.Delete(pos, pos + 9 + pos2 + 1);
		}
	}
		if (params == _T("/answer")) {
			msip_call_answer();
		}
		else if (params == _T("/hangupall")) {
			call_hangup_all_noincoming();
		}
		else if (params == _T("/hangupincoming")) {
			call_hangup_incoming();
		}
		else if (params == _T("/hangupcalling")) {
			call_hangup_calling();
		}
		else if (params.Find(_T("/dtmf:")) == 0) {
			CString value = params.Mid(6);
			if (!value.IsEmpty()) {
				mainDlg->pageDialer->DTMF(value);
			}
		}
		else if (params.Find(_T("/password:")) == 0) {
			CString value = params.Mid(10);
			password = value;
		}
		else if (params.Find(_T("/transfer:")) == 0) {
			CString value = params.Mid(10);
			if (!value.IsEmpty()) {
				messagesDlg->CallAction(MSIP_ACTION_TRANSFER, value);
			}
		}
		else {
				GotoTab(0);
				onTrayNotify(NULL, WM_LBUTTONUP);
			if (accountSettings.accountId > 0) {
				if (pjsua_acc_is_valid(account) &&
					(get_account_server().IsEmpty() ||
						(pjsua_acc_get_info(account, &info) == PJ_SUCCESS && info.status == 200)
						)
					) {
					DialNumber(params);
				}
				else {
					dialNumberDelayed = params;
				}
			}
			else {
				if (pjsua_acc_is_valid(account_local)) {
					DialNumber(params);
				}
				else if (accountSettings.enableLocalAccount) {
					dialNumberDelayed = params;
				}
			}
		}
}

void CmainDlg::DialNumber(CString params)
{
	CString number;
	CString message;
	int i = params.Find(_T(" "));
	if (i != -1) {
		number = params.Mid(0, i);
		message = params.Mid(i + 1);
		message.Trim();
	}
	else {
		number = params;
	}
	number.Replace(_T("%20"), _T(" "));
	number.Replace(_T("%2B"), _T("+"));
	number.Trim();
	if (!number.IsEmpty()) {
		if (message.IsEmpty()) {
			CString numberAdd = number;
			pageDialer->DialedAdd(numberAdd);
			MakeCall(number, false, true);
		}
		else {
			messagesDlg->SendInstantMessage(NULL, message, number);
		}
	}
}

bool CmainDlg::MakeCall(CString number, bool hasVideo, bool fromCommandLine, bool noTransform, CString name)
{
	// This fork exposes audio calling only, including command-line and legacy callers.
	hasVideo = false;
	if (accountSettings.singleMode && mainDlg->messagesDlg->GetCallsCount()) {
		GotoTab(0);
		return false;
	}
	if (!pjsua_acc_is_valid(account) && !accountSettings.enableLocalAccount && MSIP::IsPSTNNnmber(number) && !MSIP::IsIP(number)) {
		Account dummy;
		bool found = accountSettings.AccountLoad(1, &dummy);
		if (found) {
			OnMenuAccountChange(ID_ACCOUNT_CHANGE_RANGE);
		}
		else {
			MSIP::ShowErrorMessage(PJSIP_EAUTHACCNOTFOUND);
				OnAccount(0, 0);
			return false;
		}
	}
	if (MessagesOpen(number, true, noTransform, name)) {
		MessagesContact* messagesContact = messagesDlg->GetMessageContact();
		messagesContact->fromCommandLine = fromCommandLine;
		messagesDlg->Call(hasVideo);
		return true;
	}
	return false;
}

bool CmainDlg::MessagesOpen(CString number, bool forCall, bool noTransform, CString name)
{
	CString commands;
	CString numberFormated = FormatNumber(number, &commands, noTransform);
	pj_status_t pj_status = msip_verify_sip_url(numberFormated);
	if (pj_status == PJ_SUCCESS) {
		bool doNotShowMessagesWindow = false;
		if (forCall) {
			doNotShowMessagesWindow = accountSettings.singleMode;
		}
		MessagesContact* messagesContact = messagesDlg->AddTab(numberFormated, TRUE, NULL, NULL, doNotShowMessagesWindow, FALSE, number, name);
		if (messagesContact) {
			messagesContact->commands = commands;
			return true;
		}
	}
	else {
		MSIP::ShowErrorMessage(pj_status);
	}
	return false;
}

bool CmainDlg::AutoAnswer(pjsua_call_id call_id, bool force)
{
	bool allow = false;
    if (accountSettings.autoAnswerCalls == _T("all")) {
        allow = true;
    }
    else if (accountSettings.autoAnswerCalls == _T("hold")) {
        allow = !messagesDlg->GetCallsCount(false, true);
    }
    else {
        allow = !messagesDlg->GetCallsCount();
    }
	if (allow) {
		bool play = false;
		if (!force) {
			if (accountSettings.localDTMF) {
				autoAnswerPlayCallId = call_id;
				onPlayerPlay(MSIP_SOUND_RINGIN2, 0);
				play = true;
			}
		}
		if (!play) {
			pjsua_call_info call_info;
			if (!is_pjsua_running() || pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS || (call_info.state != PJSIP_INV_STATE_INCOMING && call_info.state != PJSIP_INV_STATE_EARLY)) {
				return false;
			}
			call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_id);
			if (user_data) {
				user_data->CS.Lock();
				user_data->autoAnswer = true;
				user_data->CS.Unlock();
			}
			mainDlg->PostMessage(UM_CALL_ANSWER, (WPARAM)call_id, (LPARAM)call_info.rem_vid_cnt);
		}
	}
	return allow;
}

pjsua_call_id CmainDlg::CurrentCallId()
{
	MessagesContact* messagesContact = messagesDlg->GetMessageContact();
	if (messagesContact) {
		return messagesContact->callId;
	}
	return -1;
}

CString CmainDlg::GetNameForCall(SIPURI& sipuri, call_user_data* user_data, CString& numberOriginal)
{
    CString name;
    if (user_data) {
        user_data->CS.Lock();
        if (!user_data->name.IsEmpty()) {
            name = user_data->name;
        }
        user_data->CS.Unlock();
    }
    if (name.IsEmpty()) {
    if (!accountSettings.disableNameLookup) {
        name = mainDlg->pageContacts->GetNameByNumber(!sipuri.user.IsEmpty() ? sipuri.user : sipuri.domain);
    }
        if (name.IsEmpty()) {
            if (!sipuri.name.IsEmpty()) {
                name = sipuri.name;
            }
        }
        if (name.IsEmpty() && !numberOriginal.IsEmpty()) {
            int pos = numberOriginal.Find(',');
            if (pos != -1) {
                name = numberOriginal.Left(pos);
            }
            else {
                name = numberOriginal;
            }
        }
        if (name.IsEmpty()) {
            if (!sipuri.user.IsEmpty()) {
                name = sipuri.user;
            }
            else if (!sipuri.domain.IsEmpty()) {
                name = sipuri.domain;
            }
        }
        if (user_data) {
            user_data->CS.Lock();
            user_data->name = name;
            user_data->CS.Unlock();
        }
    }
    return name;
}

void CmainDlg::ShortcutAction(Shortcut * shortcut, bool block, bool second)
{
	pjsua_call_id current_call_id;
	CString params;
	CString number = second && !shortcut->number2.IsEmpty() ? shortcut->number2 : shortcut->number;
	if (shortcut->type == MSIP_SHORTCUT_CALL) {
		if (shortcut->ringing && CommandCallPickup(number)) {
		}
		else {
			mainDlg->MakeCall(number);
		}
	}
	else if (shortcut->type == MSIP_SHORTCUT_VIDEOCALL) {
		return;
	}
	else if (shortcut->type == MSIP_SHORTCUT_MESSAGE) {
		return;
	}
	else if (shortcut->type == MSIP_SHORTCUT_DTMF) {
		mainDlg->pageDialer->DTMF(number);
	}
	else if (shortcut->type == MSIP_SHORTCUT_TRANSFER) {
		if (number.IsEmpty()) {
			OpenTransferDlg(mainDlg, MSIP_ACTION_TRANSFER);
		}
		else {
			messagesDlg->CallAction(MSIP_ACTION_TRANSFER, number);
		}
	}
	else if (shortcut->type == MSIP_SHORTCUT_ATTENDED_TRANSFER) {
		if (number.IsEmpty()) {
			OpenTransferDlg(mainDlg, MSIP_ACTION_ATTENDED_TRANSFER);
		}
		else {
			messagesDlg->CallAction(MSIP_ACTION_ATTENDED_TRANSFER, number);
		}
	}
	else if (shortcut->type == MSIP_SHORTCUT_CONFERENCE) {
		if (number.IsEmpty()) {
			OpenTransferDlg(mainDlg, MSIP_ACTION_INVITE);
		}
		else {
			messagesDlg->CallAction(MSIP_ACTION_INVITE, number);
		}
	}
	else if (shortcut->type == MSIP_SHORTCUT_RUNBATCH) {
		AfxMessageBox(_T(_GLOBAL_BUSINESS_FEATURE));
	}
	else if (shortcut->type == MSIP_SHORTCUT_CALL_URL || shortcut->type == MSIP_SHORTCUT_POP_URL) {
		AfxMessageBox(_T(_GLOBAL_BUSINESS_FEATURE));
	}
}

void CmainDlg::ShortcutsRemoveAll()
{
	for (int i = 0; i < shortcuts.GetCount(); i++) {
		Shortcut* shortcut = &shortcuts.GetAt(i);
		if (shortcut->presence) {
			shortcut->presence = false;
			mainDlg->UnsubscribeNumber(&shortcut->number);
		}
	}
	shortcuts.RemoveAll();
}

LRESULT CmainDlg::onPlayerPlay(WPARAM wParam, LPARAM lParam)
{
	CString filename;
	BOOL noLoop;
	BOOL inCall;
	if (wParam == MSIP_SOUND_CUSTOM) {
		filename = *(CString*)lParam;
		MSIP::ExpandEnvironmentStrings(filename);
		noLoop = FALSE;
		inCall = FALSE;
	}
	else if (wParam == MSIP_SOUND_CUSTOM_NOLOOP) {
		filename = *(CString*)lParam;
		MSIP::ExpandEnvironmentStrings(filename);
		noLoop = TRUE;
		inCall = FALSE;
	}
	else {
		switch (wParam) {
		case MSIP_SOUND_MESSAGE_IN:
			filename.Append(_T("msgin.wav"));
			noLoop = TRUE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_MESSAGE_OUT:
			filename.Append(_T("msgout.wav"));
			noLoop = TRUE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_HANGUP:
			filename.Append(_T("hangup.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		case MSIP_SOUND_RINGTONE:
			filename.Append(_T("ringtone.wav"));
			noLoop = FALSE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_RINGIN2:
			filename.Append(_T("ringing2.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		case MSIP_SOUND_RINGING:
			filename.Append(_T("ringing.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		default:
			noLoop = TRUE;
			inCall = FALSE;
		}
	}
	if (filename.Find('\\') == -1 && filename.Find('/') == -1) {
		filename = accountSettings.pathExe + _T("\\") + filename;
	}
	PlayerPlay(filename, noLoop, inCall);
	return 0;
}

LRESULT CmainDlg::onPlayerStop(WPARAM wParam, LPARAM lParam)
{
	PlayerStop();
	if (autoAnswerPlayCallId != PJSUA_INVALID_ID) {
		AutoAnswer(autoAnswerPlayCallId, true);
		autoAnswerPlayCallId = PJSUA_INVALID_ID;
	}
	return 0;
}

static PJ_DEF(pj_status_t) on_pjsua_wav_file_end_callback(pjmedia_port * media_port, void* args)
{
	mainDlg->PostMessage(UM_ON_PLAYER_STOP, 0, 0);
	return -1;//Here it is important to return value other than PJ_SUCCESS
}

void CmainDlg::PlayerPlay(CString filename, bool noLoop, bool inCall, bool isAA)
{
	PlayerStop();
	bool stopCallback = false;
	if (!filename.IsEmpty()) {
		pj_str_t file = MSIP::StrToPjStr(filename);
		pjsua_player_id player_id;
		if (is_pjsua_running() && pjsua_player_create(&file, noLoop ? PJMEDIA_FILE_NO_LOOP : 0, &player_id) == PJ_SUCCESS) {
			pjmedia_port* player_media_port;
			if (pjsua_player_get_port(player_id, &player_media_port) == PJ_SUCCESS) {
				if (!player_eof_data) {
					pj_pool_t* pool = pjsua_pool_create("microsip_eof_data", 512, 512);
					player_eof_data = PJ_POOL_ZALLOC_T(pool, struct player_eof_data);
					player_eof_data->pool = pool;
				}
				player_eof_data->player_id = player_id;
				if (noLoop) {
					if (pjmedia_wav_player_set_eof_cb(player_media_port, player_eof_data, &on_pjsua_wav_file_end_callback) == PJ_SUCCESS) {
						stopCallback = true;
					}
				}
				if (
					(!tone_gen && pjsua_conf_get_active_ports() <= 2)
					||
					(tone_gen && pjsua_conf_get_active_ports() <= 3)
					) {
					msip_set_sound_device(inCall ? msip_audio_output : msip_audio_ring);
				}
				pjsua_conf_port_id conf_port_id = pjsua_player_get_conf_port(player_id);
				if (inCall) {
					pjsua_conf_adjust_rx_level(conf_port_id, 0.4);
				}
				else {
					pjsua_conf_adjust_rx_level(conf_port_id, (float)accountSettings.volumeRing / 100);
				}
				pjsua_conf_connect(conf_port_id, 0);
			}
		}
		free(file.ptr);
	}
	if (noLoop && !stopCallback) {
		onPlayerStop(NULL, NULL);
	}
}

void CmainDlg::PlayerStop()
{
	if (player_eof_data && player_eof_data->player_id != PJSUA_INVALID_ID) {
		if (is_pjsua_running()) {
			pjsua_conf_disconnect(pjsua_player_get_conf_port(player_eof_data->player_id), 0);
			pjsua_player_destroy(player_eof_data->player_id);
			player_eof_data->player_id = PJSUA_INVALID_ID;
		}
		else {
			player_eof_data->player_id = PJSUA_INVALID_ID;
		}
	}
}

bool CmainDlg::CommandCallAnswer() {
	if (ringinDlgs.GetCount()) {
		RinginDlg* ringinDlg = ringinDlgs.GetAt(0);
		mainDlg->PostMessage(UM_CALL_ANSWER, (WPARAM)ringinDlg->call_id, (LPARAM)0);
		return true;
	}
	return false;
}

bool CmainDlg::CommandCallReject()
{
	if (ringinDlgs.GetCount()) {
		RinginDlg* ringinDlg = ringinDlgs.GetAt(ringinDlgs.GetCount() - 1);
		ringinDlg->OnBnClickedDecline();
		return true;
	}
	return false;
}

bool CmainDlg::CommandCallPickup(CString number)
{
	if (accountSettings.enableFeatureCodeCP && !accountSettings.featureCodeCP.IsEmpty()) {
		CString commands;
		CString numberFormated = FormatNumber(number, &commands);
		SIPURI sipuri;
		MSIP::ParseSIPURI(numberFormated, &sipuri);
		CString str = accountSettings.featureCodeCP;
		str.Append(sipuri.user);
		sipuri.user = str;
		numberFormated = MSIP::BuildSIPURI(&sipuri);
		messagesDlg->CallMake(numberFormated);
		return true;
	}
	return false;
}

LRESULT CmainDlg::onShellHookMessage(WPARAM wParam, LPARAM lParam)
{
	if (wParam == HSHELL_APPCOMMAND) {
		int nCmd = GET_APPCOMMAND_LPARAM(lParam);
		if (nCmd == APPCOMMAND_MEDIA_PLAY ||
			nCmd == APPCOMMAND_MEDIA_PLAY_PAUSE ||
			nCmd == APPCOMMAND_MEDIA_STOP) {
			if (ringinDlgs.GetCount()) {
				RinginDlg* ringinDlg = ringinDlgs.GetAt(0);
				if (nCmd == APPCOMMAND_MEDIA_STOP) {
					ringinDlg->OnBnClickedDecline();
				}
				else {
					ringinDlg->CallAccept(FALSE);
				}
			}
			else {
				if (nCmd == APPCOMMAND_MEDIA_STOP) {
					messagesDlg->OnBnClickedEnd();
				}
				else {
					CButton* callButton = (CButton*)pageDialer->GetDlgItem(IDC_CALL);
					WINDOWINFO wndInfo;
					callButton->GetWindowInfo(&wndInfo);
					bool isButtonVisisble = wndInfo.dwStyle & WS_VISIBLE;
					if (isButtonVisisble && callButton->IsWindowEnabled()) {
						pageDialer->OnBnClickedCall();
					}
					else {
						messagesDlg->OnBnClickedHold();
					}
				}
			}
		}
		else if (nCmd == APPCOMMAND_MEDIA_PAUSE) {
			messagesDlg->OnBnClickedHold();
		}
	}
	return 0;
}

LRESULT CmainDlg::onCallAnswer(WPARAM wParam, LPARAM lParam)
{
	if (is_pjsua_running()) {
		pjsua_call_id call_id = wParam;
		pjsua_call_info call_info;
		if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
			if (call_info.role == PJSIP_ROLE_UAS && (call_info.state == PJSIP_INV_STATE_INCOMING || call_info.state == PJSIP_INV_STATE_EARLY)) {
				if (lParam < 0) {
					pjsua_call_answer(call_id, -lParam, NULL, NULL);
					return 0;
				}
				if (accountSettings.singleMode) {
					call_hangup_all_noincoming();
				}
				msip_set_sound_device(msip_audio_output);
				pjsua_call_setting call_setting;
				pjsua_call_setting_default(&call_setting);
#ifdef _GLOBAL_VIDEO
				if (lParam > 0 && !accountSettings.disableVideo) {
					createPreviewWin();
					call_setting.vid_cnt = 1;
				} else {
					call_setting.vid_cnt = 0;
				}
#else
				call_setting.vid_cnt = 0;
#endif
				if (pjsua_call_answer2(call_id, &call_setting, 200, NULL, NULL) == PJ_SUCCESS) {
					callIdIncomingIgnore = MSIP::PjToStr(&call_info.call_id);
				}
				PlayerStop();
				bool restore = true;
				call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_id);
				if (user_data) {
					user_data->CS.Lock();
					if (user_data->autoAnswer) {
						if (!accountSettings.bringToFrontOnIncoming) {
							restore = false;
							if (GetForegroundWindow()->GetTopLevelParent() != this) {
                                SIPURI sipuri;
                                ParseCallSIPURI(&call_info, user_data, &sipuri);
                                CString numberOriginal;
								BaloonPopup(Translate(_T("Auto Answer")), GetNameForCall(sipuri, user_data, numberOriginal), NIIF_INFO);
							}
						}
					}
					user_data->CS.Unlock();
				}

				if (restore) {
					onTrayNotify(NULL, WM_LBUTTONUP);
				}
			}
		}
	}
	return 0;
}

LRESULT CmainDlg::onCallHangup(WPARAM wParam, LPARAM lParam)
{
	if (is_pjsua_running()) {
		pjsua_call_id call_id = wParam;
		msip_call_hangup_fast(call_id);
	}
	return 0;
}

LRESULT CmainDlg::onTabIconUpdate(WPARAM wParam, LPARAM lParam)
{
	if (lParam) {
		CallTraceOnIdentityChange((pjsua_call_id)wParam);
	}
	if (messagesDlg) {
		pjsua_call_id call_id = wParam;
		for (int i = 0; i < messagesDlg->tab->GetItemCount(); i++) {
			MessagesContact* messagesContact = messagesDlg->GetMessageContact(i);
			if (messagesContact->callId == call_id) {
				pjsua_call_info call_info;
				if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
					if (lParam) {
						call_user_data* user_data = (call_user_data*)pjsua_call_get_user_data(call_id);
						if (user_data) {
							SIPURI sipuri;
							ParseCallSIPURI(&call_info, user_data, &sipuri);
							CString numberOriginal;
							CString name = GetNameForCall(sipuri, user_data, numberOriginal);
							CString number = (!sipuri.user.IsEmpty() ? sipuri.user + _T("@") : _T("")) + sipuri.domain;
							if (accountSettings.accountId && MSIP::RemovePort(get_account_domain()) == MSIP::RemovePort(sipuri.domain)) {
								number = (!sipuri.user.IsEmpty() ? sipuri.user + _T("@") : _T("")) + get_account_domain();
							}
							messagesContact->number = number;
							messagesContact->name = name;
							if (messagesDlg->tab->GetCurSel() == i) {
								messagesDlg->SetWindowText(name);
							}
							CString tabName = name;
							user_data->CS.Lock();
							if (!user_data->diversion.IsEmpty()) {
								tabName.Format(_T("%s -> %s"), user_data->diversion, tabName);
							}
							user_data->CS.Unlock();
							tabName.Format(_T("   %s  "), tabName);
							TCITEM item;
							item.mask = TCIF_TEXT;
							item.pszText = tabName.GetBuffer();
							item.cchTextMax = 0;
							messagesDlg->tab->SetItem(i, &item);
							pageCalls->SetName(call_info.call_id, name);
							pageDialer->SetName(name);
							CStringA nameA = MSIP::Utf8EncodeUni(name);
							CStringA numberA = MSIP::Utf8EncodeUni(number);
							PJ_LOG(4, (THIS_FILENAME,
								"Call %d UI identity refreshed to '%s' and number key updated to '%s'",
								call_id,
								nameA.GetString(),
								numberA.GetString()));
						}
					}
					messagesDlg->UpdateTabIcon(messagesContact, i, &call_info);
				}
				break;
			}
		}
	}
	return 0;
}

void CmainDlg::SetPaneText2(CString str)
{
	if (str.IsEmpty()) {
		m_bar.SetPaneInfo(IDS_STATUSBAR2, IDS_STATUSBAR2, SBPS_NOBORDERS, 0);
	}
	else {
		int width;
		CDC* pDC = m_bar.GetDC();
		if (pDC && pDC->m_hAttribDC) {
			CSize size = pDC->GetTextExtent(str);
			m_bar.ReleaseDC(pDC);
			width = size.cx + MulDiv(12, dpiY, 96);
		}
		else {
			width = MulDiv(8 * str.GetLength() + 12, dpiY, 96);
		}
		m_bar.SetPaneInfo(IDS_STATUSBAR2, IDS_STATUSBAR2, SBPS_NORMAL, width);
	}
	m_bar.SetPaneText(IDS_STATUSBAR2, str);
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, IDS_STATUSBAR);
}


BOOL CmainDlg::CopyStringToClipboard(IN const CString & str)
{
	// Open the clipboard
	if (!OpenClipboard())
		return FALSE;

	// Empty the clipboard
	if (!EmptyClipboard())
	{
		CloseClipboard();
		return FALSE;
	}

	// Number of bytes to copy (consider +1 for end-of-string, and
	// properly scale byte size to sizeof(TCHAR))
	SIZE_T textCopySize = (str.GetLength() + 1) * sizeof(TCHAR);

	// Allocate a global memory object for the text
	HGLOBAL hTextCopy = GlobalAlloc(GMEM_MOVEABLE, textCopySize);
	if (hTextCopy == NULL)
	{
		CloseClipboard();
		return FALSE;
	}

	// Lock the handle, and copy source text to the buffer
	TCHAR* textCopy = reinterpret_cast<TCHAR*>(GlobalLock(
		hTextCopy));
	ASSERT(textCopy != NULL);
	StringCbCopy(textCopy, textCopySize, str.GetString());
	GlobalUnlock(hTextCopy);
	textCopy = NULL; // avoid dangling references

	// Place the handle on the clipboard
#if defined( _UNICODE )
	UINT textFormat = CF_UNICODETEXT;  // Unicode text
#else
	UINT textFormat = CF_TEXT;         // ANSI text
#endif // defined( _UNICODE )

	if (SetClipboardData(textFormat, hTextCopy) == NULL)
	{
		// Failed
		CloseClipboard();
		return FALSE;
	}

	// Release the clipboard
	CloseClipboard();

	// All right
	return TRUE;
}

void CmainDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
		if (nID == SC_CLOSE) {
			ShowWindow(SW_HIDE);
		}
		else {
			if (!accountSettings.singleMode) {
				if (nID == SC_RESTORE) {
					if (messagesDlg->tab->GetItemCount()) {
						messagesDlg->ShowWindow(SW_SHOW);
					}
				}
				if (nID == SC_MINIMIZE) {
					messagesDlg->ShowWindow(SW_HIDE);
				}
			}
			__super::OnSysCommand(nID, lParam);
		}

}

BOOL CmainDlg::OnQueryEndSession()
{
	return TRUE;
}

void CmainDlg::OnClose()
{
	DestroyWindow();
}

HBRUSH CmainDlg::OnCtlColor(CDC * pDC, CWnd * pWnd, UINT nCtlColor)
{
	if (accountSettings.darkMode && (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_EDIT)) {
		static CBrush darkBrush(DarkPalette::Window());
		pDC->SetTextColor(DarkPalette::Text());
		pDC->SetBkColor(DarkPalette::Window());
		return darkBrush;
	}
	HBRUSH br = CBaseDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	return br;
}

void CmainDlg::OnContextMenu(CWnd * pWnd, CPoint point)
{
	CPoint local = point;
	ScreenToClient(&local);
	CRect rect;
	GetClientRect(&rect);
	int height = MulDiv(16, dpiY, 96);
	if (rect.Height() - local.y <= height) {
		MainPopupMenu();
	}
	else {
		DefWindowProc(WM_CONTEXTMENU, NULL, MAKELPARAM(point.x, point.y));
	}
}

BOOL CmainDlg::OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
{
	if (nEventType == DBT_DEVNODES_CHANGED) {
		StopDialTone(_T("Audio devices changed · READY left and dial tone stopped"));
		if (is_pjsua_running()) {
			if (dwData == 1) {
				PJ_LOG(3, (THIS_FILENAME, "OnDeviceStateChanged event, schedule refresh devices"));
			}
			else {
				PJ_LOG(3, (THIS_FILENAME, "WM_DEVICECHANGE received, schedule refresh devices"));
			}
			KillTimer(IDT_TIMER_SWITCH_DEVICES);
			SetTimer(IDT_TIMER_SWITCH_DEVICES, 1500, NULL);
		}
	}
	return FALSE;
}

void CmainDlg::OnSessionChange(UINT nSessionState, UINT nId)
{
	if (nSessionState == WTS_REMOTE_CONNECT || nSessionState == WTS_CONSOLE_CONNECT) {
		if (is_pjsua_running()) {
			PJ_LOG(3, (THIS_FILENAME, "WM_WTSSESSION_CHANGE received, schedule refresh devices"));
			KillTimer(IDT_TIMER_SWITCH_DEVICES);
			SetTimer(IDT_TIMER_SWITCH_DEVICES, 1500, NULL);
		}
	}
}

void CmainDlg::OnMove(int x, int y)
{
	if (pageDialer) {
		pageDialer->RepositionAccountSidecar();
	}
	if (IsWindowVisible() && !IsZoomed() && !IsIconic()) {
		CRect cRect;
		GetWindowRect(&cRect);
		accountSettings.mainX = cRect.left;
		accountSettings.mainY = cRect.top;
		AccountSettingsPendingSave();
	}
}

void CmainDlg::OnSize(UINT type, int w, int h)
{
	CBaseDialog::OnSize(type, w, h);
	LayoutFreepbxFooter();
	if (type == SIZE_MINIMIZED) {
		StopDialTone(_T("Application hidden · READY left and dial tone stopped"));
		// Free the reserved strip while hidden; the logical dock edge is kept for restore.
		AppBarRemove();
	}
	else if (type == SIZE_RESTORED && m_docked && !m_appBarRegistered) {
		AppBarUpdateDock(false);
	}
	if (this->IsWindowVisible() && type == SIZE_RESTORED) {
		CRect cRect;
		GetWindowRect(&cRect);
		accountSettings.mainW = cRect.Width();
		accountSettings.mainH = cRect.Height();
		AccountSettingsPendingSave();
	}
}

// Aero Snap can reposition the HWND before WM_EXITSIZEMOVE, so remember where the user actually dragged.
void CmainDlg::OnMoving(UINT side, LPRECT rect)
{
	CBaseDialog::OnMoving(side, rect);
	m_dragWindowRect = *rect;
	m_dragValid = true;
}

// Hiding to the tray does not raise SIZE_MINIMIZED, so release/restore the dock here too.
void CmainDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CBaseDialog::OnShowWindow(bShow, nStatus);
	if (!bShow) {
		StopDialTone(_T("Application hidden · READY left and dial tone stopped"));
		AppBarRemove();
	}
	else if (m_docked && !m_appBarRegistered) {
		AppBarUpdateDock(false);
	}
}

// Release the reservation the instant a drag starts; Windows clips window
// dragging to rcWork, which would otherwise still exclude our own strip.
void CmainDlg::OnEnterSizeMove()
{
	AppBarRemove();
}

void CmainDlg::OnExitSizeMove()
{
	AppBarUpdateDock(true);
	if (!m_docked) {
		SnapMainWindowToWorkArea();
	}
}

// Windows may still propose an Aero Snap size while WS_THICKFRAME is present.
void CmainDlg::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	CBaseDialog::OnWindowPosChanging(lpwndpos);
	if (m_lockedWindowWidth && !(lpwndpos->flags & SWP_NOSIZE)) {
		lpwndpos->cx = m_lockedWindowWidth;
	}
}

void CmainDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CBaseDialog::OnGetMinMaxInfo(lpMMI);
	if (!m_lockedWindowWidth) {
		return;
	}
	lpMMI->ptMinTrackSize.x = m_lockedWindowWidth;
	lpMMI->ptMaxTrackSize.x = m_lockedWindowWidth;
	if (m_appBarRegistered) {
		// While docked our own reservation is inside rcWork, so hold the current height instead.
		CRect current;
		GetWindowRect(&current);
		lpMMI->ptMinTrackSize.y = current.Height();
		lpMMI->ptMaxTrackSize.y = current.Height();
		return;
	}
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	if (!GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
		return;
	}
	CRect windowRect;
	CRect visibleRect;
	GetWindowRect(&windowRect);
	if (FAILED(DwmGetWindowAttribute(m_hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visibleRect, sizeof(visibleRect)))) {
		visibleRect = windowRect;
	}
	int targetHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top
		+ (visibleRect.top - windowRect.top) + (windowRect.bottom - visibleRect.bottom);
	lpMMI->ptMinTrackSize.y = targetHeight;
	lpMMI->ptMaxTrackSize.y = targetHeight;
}

// Docks to a monitor edge and reserves exactly the fixed softphone width via the Windows AppBar API.
void CmainDlg::AppBarUpdateDock(bool allowDockChange)
{
	if (!::IsWindow(m_hWnd) || !m_lockedWindowWidth || m_appBarPositioning) {
		return;
	}
	if (allowDockChange) {
		CRect windowRect;
		CRect visibleRect;
		GetWindowRect(&windowRect);
		if (FAILED(DwmGetWindowAttribute(m_hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visibleRect, sizeof(visibleRect)))) {
			visibleRect = windowRect;
		}
		int leftInset = visibleRect.left - windowRect.left;
		int rightInset = windowRect.right - visibleRect.right;
		CRect releaseRect = visibleRect;
		if (m_dragValid) {
			releaseRect.left = m_dragWindowRect.left + leftInset;
			releaseRect.right = m_dragWindowRect.right - rightInset;
		}
		m_dragValid = false;

		MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
		if (!GetMonitorInfo(MonitorFromRect(&releaseRect, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
			return;
		}
		int monitorWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
		int dockThreshold = monitorWidth * 5 / 100;
		bool dockLeft = releaseRect.left <= monitorInfo.rcMonitor.left + dockThreshold;
		bool dockRight = releaseRect.right >= monitorInfo.rcMonitor.right - dockThreshold;
		PJ_LOG(3, (THIS_FILENAME, "AppBar dock test: releaseVisible l=%ld r=%ld monitor l=%ld r=%ld threshold=%d left=%d right=%d",
			releaseRect.left, releaseRect.right, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.right,
			dockThreshold, (int)dockLeft, (int)dockRight));
		if (!dockLeft && !dockRight) {
			AppBarRemove();
			m_docked = false;
			m_appBarMonitor.SetRectEmpty();
			return;
		}
		m_appBarEdge = dockLeft ? ABE_LEFT : ABE_RIGHT;
		m_appBarMonitor = monitorInfo.rcMonitor;
		m_docked = true;
	}
	else if (!m_docked) {
		return;
	}

	APPBARDATA data = { sizeof(APPBARDATA) };
	data.hWnd = m_hWnd;
	if (!m_appBarRegistered) {
		data.uCallbackMessage = WM_APPBAR_CALLBACK;
		UINT_PTR registered = SHAppBarMessage(ABM_NEW, &data);
		PJ_LOG(3, (THIS_FILENAME, "AppBar ABM_NEW returned: %u (hWnd=%p, callback=0x%04X)",
			(unsigned)registered, m_hWnd, (unsigned)WM_APPBAR_CALLBACK));
		if (!registered) {
			return;
		}
		m_appBarRegistered = true;
	}
	AppBarApplyPosition();
}

void CmainDlg::AppBarApplyPosition()
{
	if (!m_appBarRegistered || m_appBarPositioning || !m_lockedWindowWidth) {
		return;
	}
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	if (!GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
		return;
	}
	CRect windowRect;
	CRect visibleRect;
	GetWindowRect(&windowRect);
	if (FAILED(DwmGetWindowAttribute(m_hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visibleRect, sizeof(visibleRect)))) {
		visibleRect = windowRect;
	}
	int leftInset = visibleRect.left - windowRect.left;
	int rightInset = windowRect.right - visibleRect.right;
	int topInset = visibleRect.top - windowRect.top;
	int bottomInset = windowRect.bottom - visibleRect.bottom;
	int visibleWidth = m_lockedWindowWidth - leftInset - rightInset;

	// The HWND may still sit at an Aero Snap position, so dock against the monitor chosen at release.
	CRect dockMonitor = m_appBarMonitor.IsRectEmpty() ? CRect(monitorInfo.rcMonitor) : m_appBarMonitor;

	// Reserve using full monitor bounds so our own reservation is never fed back in.
	APPBARDATA data = { sizeof(APPBARDATA) };
	data.hWnd = m_hWnd;
	data.uEdge = m_appBarEdge;
	data.rc = dockMonitor;
	data.rc.top = monitorInfo.rcWork.top;
	data.rc.bottom = monitorInfo.rcWork.bottom;
	if (m_appBarEdge == ABE_LEFT) {
		data.rc.right = data.rc.left + visibleWidth;
	}
	else {
		data.rc.left = data.rc.right - visibleWidth;
	}
	PJ_LOG(3, (THIS_FILENAME, "AppBar requested rect: edge=%u l=%ld t=%ld r=%ld b=%ld (visibleWidth=%d)",
		(unsigned)m_appBarEdge, data.rc.left, data.rc.top, data.rc.right, data.rc.bottom, visibleWidth));
	UINT_PTR queried = SHAppBarMessage(ABM_QUERYPOS, &data);
	PJ_LOG(3, (THIS_FILENAME, "AppBar ABM_QUERYPOS returned: %u shell-adjusted rect: l=%ld t=%ld r=%ld b=%ld",
		(unsigned)queried, data.rc.left, data.rc.top, data.rc.right, data.rc.bottom));
	if (m_appBarEdge == ABE_LEFT) {
		data.rc.right = data.rc.left + visibleWidth;
	}
	else {
		data.rc.left = data.rc.right - visibleWidth;
	}
	MONITORINFO before = { sizeof(MONITORINFO) };
	GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &before);
	UINT_PTR positioned = SHAppBarMessage(ABM_SETPOS, &data);
	PJ_LOG(3, (THIS_FILENAME, "AppBar ABM_SETPOS returned: %u final appbar rect: l=%ld t=%ld r=%ld b=%ld",
		(unsigned)positioned, data.rc.left, data.rc.top, data.rc.right, data.rc.bottom));

	m_appBarPositioning = true;
	SetWindowPos(NULL, data.rc.left - leftInset, data.rc.top - topInset,
		m_lockedWindowWidth, (data.rc.bottom - data.rc.top) + topInset + bottomInset,
		SWP_NOACTIVATE | SWP_NOZORDER);
	AlignVisibleFrameVertically(data.rc);
	m_appBarPositioning = false;

	APPBARDATA changed = { sizeof(APPBARDATA) };
	changed.hWnd = m_hWnd;
	SHAppBarMessage(ABM_WINDOWPOSCHANGED, &changed);

	MONITORINFO after = { sizeof(MONITORINFO) };
	GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &after);
	PJ_LOG(3, (THIS_FILENAME, "AppBar work area before: l=%ld t=%ld r=%ld b=%ld after: l=%ld t=%ld r=%ld b=%ld",
		before.rcWork.left, before.rcWork.top, before.rcWork.right, before.rcWork.bottom,
		after.rcWork.left, after.rcWork.top, after.rcWork.right, after.rcWork.bottom));

	accountSettings.mainX = data.rc.left - leftInset;
	accountSettings.mainY = data.rc.top - topInset;
	AccountSettingsPendingSave();
}

void CmainDlg::AppBarRemove()
{
	if (!m_appBarRegistered) {
		return;
	}
	APPBARDATA data = { sizeof(APPBARDATA) };
	data.hWnd = m_hWnd;
	UINT_PTR removed = SHAppBarMessage(ABM_REMOVE, &data);
	PJ_LOG(3, (THIS_FILENAME, "AppBar ABM_REMOVE returned: %u", (unsigned)removed));
	m_appBarRegistered = false;
}

void CmainDlg::AlignVisibleFrameVertically(const CRect& targetRect)
{
	CRect windowRect;
	CRect visibleRect;
	GetWindowRect(&windowRect);
	if (FAILED(DwmGetWindowAttribute(m_hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visibleRect, sizeof(visibleRect)))) {
		return;
	}
	int deltaTop = targetRect.top - visibleRect.top;
	int deltaBottom = targetRect.bottom - visibleRect.bottom;
	if (!deltaTop && !deltaBottom) {
		return;
	}
	SetWindowPos(NULL, windowRect.left, windowRect.top + deltaTop,
		windowRect.Width(), windowRect.Height() + deltaBottom - deltaTop,
		SWP_NOACTIVATE | SWP_NOZORDER);
}

void CmainDlg::SnapMainWindowToWorkArea()
{
	if (m_snappingMainWindow || !::IsWindow(m_hWnd)) {
		return;
	}
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	if (!GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
		return;
	}
	CRect windowRect;
	CRect visibleRect;
	GetWindowRect(&windowRect);
	if (FAILED(DwmGetWindowAttribute(m_hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visibleRect, sizeof(visibleRect)))) {
		visibleRect = windowRect;
	}
	int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
	int leftInset = visibleRect.left - windowRect.left;
	int rightInset = windowRect.right - visibleRect.right;
	int topInset = visibleRect.top - windowRect.top;
	int bottomInset = windowRect.bottom - visibleRect.bottom;
	int targetTop = monitorInfo.rcWork.top - topInset;
	int targetHeight = workHeight + topInset + bottomInset;

	// Clamp the visible frame horizontally; x only, width is never changed.
	int targetLeft = windowRect.left;
	int visibleLeft = windowRect.left + leftInset;
	int visibleRight = windowRect.right - rightInset;
	if (visibleLeft < monitorInfo.rcWork.left) {
		targetLeft += monitorInfo.rcWork.left - visibleLeft;
	}
	else if (visibleRight > monitorInfo.rcWork.right) {
		targetLeft -= visibleRight - monitorInfo.rcWork.right;
	}

	if (targetLeft == windowRect.left
		&& visibleRect.top == monitorInfo.rcWork.top
		&& visibleRect.bottom == monitorInfo.rcWork.bottom) {
		return;
	}
	m_snappingMainWindow = true;
	SetWindowPos(NULL, targetLeft, targetTop, windowRect.Width(), targetHeight,
		SWP_NOACTIVATE | SWP_NOZORDER);
	AlignVisibleFrameVertically(monitorInfo.rcWork);
	m_snappingMainWindow = false;
	accountSettings.mainX = targetLeft;
	accountSettings.mainY = targetTop;
	accountSettings.mainW = windowRect.Width();
	accountSettings.mainH = targetHeight;
	AccountSettingsPendingSave();
}

void CmainDlg::LayoutFreepbxFooter()
{
	if (!m_freepbxFooter || !::IsWindow(m_freepbxFooter->m_hWnd) || !::IsWindow(m_bar.m_hWnd)) {
		return;
	}
	CRect client;
	CRect status;
	GetClientRect(&client);
	m_bar.GetWindowRect(&status);
	ScreenToClient(&status);
	int footerHeight = MulDiv(76, dpiY, 96);
	int top = max(0, status.top - footerHeight);
	m_freepbxFooter->SetWindowPos(NULL, 0, top, client.Width(), status.top - top, SWP_NOACTIVATE | SWP_NOZORDER);
	LayoutCallTracePanel();
}

// Uses the region below the lowest visible dialler control and above the footer.
void CmainDlg::LayoutCallTracePanel()
{
	if (!m_callTracePanel || !::IsWindow(m_callTracePanel->m_hWnd) || !m_freepbxFooter
		|| !::IsWindow(m_freepbxFooter->m_hWnd) || !pageDialer || !::IsWindow(pageDialer->m_hWnd)) {
		return;
	}
	if (!pageDialer->IsWindowVisible()) {
		m_callTracePanel->ShowWindow(SW_HIDE);
		return;
	}
	CRect page;
	CRect footer;
	pageDialer->GetWindowRect(&page);
	ScreenToClient(&page);
	m_freepbxFooter->GetWindowRect(&footer);
	ScreenToClient(&footer);
	int topGap = MulDiv(6, dpiY, 96);
	int footerGap = MulDiv(10, dpiY, 96);
	int controlsBottom = pageDialer->GetVisibleControlsBottom();
	int top = (controlsBottom > 0 ? controlsBottom : page.bottom) + topGap;
	int height = footer.top - footerGap - top;
	if (height < MulDiv(60, dpiY, 96)) {
		m_callTracePanel->ShowWindow(SW_HIDE);
		return;
	}
	m_callTracePanel->SetWindowPos(&wndTop, page.left, top, page.Width(), height,
		SWP_NOACTIVATE);
	m_callTracePanel->ShowPanel();
}

void CmainDlg::SetDialToneSessionActive(bool active)
{
	if (pageDialer && ::IsWindow(pageDialer->m_hWnd)) {
		pageDialer->SetDialToneSessionActive(active);
	}
}

void CmainDlg::StopDialTone(CString reason, bool retainQualification)
{
	bool wasQualifying = m_dialToneCheckPending || dial_tone_is_active();
	KillTimer(IDT_TIMER_DIAL_TONE_OPTIONS);
	++m_dialToneGeneration;
	m_dialToneCheckPending = false;
	SetDialToneSessionActive(false);
	dial_tone_stop();
	if (wasQualifying && !reason.IsEmpty() && m_callTracePanel && ::IsWindow(m_callTracePanel->m_hWnd)) {
		m_callTracePanel->Append(reason);
	}
	if (!retainQualification) {
		m_dialToneReadyTick = 0;
		m_dialToneReadyAccount = PJSUA_INVALID_ID;
		m_dialToneCallPending = false;
	}
}

void CmainDlg::DialToneTargetEntered(bool selected)
{
	StopDialTone(selected ? _T("Dial target selected · dial tone stopped")
		: _T("First dial character entered · dial tone stopped"), true);
}

void CmainDlg::DialToneCallStarting()
{
	m_dialToneCallPending = m_dialToneReadyTick && m_dialToneReadyAccount == account
		&& GetTickCount() - m_dialToneReadyTick <= 60000;
	StopDialTone(_T("INVITE requested · dial tone stopped"), m_dialToneCallPending);
}

void CmainDlg::DialToneCallCancelled()
{
	m_dialToneCallPending = false;
	m_dialToneReadyTick = 0;
	m_dialToneReadyAccount = PJSUA_INVALID_ID;
	if (m_callTracePanel && ::IsWindow(m_callTracePanel->m_hWnd)) {
		m_callTracePanel->Append(_T("INVITE was not started · qualification released"));
	}
}

void CmainDlg::BeginDialToneReadiness()
{
	StopDialTone();
	m_dialToneCheckPending = true;
	SetDialToneSessionActive(true);
	m_dialToneAudioMs = 0;
	m_dialToneBaseState = 0; // 0 READY, 1 DEGRADED, 2 NOT READY
	m_dialToneServer = get_account_server();
	m_dialToneTransport = accountSettings.account.transport;
	if (m_dialToneTransport.IsEmpty()) {
		m_dialToneTransport = _T("UDP");
	}
	else {
		m_dialToneTransport.MakeUpper();
	}
	if (!m_callTracePanel || !::IsWindow(m_callTracePanel->m_hWnd)) {
		m_dialToneCheckPending = false;
		SetDialToneSessionActive(false);
		return;
	}
	m_callTracePanel->Append(_T("Dial-Tone     CHECKING"));

	if (messagesDlg && messagesDlg->GetCallsCount()) {
		m_callTracePanel->Append(_T("Dial-Tone     NOT READY · a call is already active"));
		m_dialToneCheckPending = false;
		SetDialToneSessionActive(false);
		return;
	}
	if (!accountSettings.accountId || !is_pjsua_running() || !pjsua_acc_is_valid(account)) {
		m_callTracePanel->Append(_T("PBX           NOT READY · no enabled SIP account"));
		m_dialToneCheckPending = false;
		SetDialToneSessionActive(false);
		return;
	}
	pjsua_acc_info accountInfo;
	if (pjsua_acc_get_info(account, &accountInfo) != PJ_SUCCESS) {
		m_callTracePanel->Append(_T("PBX           NOT READY · registration state unavailable"));
		m_dialToneBaseState = 2;
	}
	else if (accountInfo.has_registration && accountInfo.status == PJSIP_SC_OK) {
		CString line;
		if (accountInfo.expires != PJSIP_EXPIRES_NOT_SPECIFIED) {
			line.Format(_T("PBX           READY · registered · refresh in %d s"), accountInfo.expires);
		}
		else {
			line = _T("PBX           READY · registered");
		}
		m_callTracePanel->Append(line);
	}
	else if (!accountInfo.has_registration) {
		m_callTracePanel->Append(_T("PBX           DEGRADED · account has no registrar"));
		m_dialToneBaseState = max(m_dialToneBaseState, 1);
	}
	else {
		CString line;
		line.Format(_T("PBX           NOT READY · registration SIP %d"), accountInfo.status);
		m_callTracePanel->Append(line);
		m_dialToneBaseState = 2;
	}

	if (m_dialToneServer.IsEmpty()) {
		m_callTracePanel->Append(_T("SIP           NOT READY · server not configured"));
		m_dialToneBaseState = 2;
	}

	pjsua_transport_id transportId = transport_udp;
	if (!m_dialToneTransport.CompareNoCase(_T("TCP"))) transportId = transport_tcp;
	else if (!m_dialToneTransport.CompareNoCase(_T("TLS"))) transportId = transport_tls;
	pjsua_transport_info transportInfo;
	if (transportId == PJSUA_INVALID_ID || pjsua_transport_get_info(transportId, &transportInfo) != PJ_SUCCESS) {
		m_callTracePanel->Append(_T("SIP           NOT READY · ") + m_dialToneTransport + _T(" transport unavailable"));
		m_dialToneBaseState = 2;
	}


	UpdateSoundDevicesIds();
	unsigned deviceCount = PJMEDIA_AUD_MAX_DEVS;
	pjmedia_aud_dev_info devices[PJMEDIA_AUD_MAX_DEVS];
	bool enumerated = pjsua_enum_aud_devs(devices, &deviceCount) == PJ_SUCCESS;
	pj_status_t audioStatus = PJ_EUNKNOWN;
	DWORD audioStarted = GetTickCount();
	if (enumerated) {
		pjsua_snd_dev_param sound;
		pjsua_snd_dev_param_default(&sound);
		sound.capture_dev = msip_audio_input;
		sound.playback_dev = msip_audio_output;
		audioStatus = pjsua_set_snd_dev2(&sound);
	}
	int activeInput = msip_audio_input;
	int activeOutput = msip_audio_output;
	if (audioStatus == PJ_SUCCESS
		&& (!pjsua_snd_is_active() || pjsua_get_snd_dev(&activeInput, &activeOutput) != PJ_SUCCESS)) {
		audioStatus = PJ_EUNKNOWN;
	}
	m_dialToneAudioMs = GetTickCount() - audioStarted;
	if (audioStatus == PJ_SUCCESS) {
		CString speaker = activeOutput >= 0 && (unsigned)activeOutput < deviceCount
			? MSIP::Utf8DecodeUni(devices[activeOutput].name) : _T("System default playback");
		CString microphone = activeInput >= 0 && (unsigned)activeInput < deviceCount
			? MSIP::Utf8DecodeUni(devices[activeInput].name) : _T("System default capture");
		CString line;
		line.Format(_T("Audio         READY · %s / %s · %lu ms"), speaker, microphone, m_dialToneAudioMs);
		m_callTracePanel->Append(line);
	}
	else {
		CString configuredSpeaker = accountSettings.audioOutputDevice.IsEmpty()
			? _T("System default playback") : accountSettings.audioOutputDevice;
		CString configuredMicrophone = accountSettings.audioInputDevice.IsEmpty()
			? _T("System default capture") : accountSettings.audioInputDevice;
		CString line;
		if (!enumerated) {
			line = _T("Audio         NOT READY · device enumeration failed");
		}
		else {
			line.Format(_T("Audio         NOT READY · could not open %s / %s (%d/%d) · %s"),
				configuredSpeaker, configuredMicrophone, msip_audio_output, msip_audio_input,
				pj_error_text(audioStatus));
		}
		m_callTracePanel->Append(line);
		m_dialToneBaseState = 2;
	}

	unsigned codecCount = PJMEDIA_CODEC_MGR_MAX_CODECS;
	pjsua_codec_info codecInfo[PJMEDIA_CODEC_MGR_MAX_CODECS];
	CString codecs;
	if (pjsua_enum_codecs(codecInfo, &codecCount) == PJ_SUCCESS) {
		for (unsigned i = 0; i < codecCount; ++i) {
			if (codecInfo[i].priority) {
				if (!codecs.IsEmpty()) codecs += _T(", ");
				CString codec = MSIP::PjToStr(&codecInfo[i].codec_id);
				int slash = codec.Find(_T('/'));
				codecs += slash == -1 ? codec : codec.Left(slash);
			}
		}
	}
	if (codecs.IsEmpty()) {
		m_callTracePanel->Append(_T("Codecs        NOT READY · no enabled audio codec"));
		m_dialToneBaseState = 2;
	}
	else {
		m_callTracePanel->Append(_T("Codecs        READY · ") + codecs);
	}

	if (m_dialToneBaseState == 2) {
		m_callTracePanel->Append(_T("Dial-Tone     NOT READY"));
		m_dialToneCheckPending = false;
		SetDialToneSessionActive(false);
		return;
	}

	pj_pool_t* pool = pjsua_pool_create("dial_tone_options", 512, 512);
	pjsua_acc_config config;
	CString destination;
	if (pool && pjsua_acc_get_config(account, pool, &config) == PJ_SUCCESS) {
		destination = MSIP::PjToStr(&config.reg_uri);
	}
	if (pool) {
		pj_pool_release(pool);
	}
	if (destination.IsEmpty()) {
		destination = m_dialToneServer;
		destination.Trim();
		if (destination.Left(4).CompareNoCase(_T("sip:")) != 0
			&& destination.Left(5).CompareNoCase(_T("sips:")) != 0) {
			destination = _T("sip:") + destination;
			AddTransportSuffix(destination, &accountSettings.account);
		}
	}
	pj_str_t uri = MSIP::StrToPjStr(destination);
	pj_str_t method = MSIP::StrToPjStr(_T("OPTIONS"));
	pjsua_msg_data msgData;
	pjsua_msg_data_init(&msgData);
	msgData.target_uri = uri;
	m_callTracePanel->Append(_T("SIP           CHECKING · OPTIONS ") + sip_target_for_trace(destination));
	DialToneOptionsToken* request = new DialToneOptionsToken();
	request->window = m_hWnd;
	request->generation = m_dialToneGeneration;
	request->started = GetTickCount();
	request->pool = pjsua_pool_create("dial_tone_auth", 1024, 1024);
	request->authInitialized = false;
	request->authRetries = 0;
	pjsua_acc_config authConfig;
	pj_status_t authStatus = request->pool
		? pjsua_acc_get_config(account, request->pool, &authConfig) : PJ_ENOMEM;
	if (authStatus == PJ_SUCCESS) {
		authStatus = pjsip_auth_clt_init(&request->authSession,
			pjsua_get_pjsip_endpt(), request->pool, 0);
	}
	if (authStatus == PJ_SUCCESS) {
		authStatus = pjsip_auth_clt_set_credentials(&request->authSession,
			authConfig.cred_count, authConfig.cred_info);
	}
	if (authStatus == PJ_SUCCESS) {
		authStatus = pjsip_auth_clt_set_prefs(&request->authSession, &authConfig.auth_pref);
	}
	if (authStatus == PJ_SUCCESS) {
		request->authInitialized = true;
	}
	pj_status_t sendStatus = pjsua_acc_send_request(account, &uri, &method, NULL, request, &msgData);
	free(uri.ptr);
	free(method.ptr);
	if (sendStatus != PJ_SUCCESS) {
		destroy_dial_tone_options_token(request);
		CString error = pj_error_text(sendStatus);
		if (sendStatus == PJ_EINVAL) {
			error = _T("PJ_EINVAL · ") + error;
		}
		m_callTracePanel->Append(_T("SIP           NOT READY · OPTIONS send failed · ") + error);
		m_dialToneCheckPending = false;
		SetDialToneSessionActive(false);
		return;
	}
	SetTimer(IDT_TIMER_DIAL_TONE_OPTIONS, 3000, NULL);
}

LRESULT CmainDlg::onDialToneOptions(WPARAM, LPARAM lParam)
{
	DialToneOptionsResult* result = (DialToneOptionsResult*)lParam;
	if (!result) {
		return 0;
	}
	if (result->generation != m_dialToneGeneration) {
		delete result;
		return 0;
	}
	KillTimer(IDT_TIMER_DIAL_TONE_OPTIONS);
	m_dialToneCheckPending = false;
	int state = m_dialToneBaseState;
	CString line;
	if (result->statusCode >= 200 && result->statusCode < 300) {
		CString transport = result->activeTransport.IsEmpty() ? m_dialToneTransport : result->activeTransport;
		CString peer = result->responseSource.IsEmpty() ? m_dialToneServer : result->responseSource;
		line.Format(_T("SIP           READY · %d %s · %lu ms · %s · %s"), result->statusCode,
			result->statusText, result->elapsed, transport, peer);
		if (result->elapsed > 1000) {
			line += _T(" · slow response");
			state = max(state, 1);
		}
	}
	else if (result->authenticationFailed) {
		line.Format(_T("SIP           NOT READY · authentication failed · %d %s · %lu ms"),
			result->statusCode, result->statusText, result->elapsed);
		state = 2;
	}
	else if (result->statusCode > 0 && result->statusCode < 500) {
		line.Format(_T("SIP           DEGRADED · %d %s · %lu ms"), result->statusCode,
			result->statusText, result->elapsed);
		state = max(state, 1);
	}
	else {
		if (result->statusCode) {
			line.Format(_T("SIP           NOT READY · %d %s · %lu ms"), result->statusCode,
				result->statusText, result->elapsed);
		}
		else {
			line.Format(_T("SIP           NOT READY · no OPTIONS response · %lu ms"), result->elapsed);
		}
		state = 2;
	}
	m_callTracePanel->Append(line);

	if (state == 0) {
		CString target;
		if (pageDialer && pageDialer->GetDlgItem(IDC_NUMBER)) {
			pageDialer->GetDlgItem(IDC_NUMBER)->GetWindowText(target);
			target.Trim();
		}
		if (!target.IsEmpty()) {
			m_callTracePanel->Append(_T("Dial-Tone     READY · target entered; tone withheld"));
			m_dialToneReadyTick = GetTickCount();
			m_dialToneReadyAccount = account;
			SetDialToneSessionActive(false);
			delete result;
			return 0;
		}
		pj_status_t toneStatus = dial_tone_start();
		if (toneStatus == PJ_SUCCESS) {
			m_dialToneOptionsSamples[m_dialToneSampleNext] = result->elapsed;
			m_dialToneAudioSamples[m_dialToneSampleNext] = m_dialToneAudioMs;
			m_dialToneSampleNext = (m_dialToneSampleNext + 1) % 5;
			if (m_dialToneSampleCount < 5) ++m_dialToneSampleCount;
			m_dialToneReadyTick = GetTickCount();
			m_dialToneReadyAccount = account;
			m_callTracePanel->Append(_T("Dial-Tone     READY · start dialling"));
		}
		else {
			CString failure;
			failure.Format(_T("Dial-Tone     NOT READY · %s failed · %s"),
				MSIP::Utf8DecodeUni(dial_tone_error_stage()), pj_error_text(toneStatus));
			m_callTracePanel->Append(failure);
			SetDialToneSessionActive(false);
		}
	}
	else if (state == 1) {
		m_callTracePanel->Append(_T("Dial-Tone     DEGRADED · tone withheld"));
		SetDialToneSessionActive(false);
	}
	else {
		m_callTracePanel->Append(_T("Dial-Tone     NOT READY"));
		SetDialToneSessionActive(false);
	}
	delete result;
	return 0;
}

void CmainDlg::SetupJumpList()
{
	JumpList jl(_T(_GLOBAL_NAME_VISIBLE));
	jl.AddTasks();
}

void CmainDlg::RemoveJumpList()
{
	JumpList jl(_T(_GLOBAL_NAME_VISIBLE));
	jl.DeleteJumpList();
}

void CmainDlg::OnMenuWebsite()
{
	CString url = _T(_GLOBAL_MENU_WEBSITE);
	MSIP::OpenURL(url);
}

void CmainDlg::OnMenuHelp()
{
	OpenHelp();
}

void CmainDlg::OnMenuAddl()
{
}

void CmainDlg::OnMuteInput()
{
	pageDialer->OnBnClickedMuteInput();
}

void CmainDlg::OnMuteOutput()
{
	pageDialer->OnBnClickedMuteOutput();
}

LRESULT CmainDlg::onCustomLoaded(WPARAM wParam, LPARAM lParam)
{
	return 0;
}

LRESULT CmainDlg::onUsersDirectoryLoaded(WPARAM wParam, LPARAM lParam)
{
	CString message;
	//PJ_LOG(3, (THIS_FILENAME, "Users directory loaded"));
	URLGetAsyncData* response = (URLGetAsyncData*)wParam;
	if (response->statusCode == 0) {
		if (usersDirectorySequence == 1) {
			message = Translate(_T("Connection Failed"));
		}
		usersDirectoryReconnect++;
	}
	else {
		usersDirectoryReconnect = 0;
	if (response->statusCode >= 300) {
		if (usersDirectorySequence == 1) {
			message.Format(_T("%s %d"), Translate(_T("The server returned an error code:")), response->statusCode);
		}
	}
	else if (response->statusCode == 200 && !response->body.IsEmpty()) {
		CArray<ContactWithFields*> contacts;
		ContactWithFields* contactWithFields;
		CList<Prensence> prensences;
		BOOL ok = FALSE;
		if (response->headers.Find(_T("Content-Type: text/csv")) != -1) {
			TCHAR path[MAX_PATH];
			if (GetTempPath(MAX_PATH, path)) {
				TCHAR filename[MAX_PATH];
				if (GetTempFileName(path, _T("csv"), 0, filename)) {
					CFile tmp;
					CFileException fileException;
					if (tmp.Open(filename, CFile::modeWrite, &fileException)) {
						tmp.Write(response->body, response->body.GetLength());
						tmp.Close();
						if (pageContacts->Import(filename, contacts)) {
							ok = TRUE;
						}
					}
					DeleteFile(filename);
				}
			}
		}
		else {
			// JSON
			Json::Value root;
			Json::Value items;
			Json::Value presence;
			Json::Reader reader;
			bool parsedSuccess = reader.parse((LPCSTR)response->body,
				root,
				false);
			if (parsedSuccess) {
				//PJ_LOG(3, (THIS_FILENAME, "JSON foramt detected"));
				try {
					if (!root.isArray()) {
						if (root.isMember("refresh") && root["refresh"].isInt()) {
							usersDirectoryRefresh = root["refresh"].asInt();
						}
						if (root.isMember("silent") && root["silent"].isInt()) {
							usersDirectorySilent = root["silent"].asInt();
						}
						if (root.isMember("items") && root["items"].isArray()) {
							items = root["items"];
							ok = true;
						}
						if (root.isMember("presence") && root["presence"].isArray()) {
							presence = root["presence"];
						}
					}
					else {
						items = root;
						ok = true;
					}
					if (items.isArray()) {
						for (Json::Value::ArrayIndex i = 0; i != items.size(); i++) {
							contactWithFields = new ContactWithFields();
							contactWithFields->contact.directory = true;
							if (items[i]["name"].isString()) {
								contactWithFields->fields.AddTail(_T("name"));
								contactWithFields->contact.name = MSIP::Utf8DecodeUni(items[i]["name"].asCString());
							}
							if (items[i]["number"].isString()) {
								contactWithFields->fields.AddTail(_T("number"));
								contactWithFields->contact.number = MSIP::Utf8DecodeUni(items[i]["number"].asCString());
							}
							else if (items[i]["phone"].isString()) {
								contactWithFields->fields.AddTail(_T("number"));
								contactWithFields->contact.number = MSIP::Utf8DecodeUni(items[i]["phone"].asCString());
							}
							else if (items[i]["telephone"].isString()) {
								contactWithFields->fields.AddTail(_T("number"));
								contactWithFields->contact.number = MSIP::Utf8DecodeUni(items[i]["telephone"].asCString());
							}
							if (items[i]["firstname"].isString()) {
								contactWithFields->fields.AddTail(_T("firstname"));
								contactWithFields->contact.firstname = MSIP::Utf8DecodeUni(items[i]["firstname"].asCString());
							}
							if (items[i]["lastname"].isString()) {
								contactWithFields->fields.AddTail(_T("lastname"));
								contactWithFields->contact.lastname = MSIP::Utf8DecodeUni(items[i]["lastname"].asCString());
							}
							if (items[i]["phone"].isString()) {
								contactWithFields->fields.AddTail(_T("phone"));
								contactWithFields->contact.phone = MSIP::Utf8DecodeUni(items[i]["phone"].asCString());
							}
							if (items[i]["mobile"].isString()) {
								contactWithFields->fields.AddTail(_T("mobile"));
								contactWithFields->contact.mobile = MSIP::Utf8DecodeUni(items[i]["mobile"].asCString());
							}
							if (items[i]["email"].isString()) {
								contactWithFields->fields.AddTail(_T("email"));
								contactWithFields->contact.email = MSIP::Utf8DecodeUni(items[i]["email"].asCString());
							}
							if (items[i]["address"].isString()) {
								contactWithFields->fields.AddTail(_T("address"));
								contactWithFields->contact.address = MSIP::Utf8DecodeUni(items[i]["address"].asCString());
							}
							if (items[i]["city"].isString()) {
								contactWithFields->fields.AddTail(_T("city"));
								contactWithFields->contact.city = MSIP::Utf8DecodeUni(items[i]["city"].asCString());
							}
							if (items[i]["state"].isString()) {
								contactWithFields->fields.AddTail(_T("state"));
								contactWithFields->contact.state = MSIP::Utf8DecodeUni(items[i]["state"].asCString());
							}
							if (items[i]["zip"].isString()) {
								contactWithFields->fields.AddTail(_T("zip"));
								contactWithFields->contact.zip = MSIP::Utf8DecodeUni(items[i]["zip"].asCString());
							}
							if (items[i]["comment"].isString()) {
								contactWithFields->fields.AddTail(_T("comment"));
								contactWithFields->contact.comment = MSIP::Utf8DecodeUni(items[i]["comment"].asCString());
							}
							if (items[i]["id"].isString()) {
								contactWithFields->fields.AddTail(_T("id"));
								contactWithFields->contact.id = MSIP::Utf8DecodeUni(items[i]["id"].asCString());
							}
							if (items[i]["info"].isString()) {
								contactWithFields->fields.AddTail(_T("info"));
								contactWithFields->contact.info = MSIP::Utf8DecodeUni(items[i]["info"].asCString());
							}
							if (items[i]["presence"].isInt()) {
								contactWithFields->fields.AddTail(_T("presence"));
								contactWithFields->contact.presence = items[i]["presence"].asInt() ? 1 : 0;
							}
							if (items[i]["starred"].isInt()) {
								contactWithFields->fields.AddTail(_T("starred"));
								contactWithFields->contact.starred = items[i]["starred"].asInt() ? 1 : 0;
							}
							if (pageContacts->ContactPrepare(&contactWithFields->contact)) {
								contacts.Add(contactWithFields);
							}
							else {
								delete contactWithFields;
							}
						}
					}
					if (presence.isArray()) {
						for (Json::Value::ArrayIndex i = 0; i != presence.size(); i++) {
							if (presence[i]["number"].isString() && presence[i]["status"].isString()) {
								CString number = MSIP::Utf8DecodeUni(presence[i]["number"].asCString());
								CString status = MSIP::Utf8DecodeUni(presence[i]["status"].asCString());
								CString info;
								if (presence[i]["info"].isString()) {
									info = MSIP::Utf8DecodeUni(presence[i]["info"].asCString());
								}
								int image;
								bool ringing = false;
								if (status == _T("offline")) {
									image = MSIP_CONTACT_ICON_OFFLINE;
								}
								else if (status == _T("online")) {
									image = MSIP_CONTACT_ICON_ONLINE;
								}
								else if (status == _T("away")) {
									image = MSIP_CONTACT_ICON_AWAY;
								}
								else if (status == _T("busy")) {
									image = MSIP_CONTACT_ICON_BUSY;
								}
								else if (status == _T("ring")) {
									image = MSIP_CONTACT_ICON_ON_THE_PHONE;
									ringing = true;
								}
								else if (status == _T("phone")) {
									image = MSIP_CONTACT_ICON_ON_THE_PHONE;
								}
								else {
									image = MSIP_CONTACT_ICON_UNKNOWN;
								}
								Prensence prensence;
								prensence.number = number;
								prensence.image = image;
								prensence.ringing = ringing;
								prensence.info = info;
								prensences.AddTail(prensence);
							}
						}
					}
				}
				catch (std::exception const& e) {
				}
			}
			else {
				// XML
				//PJ_LOG(3, (THIS_FILENAME, "XML foramt detected"));
				CMarkup xml;
				BOOL bResult = xml.SetDoc(MSIP::Utf8DecodeUni(response->body));
				if (bResult) {
					ok = true;
					if (xml.FindElem(_T("contacts"))) {
						if (xml.FindAttrib(_T("refresh"))) {
							usersDirectoryRefresh = _wtoi(xml.GetAttrib(_T("refresh")));
						}
						if (xml.FindAttrib(_T("silent"))) {
							usersDirectorySilent = _wtoi(xml.GetAttrib(_T("silent")));
						}
						while (xml.FindChildElem(_T("contact"))) {
							xml.IntoElem();
							contactWithFields = new ContactWithFields();
							contactWithFields->contact.directory = true;
							if (xml.FindAttrib(_T("name"))) {
								contactWithFields->fields.AddTail(_T("name"));
								contactWithFields->contact.name = xml.GetAttrib(_T("name"));
							}
							if (xml.FindAttrib(_T("number"))) {
								contactWithFields->fields.AddTail(_T("number"));
								contactWithFields->contact.number = xml.GetAttrib(_T("number"));
							}
							if (xml.FindAttrib(_T("firstname"))) {
								contactWithFields->fields.AddTail(_T("firstname"));
								contactWithFields->contact.firstname = xml.GetAttrib(_T("firstname"));
							}
							if (xml.FindAttrib(_T("lastname"))) {
								contactWithFields->fields.AddTail(_T("lastname"));
								contactWithFields->contact.lastname = xml.GetAttrib(_T("lastname"));
							}
							if (xml.FindAttrib(_T("phone"))) {
								contactWithFields->fields.AddTail(_T("phone"));
								contactWithFields->contact.phone = xml.GetAttrib(_T("phone"));
							}
							if (xml.FindAttrib(_T("mobile"))) {
								contactWithFields->fields.AddTail(_T("mobile"));
								contactWithFields->contact.mobile = xml.GetAttrib(_T("mobile"));
							}
							if (xml.FindAttrib(_T("email"))) {
								contactWithFields->fields.AddTail(_T("email"));
								contactWithFields->contact.email = xml.GetAttrib(_T("email"));
							}
							if (xml.FindAttrib(_T("address"))) {
								contactWithFields->fields.AddTail(_T("address"));
								contactWithFields->contact.address = xml.GetAttrib(_T("address"));
							}
							if (xml.FindAttrib(_T("city"))) {
								contactWithFields->fields.AddTail(_T("city"));
								contactWithFields->contact.city = xml.GetAttrib(_T("city"));
							}
							if (xml.FindAttrib(_T("state"))) {
								contactWithFields->fields.AddTail(_T("state"));
								contactWithFields->contact.state = xml.GetAttrib(_T("state"));
							}
							if (xml.FindAttrib(_T("zip"))) {
								contactWithFields->fields.AddTail(_T("zip"));
								contactWithFields->contact.zip = xml.GetAttrib(_T("zip"));
							}
							if (xml.FindAttrib(_T("comment"))) {
								contactWithFields->fields.AddTail(_T("comment"));
								contactWithFields->contact.comment = xml.GetAttrib(_T("comment"));
							}
							if (xml.FindAttrib(_T("id"))) {
								contactWithFields->fields.AddTail(_T("id"));
								contactWithFields->contact.id = xml.GetAttrib(_T("id"));
							}
							if (xml.FindAttrib(_T("info"))) {
								contactWithFields->fields.AddTail(_T("info"));
								contactWithFields->contact.info = xml.GetAttrib(_T("info"));
							}
							if (xml.FindAttrib(_T("presence"))) {
								contactWithFields->fields.AddTail(_T("presence"));
								CString rab = xml.GetAttrib(_T("presence"));
								contactWithFields->contact.presence = rab == _T("1");
							}
							if (xml.FindAttrib(_T("starred"))) {
								contactWithFields->fields.AddTail(_T("starred"));
								CString rab = xml.GetAttrib(_T("starred"));
								contactWithFields->contact.starred = rab == _T("1");
							}
							if (pageContacts->ContactPrepare(&contactWithFields->contact)) {
								contacts.Add(contactWithFields);
							}
							else {
								delete contactWithFields;
							}
							xml.OutOfElem();
						}
					}
					else if (xml.FindElem(_T("YealinkIPPhoneBook"))) {
						while (xml.FindChildElem(_T("Menu"))) {
							xml.IntoElem();
							while (xml.FindChildElem(_T("Unit"))) {
								xml.IntoElem();
								contactWithFields = new ContactWithFields();
								contactWithFields->contact.directory = true;
								if (xml.FindAttrib(_T("Name"))) {
									contactWithFields->fields.AddTail(_T("name"));
									contactWithFields->contact.name = xml.GetAttrib(_T("Name"));
								}
								if (xml.FindAttrib(_T("Phone1"))) {
									contactWithFields->fields.AddTail(_T("number"));
									contactWithFields->contact.number = xml.GetAttrib(_T("Phone1"));
								}
								if (xml.FindAttrib(_T("Phone2"))) {
									contactWithFields->fields.AddTail(_T("phone"));
									contactWithFields->contact.phone = xml.GetAttrib(_T("Phone2"));
								}
								if (xml.FindAttrib(_T("Phone3"))) {
									contactWithFields->fields.AddTail(_T("mobile"));
									contactWithFields->contact.mobile = xml.GetAttrib(_T("Phone3"));
								}
								if (pageContacts->ContactPrepare(&contactWithFields->contact)) {
									contacts.Add(contactWithFields);
								}
								else {
									delete contactWithFields;
								}
								xml.OutOfElem();
							}
							xml.OutOfElem();
						}
					}
					else {
						while (xml.FindChildElem(_T("entry")) || xml.FindChildElem(_T("DirectoryEntry"))) {
							xml.IntoElem();
							contactWithFields = new ContactWithFields();
							contactWithFields->contact.directory = true;
							if (xml.FindChildElem(_T("extension")) || xml.FindChildElem(_T("Telephone"))) {
								contactWithFields->fields.AddTail(_T("number"));
								contactWithFields->contact.number = xml.GetChildData();
								if (xml.FindChildElem(_T("extension")) || xml.FindChildElem(_T("Telephone"))) {
									contactWithFields->fields.AddTail(_T("phone"));
									contactWithFields->contact.phone = xml.GetChildData();
								}
								if (xml.FindChildElem(_T("extension")) || xml.FindChildElem(_T("Telephone"))) {
									contactWithFields->fields.AddTail(_T("mobile"));
									contactWithFields->contact.mobile = xml.GetChildData();
								}
								xml.ResetChildPos();
							}
							if (xml.FindChildElem(_T("name")) || xml.FindChildElem(_T("Name"))) {
								contactWithFields->fields.AddTail(_T("name"));
								contactWithFields->contact.name = xml.GetChildData();
								xml.ResetChildPos();
							}
							if (xml.FindChildElem(_T("info"))) {
								contactWithFields->fields.AddTail(_T("info"));
								contactWithFields->contact.info = xml.GetChildData();
								xml.ResetChildPos();
							}
							if (xml.FindChildElem(_T("presence"))) {
								contactWithFields->fields.AddTail(_T("presence"));
								contactWithFields->contact.presence = xml.GetChildData() == _T("1");
								xml.ResetChildPos();
							}
							if (xml.FindChildElem(_T("starred"))) {
								contactWithFields->fields.AddTail(_T("starred"));
								contactWithFields->contact.starred = xml.GetChildData() == _T("1");
								xml.ResetChildPos();
							}
							if (pageContacts->ContactPrepare(&contactWithFields->contact)) {
								contacts.Add(contactWithFields);
							}
							else {
								delete contactWithFields;
							}
							xml.OutOfElem();
						}
					}
				}
			}
		}
		bool sort = false;
		if (ok) {
			pageContacts->ContactsAdd(&contacts, true);
			for (int i = 0; i < contacts.GetCount(); i++) {
				contactWithFields = contacts.GetAt(i);
				delete contactWithFields;
			}
			usersDirectoryLoaded = true;
			sort = true;
		}

		POSITION pos = prensences.GetHeadPosition();
		if (pos) {
			while (pos) {
				POSITION posKey = pos;
				Prensence prensence = prensences.GetNext(pos);
				pageContacts->PresenceReceived(&prensence.number, prensence.image, prensence.ringing, &prensence.info, true);
				pageDialer->PresenceReceived(&prensence.number, prensence.image, prensence.ringing, true);
			};
			if (!sort && pageContacts->m_SortItemsExListCtrl.GetSortColumn() == 2) {
				sort = true;
			}
		}
		if (sort) {
			pageContacts->m_SortItemsExListCtrl.SortColumn(pageContacts->m_SortItemsExListCtrl.GetSortColumn(), pageContacts->m_SortItemsExListCtrl.IsAscending());
		}
		if (usersDirectoryRefresh == -1) {
			response->headers.MakeLower();
			CString search = _T("\r\ncache-control:");
			int n = response->headers.Find(search);
			if (n > 0) {
				n = n + search.GetLength();
				int l = response->headers.Find(_T("\r\n"), n);
				if (l > 0) {
					response->headers = response->headers.Mid(n, l - n);
					search = _T("max-age=");
					n = response->headers.Find(search);
					if (n != -1) {
						response->headers = response->headers.Mid(n + search.GetLength());
						usersDirectoryRefresh = atoi(CStringA(response->headers));
					}
				}
			}
			if (usersDirectoryRefresh > 0) {
				if (usersDirectoryRefresh < 60) {
					usersDirectoryRefresh = 60;
				}
				if (usersDirectoryRefresh > 86400) {
					usersDirectoryRefresh = 86400;
				}
			}
		}
		if (usersDirectorySequence == 1) {
			if (!ok) {
				message = Translate(_T("The received data cannot be recognized"));
			}
		}
	}
	}
	if (usersDirectoryReconnect) {
		SetTimer(IDT_TIMER_DIRECTORY, 1000 * 10 * usersDirectoryReconnect * usersDirectoryReconnect * usersDirectoryReconnect, NULL);
	}
	else {
		if (usersDirectoryRefresh>0) {
			SetTimer(IDT_TIMER_DIRECTORY, 1000 * usersDirectoryRefresh, NULL);
		}
	}

	//PJ_LOG(3, (THIS_FILENAME, "End UsersDirectoryLoad"));
	delete response;

	if (message && !usersDirectorySilent) {
		BaloonPopup(Translate(_T("Directory of Users")), message, NIIF_INFO);
	}

	return 0;
}

void CmainDlg::UsersDirectoryLoad(bool update)
{
	KillTimer(IDT_TIMER_DIRECTORY);
	if (!update) {
		usersDirectorySequence = 0;
		usersDirectoryRefresh = -1;
		usersDirectorySilent = 0;
		usersDirectoryReconnect = 0;
	}
	if (!accountSettings.usersDirectory.IsEmpty()) {
		//PJ_LOG(3, (THIS_FILENAME, "Users directory load"));
		CString postData, headers;
		CString url = accountSettings.usersDirectory;
		url.Replace(_T("%"), _T("*"));
		url.Replace(_T("*s"), _T("%s"));
		url.Format(url, accountSettings.account.username, accountSettings.account.password, get_account_server());
		url.Replace(_T("*"), _T("%"));
		url = msip_url_mask(url);
		url.AppendFormat(_T("%ssequence=%d"), url.Find('?') == -1 ? _T("?") : _T("&"), usersDirectorySequence);
		usersDirectorySequence++;
		//PJ_LOG(3, (THIS_FILENAME, "Begin UsersDirectoryLoad"));
		URLGetAsync(url, m_hWnd, UM_USERS_DIRECTORY
			, false
		);
	}
}

void CmainDlg::AccountSettingsPendingSave()
{
	KillTimer(IDT_TIMER_SAVE);
	SetTimer(IDT_TIMER_SAVE, 5000, NULL);
}

void CmainDlg::OnAccountChanged(bool init)
{
	StopDialTone(_T("Account changed · READY left and dial tone stopped"));
	TrayIconUpdateTip();
	SetPaneText2(get_account_server());
	if (!init) {
		pageDialer->RebuildButtons();
	}
	pageDialer->UpdateAccountIdentity();
	LayoutCallTracePanel();
}

void CmainDlg::OpenTransferDlg(CWnd * pParent, msip_action action, pjsua_call_id call_id, Contact * selectedContact)
{
	if (mainDlg->transferDlg) {
		mainDlg->transferDlg->OnClose();
	}
	mainDlg->transferDlg = new Transfer(pParent);
	mainDlg->transferDlg->SetAction(action, call_id);
	mainDlg->transferDlg->LoadFromContacts(selectedContact);
	mainDlg->transferDlg->SetForegroundWindow();
}

void CmainDlg::OnCheckUpdates()
{
	if (!upstream_updates_enabled()) {
		return;
	}
	updateCheckerShow = true;
	CheckUpdates();
}

void CmainDlg::CheckUpdates()
{
	if (!upstream_updates_enabled()) {
		return;
	}
	CString url;
	url = _T("http://update.microsip.org/softphone-update.txt");
	url.AppendFormat(_T("?version=%s&client=%s"), _T(_GLOBAL_VERSION), CString(urlencode(MSIP::Utf8EncodeUni(CString(_T(_GLOBAL_NAME))))));
#ifndef _GLOBAL_VIDEO
	url.Append(_T("&lite=1"));
#endif
	URLGetAsync(url, m_hWnd, UM_UPDATE_CHECKER_LOADED);
}

LRESULT CmainDlg::OnUpdateCheckerLoaded(WPARAM wParam, LPARAM lParam)
{
	bool found = false;
	URLGetAsyncData* response = (URLGetAsyncData*)wParam;
	if (!upstream_updates_enabled()) {
		delete response;
		updateCheckerShow = false;
		return 0;
	}
	if (response->statusCode == 200) {
		if (!response->body.IsEmpty() && response->body.Left(4) == "http") {
			int pos = response->body.Find("\n");
			if (pos > 0) {
				CStringA url = response->body.Left(pos);
				url.Trim();
				bool allowed = false;
				DWORD dwServiceType;
				CString strServer;
				CString strObject;
				INTERNET_PORT nPort;
				if (AfxParseURL(CString(url), dwServiceType, strServer, strObject, nPort) && dwServiceType == AFX_INET_SERVICE_HTTPS && strServer.Right(13) == _T(".microsip.org")) {
					allowed = true;
				}
				int pos1 = response->body.Find("\n", pos + 1);
				if (allowed && pos1 > pos) {
					CStringA version = response->body.Mid(pos, pos1 - pos);
					version.Trim();
					CString info = MSIP::Utf8DecodeUni(response->body.Mid(pos1 + 1));
					info.Trim();
					CStringA our = _GLOBAL_VERSION;
					int count = version.Replace(".", ".");
					if (count < 4) {
						int i = count;
						while (i < 3) {
							version.Append(".0");
							i++;
						}
						count = our.Replace(".", ".");
						i = count;
						while (i < 3) {
							our.Append(".0");
							i++;
						}
						unsigned long ia = inet_addr(version.GetBuffer());
						if (ia != -1 && htonl(ia) > htonl(inet_addr(our.GetBuffer()))) {
							CString caption;
							caption.Format(_T("%s %s"), _T(_GLOBAL_NAME_VISIBLE), Translate(_T("Update Available")));
							CString message = Translate(_T("Do you want to update now?"));
							if (!info.IsEmpty()) {
								message.AppendFormat(_T("\r\n\r\n%s"), info);
							}
							found = true;
							if (::MessageBox(this->m_hWnd, message, caption, MB_YESNO | MB_ICONQUESTION) == IDYES) {
								MSIP::OpenURL(MSIP::Utf8DecodeUni(url));
							}
						}
					}
				}
			}
		}
	}
	delete response;
	if (updateCheckerShow && !found) {
		MessageBox(_T("No new version found"), _T(""), MB_ICONINFORMATION);
	}
	updateCheckerShow = false;
	return 0;
}

#ifdef _GLOBAL_VIDEO
int CmainDlg::VideoCaptureDeviceId(CString name)
{
	unsigned count = PJMEDIA_VID_DEV_MAX_DEVS;
	pjmedia_vid_dev_info vid_dev_info[PJMEDIA_VID_DEV_MAX_DEVS];
	pjsua_vid_enum_devs(vid_dev_info, &count);
	for (unsigned i = 0; i < count; i++) {
		if (vid_dev_info[i].fmt_cnt && (vid_dev_info[i].dir == PJMEDIA_DIR_ENCODING || vid_dev_info[i].dir == PJMEDIA_DIR_ENCODING_DECODING)) {
			CString vidDevName = MSIP::Utf8DecodeUni(vid_dev_info[i].name);
			if ((!name.IsEmpty() && name == vidDevName)
				||
				(name.IsEmpty() && accountSettings.videoCaptureDevice == vidDevName)) {
				return vid_dev_info[i].id;
			}
		}
	}
	return PJMEDIA_VID_DEFAULT_CAPTURE_DEV;
}

void CmainDlg::createPreviewWin()
{
	if (!previewWin) {
		previewWin = new Preview(this);
	}
	previewWin->Start(VideoCaptureDeviceId());
}
#endif

void CmainDlg::OnUpdatePane(CCmdUI* pCmdUI)
{
	pCmdUI->Enable();
}

void CmainDlg::SubsribeNumber(CString * number)
{
	if (!isSubscribed) {
		return;
	}
	if (!is_pjsua_running()) {
		return;
	}
	CString commands;
	CString numberFormated = FormatNumber(*number, &commands, true);

	pjsua_buddy_id ids[PJSUA_MAX_BUDDIES];
	unsigned count = PJSUA_MAX_BUDDIES;
	pjsua_enum_buddies(ids, &count);
	for (unsigned i = 0; i < count; i++) {
		CString* buddyNumber = (CString*)pjsua_buddy_get_user_data(ids[i]);
		if (*buddyNumber == numberFormated) {
			onBuddyState(ids[i], 0);
			return;
		}
	}
	CString numberFormatedPresence = numberFormated;
	pj_status_t status = msip_verify_sip_url(numberFormatedPresence);
	if (status == PJ_SUCCESS) {
		pjsua_acc_id acc_id;
		pj_str_t pj_uri;
		if (SelectSIPAccount(numberFormatedPresence, acc_id, &pj_uri)) {
			pjsua_buddy_id p_buddy_id;
			pjsua_buddy_config buddy_cfg;
			pjsua_buddy_config_default(&buddy_cfg);
			buddy_cfg.subscribe = PJ_TRUE;
			buddy_cfg.uri = pj_uri;
			buddy_cfg.user_data = (void*)(new CString(numberFormated));
			status = pjsua_buddy_add(&buddy_cfg, &p_buddy_id);
			free(pj_uri.ptr);
		}
	}
	if (status != PJ_SUCCESS) {
		CString str;
		str.Format(_T("%s\r\n%s"), Translate(_T("Presence Subscription")), *number);
		CString message = MSIP::GetErrorMessage(status);
		BaloonPopup(str, Translate(message.GetBuffer()), NIIF_INFO);
	}
}

void CmainDlg::UnsubscribeNumber(CString * number)
{
	if (!isSubscribed) {
		return;
	}
	if (!is_pjsua_running()) {
		return;
	}
	CString commands;
	CString numberFormated = FormatNumber(*number, &commands, true);

	pjsua_buddy_id ids[PJSUA_MAX_BUDDIES];
	unsigned count = PJSUA_MAX_BUDDIES;
	pjsua_enum_buddies(ids, &count);
	for (unsigned i = 0; i < count; i++) {
		CString* buddyNumber = (CString*)pjsua_buddy_get_user_data(ids[i]);
		if (*buddyNumber == numberFormated) {
			bool found1 = false;
			bool found2 = false;
			if (pageContacts->FindContact(numberFormated, true)) {
				found1 = true;
			}
			if (!found1) {
				for (int i = 0; i < shortcuts.GetCount(); i++) {
					Shortcut* shortcut = &shortcuts.GetAt(i);
					if (shortcut->presence) {
						CString commands;
						if (numberFormated == FormatNumber(shortcut->number, &commands, true)) {
							found2 = true;
							break;
						}
					}
				}
			}
			if (!found1 && !found2) {
				pjsua_buddy_del(ids[i]);
				delete buddyNumber;
			}
			return;
		}
	}
}

void CmainDlg::Subscribe()
{
	if (isSubscribed) {
		return;
	}
	isSubscribed = true;
	pageContacts->PresenceSubscribe();
	pageDialer->PresenceSubscribe();
}

void CmainDlg::Unsubscribe()
{
	if (!isSubscribed) {
		return;
	}
	if (is_pjsua_running()) {
		pjsua_buddy_id ids[PJSUA_MAX_BUDDIES];
		unsigned count = PJSUA_MAX_BUDDIES;
		pjsua_enum_buddies(ids, &count);
		for (unsigned i = 0; i < count; i++) {
			CString* buddyNumber = (CString*)pjsua_buddy_get_user_data(ids[i]);
			pjsua_buddy_del(ids[i]);
			delete buddyNumber;
		}
	}
	pageContacts->PresenceReset();
	pageDialer->PresenceReset();
	isSubscribed = false;
}

