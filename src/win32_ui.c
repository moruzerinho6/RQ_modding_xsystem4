/* Win32-only extras: title-bar Image filter menu. Isolated so windows.h
 * never collides with xsystem4 Rectangle / ERROR / VK_* macros. */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include "xsystem4.h"

#define RQ_FILTER_MENU_BASE 0xE100

static void SDLCALL win_msg_hook(void *userdata, void *hWnd, unsigned int message,
				 Uint64 wParam, Sint64 lParam)
{
	(void)userdata;
	(void)hWnd;
	(void)lParam;
	if (message != WM_SYSCOMMAND)
		return;
	unsigned cmd = (unsigned)wParam;
	if (cmd >= RQ_FILTER_MENU_BASE && cmd < RQ_FILTER_MENU_BASE + 5)
		rq_notify_scaling_from_menu((int)(cmd - RQ_FILTER_MENU_BASE));
}

void win32_ui_init(SDL_Window *window)
{
	if (!window)
		return;
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (SDL_GetWindowWMInfo(window, &info)) {
		HMENU sys = GetSystemMenu(info.info.win.window, FALSE);
		if (sys) {
			AppendMenuW(sys, MF_SEPARATOR, 0, NULL);
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 0,
				    L"Filter: Standard (bilinear)");
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 1,
				    L"Filter: Bicubic");
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 2,
				    L"Filter: Nearest (pixel)");
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 3,
				    L"Filter: Lanczos");
			AppendMenuW(sys, MF_STRING, RQ_FILTER_MENU_BASE + 4,
				    L"Filter: xBRZ");
		}
	}
	SDL_SetWindowsMessageHook(win_msg_hook, NULL);
}

#else

#include <SDL.h>

void win32_ui_init(SDL_Window *window)
{
	(void)window;
}

#endif
