/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 * Copyright (C) 2026 RQ_modding
 *
 * CrayfishLogViewer — vanilla Display Data Log extra window.
 * Game script feeds combat / item / map / event text via AddText.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#endif

#include "system4.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "xsystem4.h"

#define IDM_FILE_SAVE  1001
#define IDM_FILE_CLEAR 1002
#define IDM_FILE_CLOSE 1003
#define IDM_WIN_TOP    1101

static bool is_opened = false;
static char *window_title_utf8 = NULL;
static char *save_folder_utf8 = NULL;
static char *log_utf8 = NULL;
static size_t log_len = 0;
static size_t log_cap = 0;

#ifdef _WIN32
static HWND viewer_hwnd = NULL;
static HWND edit_hwnd = NULL;
static bool always_on_top = false;
static HINSTANCE msftedit;

static void ensure_log_cap(size_t extra)
{
	if (log_len + extra + 1 <= log_cap)
		return;
	size_t n = log_cap ? log_cap : 4096;
	while (n < log_len + extra + 1)
		n *= 2;
	log_utf8 = xrealloc(log_utf8, n);
	log_cap = n;
}

static void log_append_utf8(const char *s)
{
	size_t n = strlen(s);
	ensure_log_cap(n);
	memcpy(log_utf8 + log_len, s, n);
	log_len += n;
	log_utf8[log_len] = '\0';
}

static const char *cfl_path(void)
{
	static char path[1024];
	if (save_folder_utf8 && *save_folder_utf8) {
		snprintf(path, sizeof path, "%s/LogViewerWindowPos.sav", save_folder_utf8);
		return path;
	}
	char *p = savedir_path("LogViewerWindowPos.sav");
	snprintf(path, sizeof path, "%s", p ? p : "LogViewerWindowPos.sav");
	free(p);
	return path;
}

static void save_window_pos(void)
{
	if (!viewer_hwnd)
		return;
	RECT r;
	if (!GetWindowRect(viewer_hwnd, &r))
		return;
	uint8_t buf[24];
	memset(buf, 0, sizeof buf);
	memcpy(buf, "CFL", 3);
	LittleEndian_putDW(buf, 8, (uint32_t)r.left);
	LittleEndian_putDW(buf, 12, (uint32_t)r.top);
	LittleEndian_putDW(buf, 16, (uint32_t)(r.right - r.left));
	LittleEndian_putDW(buf, 20, (uint32_t)(r.bottom - r.top));
	file_write(cfl_path(), buf, sizeof buf);
}

static bool load_window_pos(int *x, int *y, int *w, int *h)
{
	size_t len = 0;
	uint8_t *buf = file_read(cfl_path(), &len);
	if (!buf || len < 24 || memcmp(buf, "CFL", 3) != 0) {
		free(buf);
		return false;
	}
	*x = LittleEndian_getDW(buf, 8);
	*y = LittleEndian_getDW(buf, 12);
	*w = LittleEndian_getDW(buf, 16);
	*h = LittleEndian_getDW(buf, 20);
	free(buf);
	if (*w < 200)
		*w = 640;
	if (*h < 120)
		*h = 480;
	return true;
}

static COLORREF color_for_line(const wchar_t *w)
{
	if (!w || !w[0])
		return RGB(0, 0, 0);
	if (w[0] == 0x25A0 || w[0] == 0x203B) /* ■ ※ */
		return RGB(0, 0, 180);
	if (w[0] == 0x25CB || w[0] == 0x25CF) /* ○ ● */
		return RGB(0, 80, 160);
	return RGB(0, 0, 0);
}

static void richedit_append(const char *utf8)
{
	if (!edit_hwnd || !utf8 || !utf8[0])
		return;
	wchar_t *w = utf8_to_wchar(utf8);
	if (!w)
		return;
	CHARFORMAT2W cf;
	memset(&cf, 0, sizeof cf);
	cf.cbSize = sizeof cf;
	cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
	cf.crTextColor = color_for_line(w);
	cf.yHeight = 10 * 20;
	wcscpy(cf.szFaceName, L"Segoe UI");
	SendMessageW(edit_hwnd, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
	SendMessageW(edit_hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
	SendMessageW(edit_hwnd, EM_REPLACESEL, FALSE, (LPARAM)w);
	SendMessageW(edit_hwnd, EM_SCROLLCARET, 0, 0);
	free(w);
}

static void richedit_reload(void)
{
	if (!edit_hwnd)
		return;
	SetWindowTextW(edit_hwnd, L"");
	if (log_utf8 && log_len)
		richedit_append(log_utf8);
}

static void save_log_to_file(void)
{
	int year, month, mday, wday, hour, min, sec, ms;
	get_date(&year, &month, &mday, &wday);
	get_time(&hour, &min, &sec, &ms);
	char defname[64];
	snprintf(defname, sizeof defname, "Log %04d-%02d-%02d %02d_%02d.txt",
		 year, month, mday, hour, min);

	wchar_t file[MAX_PATH];
	wchar_t *wdef = utf8_to_wchar(defname);
	wcsncpy(file, wdef ? wdef : L"log.txt", MAX_PATH - 1);
	file[MAX_PATH - 1] = 0;
	free(wdef);

	wchar_t dir[MAX_PATH];
	dir[0] = 0;
	if (save_folder_utf8) {
		wchar_t *wd = utf8_to_wchar(save_folder_utf8);
		if (wd) {
			wcsncpy(dir, wd, MAX_PATH - 1);
			dir[MAX_PATH - 1] = 0;
			free(wd);
		}
	}

	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof ofn);
	ofn.lStructSize = sizeof ofn;
	ofn.hwndOwner = viewer_hwnd;
	ofn.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrInitialDir = dir[0] ? dir : NULL;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	ofn.lpstrDefExt = L"txt";
	if (!GetSaveFileNameW(&ofn))
		return;

	char *utf_path = NULL;
#ifdef _WIN32
	int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, NULL, 0, NULL, NULL);
	utf_path = xmalloc(n);
	WideCharToMultiByte(CP_UTF8, 0, file, -1, utf_path, n, NULL, NULL);
#endif
	const char *data = log_utf8 ? log_utf8 : "";
	if (!file_write(utf_path, (uint8_t *)data, strlen(data))) {
		wchar_t msg[MAX_PATH + 64];
		_snwprintf(msg, MAX_PATH + 64, L"Failed to save [%s]", file);
		MessageBoxW(viewer_hwnd, msg, L"CrayfishLogViewer", MB_ICONERROR);
	} else {
		wchar_t msg[MAX_PATH + 64];
		_snwprintf(msg, MAX_PATH + 64, L"[%s] has been saved", file);
		MessageBoxW(viewer_hwnd, msg, L"CrayfishLogViewer", MB_OK);
	}
	free(utf_path);
}

static void destroy_viewer(void)
{
	save_window_pos();
	if (viewer_hwnd) {
		DestroyWindow(viewer_hwnd);
		viewer_hwnd = NULL;
		edit_hwnd = NULL;
	}
	is_opened = false;
}

static LRESULT CALLBACK viewer_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_SIZE:
		if (edit_hwnd) {
			RECT r;
			GetClientRect(hwnd, &r);
			MoveWindow(edit_hwnd, 0, 0, r.right, r.bottom, TRUE);
		}
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wparam)) {
		case IDM_FILE_SAVE:
			save_log_to_file();
			return 0;
		case IDM_FILE_CLEAR:
			log_len = 0;
			if (log_utf8)
				log_utf8[0] = '\0';
			if (edit_hwnd)
				SetWindowTextW(edit_hwnd, L"");
			return 0;
		case IDM_FILE_CLOSE:
			destroy_viewer();
			return 0;
		case IDM_WIN_TOP:
			always_on_top = !always_on_top;
			SetWindowPos(hwnd, always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
				     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			CheckMenuItem(GetMenu(hwnd), IDM_WIN_TOP,
				      MF_BYCOMMAND | (always_on_top ? MF_CHECKED : MF_UNCHECKED));
			return 0;
		}
		break;
	case WM_CLOSE:
		destroy_viewer();
		return 0;
	case WM_DESTROY:
		viewer_hwnd = NULL;
		edit_hwnd = NULL;
		is_opened = false;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static HMENU make_menu(void)
{
	HMENU bar = CreateMenu();
	HMENU file = CreatePopupMenu();
	AppendMenuW(file, MF_STRING, IDM_FILE_SAVE, L"Save log...");
	AppendMenuW(file, MF_STRING, IDM_FILE_CLEAR, L"Clear");
	AppendMenuW(file, MF_SEPARATOR, 0, NULL);
	AppendMenuW(file, MF_STRING, IDM_FILE_CLOSE, L"Close");
	HMENU win = CreatePopupMenu();
	AppendMenuW(win, MF_STRING, IDM_WIN_TOP, L"Always on top");
	AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"File");
	AppendMenuW(bar, MF_POPUP, (UINT_PTR)win, L"Window");
	return bar;
}

static bool create_viewer(void)
{
	if (viewer_hwnd) {
		ShowWindow(viewer_hwnd, SW_SHOW);
		is_opened = true;
		return true;
	}

	if (!msftedit)
		msftedit = LoadLibraryW(L"Msftedit.dll");

	static ATOM atom;
	if (!atom) {
		WNDCLASSEXW wc;
		memset(&wc, 0, sizeof wc);
		wc.cbSize = sizeof wc;
		wc.lpfnWndProc = viewer_wndproc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wc.lpszClassName = L"CrayfishLogViewerWindowClass";
		atom = RegisterClassExW(&wc);
		if (!atom) {
			WARNING("CrayfishLogViewer: RegisterClassEx failed");
			return false;
		}
	}

	int x = 80, y = 80, w = 640, h = 480;
	load_window_pos(&x, &y, &w, &h);

	const char *title = window_title_utf8 && *window_title_utf8 ? window_title_utf8 : "Rance Quest";
	wchar_t *wtitle = utf8_to_wchar(title);

	viewer_hwnd = CreateWindowExW(0, L"CrayfishLogViewerWindowClass",
				      wtitle ? wtitle : L"Rance Quest",
				      WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				      x, y, w, h,
				      NULL, make_menu(), GetModuleHandleW(NULL), NULL);
	free(wtitle);
	if (!viewer_hwnd) {
		WARNING("CrayfishLogViewer: CreateWindowEx failed");
		return false;
	}

	RECT cr;
	GetClientRect(viewer_hwnd, &cr);
	edit_hwnd = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
				    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
				    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
				    0, 0, cr.right, cr.bottom,
				    viewer_hwnd, NULL, GetModuleHandleW(NULL), NULL);
	if (!edit_hwnd) {
		WARNING("CrayfishLogViewer: rich edit create failed");
		DestroyWindow(viewer_hwnd);
		viewer_hwnd = NULL;
		return false;
	}
	SendMessageW(edit_hwnd, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
	richedit_reload();
	is_opened = true;
	return true;
}
#endif /* _WIN32 */

static bool CrayfishLogViewer_OpenViewer(void)
{
#ifdef _WIN32
	if (!create_viewer()) {
		is_opened = false;
		return false;
	}
	return true;
#else
	is_opened = true;
	return true;
#endif
}

static bool CrayfishLogViewer_CloseViewer(void)
{
#ifdef _WIN32
	destroy_viewer();
#else
	is_opened = false;
#endif
	return true;
}

static bool CrayfishLogViewer_IsOpenedViewer(void)
{
#ifdef _WIN32
	return is_opened && viewer_hwnd != NULL;
#else
	return is_opened;
#endif
}

static void CrayfishLogViewer_SetWindowTitleName(struct string *window_title_name)
{
	free(window_title_utf8);
	window_title_utf8 = sjis2utf(window_title_name->text, 0);
#ifdef _WIN32
	if (viewer_hwnd && window_title_utf8) {
		wchar_t *w = utf8_to_wchar(window_title_utf8);
		if (w) {
			SetWindowTextW(viewer_hwnd, w);
			free(w);
		}
	}
#endif
}

static bool CrayfishLogViewer_AddText(struct string *text)
{
	char *utf = sjis2utf(text->text, 0);
	if (!utf)
		return true;
#ifdef _WIN32
	if (!log_utf8)
		ensure_log_cap(1);
	log_append_utf8(utf);
	if (edit_hwnd)
		richedit_append(utf);
#else
	log_message("Crayfish", "%s", utf);
#endif
	free(utf);
	return true;
}

static bool CrayfishLogViewer_ClearText(void)
{
	log_len = 0;
	if (log_utf8)
		log_utf8[0] = '\0';
#ifdef _WIN32
	if (edit_hwnd)
		SetWindowTextW(edit_hwnd, L"");
#endif
	return true;
}

static void CrayfishLogViewer_SetSaveFolderName(struct string *save_folder_name)
{
	free(save_folder_utf8);
	save_folder_utf8 = sjis2utf(save_folder_name->text, 0);
}

void crayfish_log_pump(void)
{
#ifdef _WIN32
	MSG msg;
	if (viewer_hwnd) {
		while (PeekMessageW(&msg, viewer_hwnd, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	if (edit_hwnd) {
		while (PeekMessageW(&msg, edit_hwnd, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
#endif
}

HLL_LIBRARY(CrayfishLogViewer,
	    HLL_EXPORT(OpenViewer, CrayfishLogViewer_OpenViewer),
	    HLL_EXPORT(CloseViewer, CrayfishLogViewer_CloseViewer),
	    HLL_EXPORT(IsOpenedViewer, CrayfishLogViewer_IsOpenedViewer),
	    HLL_EXPORT(SetWindowTitleName, CrayfishLogViewer_SetWindowTitleName),
	    HLL_EXPORT(AddText, CrayfishLogViewer_AddText),
	    HLL_EXPORT(ClearText, CrayfishLogViewer_ClearText),
	    HLL_EXPORT(SetSaveFolderName, CrayfishLogViewer_SetSaveFolderName));
