#include "stdafx.h"

#include "gui.h"
#include "resource.h"

#include "core.h"
#include "calc.h"

#include "gifhandle.h"
#include "gif.h"

#include "resource.h"
#include "var.h"
#include "link.h"
#include "keys.h"

#include "guidebug.h"
#include "guioptions.h"
#include "guicontext.h"
#include "guibuttons.h"
#include "guilcd.h"
#include "guispeed.h"
#include "guivartree.h"
#include "guifaceplate.h"
#include "guiglow.h"
#include "guiopenfile.h"
#include "guicutout.h"
#include "dbmem.h"
#include "dbreg.h"
#include "dbtoolbar.h"
#include "dbtrack.h"
#include "dbdisasm.h"
#include "registry.h"
#include "sendfiles.h"
#include "state.h"
#ifdef USE_COM
#include "wbded.h"
#endif
#include "link.h"
#include "uxtheme.h"
#ifdef USE_GDIPLUS
#include "CGdiPlusBitmap.h"
#endif

#include "DropTarget.h"

#include "expandpane.h"

#ifdef _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#define MENU_FILE 0
#define MENU_EDIT 1
#define MENU_CALC 2
#define MENU_HELP 3

char ExeDir[512];

INT_PTR CALLBACK DlgVarlist(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
HINSTANCE g_hInst;
HACCEL hacceldebug;
POINT drop_pt;
BOOL gif_anim_advance;
BOOL silent_mode = FALSE;

LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ToolProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);


void gui_draw(int slot) {
	gslot = slot;
	InvalidateRect(calcs[slot].hwndLCD, NULL, FALSE);
	UpdateWindow(calcs[slot].hwndLCD);

	//UpdateDebugTrack();
	if (calcs[gslot].gif_disp_state != GDS_IDLE) {
		static int skip = 0;
		if (skip == 0) {
			gif_anim_advance = TRUE;
			InvalidateRect(calcs[gslot].hwndFrame, NULL, FALSE);
		}
		skip = (skip + 1) % 4;
	}
}

VOID CALLBACK TimerProc(HWND hwnd, UINT Message, UINT_PTR idEvent, DWORD dwTimer) {
	static long difference;
	static DWORD prevTimer;

	// How different the timer is from where it should be
	// guard from erroneous timer calls with an upper bound
	// that's the limit of time it will take before the
	// calc gives up and claims it lost time
	difference += ((dwTimer - prevTimer) & 0x003F) - TPF;
	prevTimer = dwTimer;


	int i;
	for (i = 0; i < MAX_CALCS; i++) {
		if (calcs[i].active && calcs[i].send == TRUE) {
			static int frameskip = 0;
			frameskip = (frameskip + 1) % 3;

			if (frameskip == 0) {
				extern HWND hwndSend;
				SendMessage(hwndSend, WM_USER, 0, 0);
				difference = 0;
				return;
			}
		}
	}

	// Are we greater than Ticks Per Frame that would call for
	// a frame skip?
	if (difference > -TPF) {
		calc_run_all();
		while (difference >= TPF) {
			calc_run_all();
			difference -= TPF;
		}

		int i;
		for (i = 0; i < MAX_CALCS; i++) {
			if (calcs[i].active) {
				gui_draw(i);
			}
		}
	// Frame skip if we're too far ahead.
	} else difference += TPF;
}


extern RECT db_rect;

int gui_debug(int slot) {
	pausesound();
	HWND hdebug;
	RECT pos = {CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT+600, CW_USEDEFAULT+400};
	if (db_rect.left != -1) CopyRect(&pos, &db_rect);

	pos.right -= pos.left;
	pos.bottom -= pos.top;

	if ((hdebug = FindWindow(g_szDebugName, "Debugger"))) {
		SwitchToThisWindow(hdebug, TRUE);
		return -1;
	}
	calcs[gslot].running = FALSE;
	hdebug = CreateWindowEx(
		WS_EX_APPWINDOW,
		g_szDebugName,
        "Debugger",
		WS_VISIBLE | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        pos.left, pos.top, pos.right, pos.bottom,
        0, 0, g_hInst, NULL);

	calcs[slot].hwndDebug = hdebug;
	SendMessage(hdebug, WM_SIZE, 0, 0);
	return 0;
}

void SkinCutout(HWND hwnd);

int gui_frame(int slot) {
 	RECT r;

 	if (!calcs[slot].Scale) 		
		calcs[slot].Scale = 2;
	if (calcs[slot].SkinEnabled) {
 		SetRect(&r, 0, 0, calcs[slot].rectSkin.right, calcs[slot].rectSkin.bottom);
 	} else {
 		SetRect(&r, 0, 0, 128*calcs[slot].Scale, 64*calcs[slot].Scale);
 	}
	AdjustWindowRect(&r, WS_CAPTION | WS_TILEDWINDOW, FALSE);
	r.bottom += GetSystemMetrics(SM_CYMENU);

	// Set gslot so the CreateWindow functions operate on the correct calc
	gslot = slot;
	calcs[slot].hwndFrame = CreateWindowEx(
		0, //WS_EX_APPWINDOW,
		g_szAppName,
        "Z80",
		(WS_TILEDWINDOW |  (silent_mode ? 0 : WS_VISIBLE) | WS_CLIPCHILDREN) & ~(WS_MAXIMIZEBOX /* | WS_SIZEBOX */),
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        NULL, 0, g_hInst, NULL);

	HDC hdc = GetDC(calcs[slot].hwndFrame);
	//HBITMAP hbmSkin = LoadBitmap(g_hInst, calcs[slot].model);
	calcs[slot].hdcSkin = CreateCompatibleDC(hdc);
	//SelectObject(calcs[gslot].hdcSkin, hbmSkin);

	/*calcs[slot].hwndLCD = CreateWindowEx(
		0,
		g_szLCDName,
		"LCD",
		WS_VISIBLE |  WS_CHILD,
		0, 0, calcs[slot].cpu.pio.lcd->width*calcs[slot].Scale, 64*calcs[slot].Scale,
		calcs[slot].hwndFrame, (HMENU) 99, g_hInst,  NULL);*/

	if (calcs[slot].hwndFrame == NULL /*|| calcs[slot].hwndLCD == NULL*/) return -1;

	GetClientRect(calcs[slot].hwndFrame, &r);
	calcs[slot].running = TRUE;
	calcs[slot].speed = 100;
	HMENU hmenu = GetMenu(calcs[slot].hwndFrame);
	CheckMenuRadioItem(GetSubMenu(GetSubMenu(hmenu, 2),4), IDM_SPEED_QUARTER, IDM_SPEED_MAX, IDM_SPEED_NORMAL, MF_BYCOMMAND);
	gui_frame_update(slot);
	ReleaseDC(calcs[slot].hwndFrame, hdc);
	return 0;
}

#ifdef USE_GDIPLUS
int gui_frame_update(int slot) {
	HDC hdc = GetDC(calcs[slot].hwndFrame);
	calcs[slot].hdcKeymap = CreateCompatibleDC(hdc);
	calcs[gslot].hdcSkin = CreateCompatibleDC(hdc);
	Graphics skinGraphics(calcs[gslot].hdcSkin);
	Graphics keymapGraphics(calcs[slot].hdcKeymap);
	CGdiPlusBitmapResource hbmSkin, hbmKeymap;
	hbmSkin.Load(CalcModelTxt[calcs[slot].model],_T("PNG"), g_hInst);

	switch(calcs[slot].model)
	{
		case TI_73:
		case TI_83P:
		case TI_83PSE:
			hbmKeymap.Load("TI-83+Keymap", _T("PNG"), g_hInst);
			break;
		case TI_82:
			hbmKeymap.Load("TI-82Keymap", _T("PNG"), g_hInst);
			break;
		case TI_83:
			hbmKeymap.Load("TI-83Keymap", _T("PNG"), g_hInst);
			break;
		case TI_84P:
		case TI_84PSE:
			hbmKeymap.Load("TI-84+SEKeymap", _T("PNG"), g_hInst);
			break;
		default:
			break;
	}

	int skinWidth = hbmSkin.m_pBitmap->GetWidth();
	int skinHeight = hbmSkin.m_pBitmap->GetHeight();
	int keymapWidth = hbmKeymap.m_pBitmap->GetWidth();
	int keymapHeight = hbmKeymap.m_pBitmap->GetHeight();
	int x, y, foundX = 0, foundY = 0;
	bool foundScreen = FALSE;
	if ((skinWidth != keymapWidth) || (skinHeight != keymapHeight)) {
		calcs[slot].SkinEnabled = false;
		MessageBox(NULL, "Skin and Keymap are not the same size", "error",  MB_OK);
	} else {
		calcs[slot].rectSkin.right = skinWidth;
		calcs[slot].rectSkin.bottom = skinHeight;			//find the screen size
		Color pixel;
		for(y = 0; y < skinHeight && !foundScreen; y++) {
			for (x = 0; x < skinWidth && !foundScreen; x++) {
				hbmKeymap.m_pBitmap->GetPixel(x, y, &pixel);
				if (pixel.GetBlue() == 0 && pixel.GetRed() == 255 && pixel.GetGreen() == 0)	{
					//81 92
					foundX = x;
					foundY = y;
					foundScreen = true;
				}
			}
		}
	}
	if (!foundScreen) {
		MessageBox(NULL, "Unable to find the screen box", "error", MB_OK);
		calcs[slot].SkinEnabled = false;
	}
	calcs[slot].rectLCD.left = foundX;
	calcs[slot].rectLCD.top = foundY;
	calcs[slot].rectLCD.right = foundX+192;
	calcs[slot].rectLCD.bottom = foundY+128;
	if (!calcs[slot].hwndFrame)
		return 0;
	HMENU hmenu = GetMenu(calcs[slot].hwndFrame);	
	if (hmenu != NULL) {
		if (!calcs[gslot].SkinEnabled) {
			RECT rc;
			CheckMenuItem(GetSubMenu(hmenu, 2), IDM_CALC_SKIN, MF_BYCOMMAND | MF_UNCHECKED);
			// Create status bar
			if (calcs[slot].hwndStatusBar != NULL) {
				SendMessage(calcs[slot].hwndStatusBar, WM_DESTROY, 0, 0);
				SendMessage(calcs[slot].hwndStatusBar, WM_CLOSE, 0, 0);
			}
			SetRect(&rc, 0, 0, 128*calcs[slot].Scale, 64*calcs[slot].Scale);
			int iStatusWidths[] = {100, -1};
			calcs[slot].hwndStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, calcs[slot].hwndFrame, (HMENU)99, g_hInst, NULL);
			SendMessage(calcs[slot].hwndStatusBar, SB_SETPARTS, 2, (LPARAM) &iStatusWidths);
			SendMessage(calcs[slot].hwndStatusBar, SB_SETTEXT, 1, (LPARAM) CalcModelTxt[calcs[slot].model]);
			RECT src;
			GetWindowRect(calcs[slot].hwndStatusBar, &src);
			AdjustWindowRect(&rc, (WS_TILEDWINDOW | WS_CLIPCHILDREN) & ~WS_MAXIMIZEBOX, FALSE);
			rc.bottom += src.bottom - src.top;
			if (hmenu)
				rc.bottom += GetSystemMetrics(SM_CYMENU);
			SetWindowPos(calcs[slot].hwndFrame, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
			GetClientRect(calcs[slot].hwndFrame, &rc);
			SendMessage(calcs[slot].hwndStatusBar, WM_SIZE, 0, 0);
			SendMessage(calcs[slot].hwndStatusBar, SB_SETTEXT, 1, (LPARAM) CalcModelTxt[calcs[slot].model]);
			//InvalidateRect(calcs[slot].hwndFrame, NULL, FALSE);
		} else {
			CheckMenuItem(GetSubMenu(hmenu, 2), IDM_CALC_SKIN, MF_BYCOMMAND | MF_CHECKED);
			SendMessage(calcs[slot].hwndStatusBar, WM_DESTROY, 0, 0);
			SendMessage(calcs[slot].hwndStatusBar, WM_CLOSE, 0, 0);
			calcs[gslot].hwndStatusBar = NULL;
			//SetRect(&calcs[slot].rectSkin, 0, 0, 350, 725);
			RECT rc;
			CopyRect(&rc, &calcs[slot].rectSkin);
			AdjustWindowRect(&rc, WS_CAPTION | WS_TILEDWINDOW , FALSE);
			rc.bottom += GetSystemMetrics(SM_CYMENU);
			SetWindowPos(calcs[slot].hwndFrame, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_DRAWFRAME);
			//InvalidateRect(hwnd, NULL, TRUE);
		}
	}
	skinGraphics.DrawImage(hbmSkin, 0, 0);

	if (calcs[slot].model == TI_84PSE) {
		if (DrawFaceplateRegion(calcs[gslot].hdcSkin))
			MessageBox(NULL, "Unable to draw faceplate", "error", MB_OK);
	}
	HDC hdcOverlay = CreateCompatibleDC(calcs[gslot].hdcSkin);
	Graphics newSkinGraphics(hdcOverlay);
	newSkinGraphics.DrawImage(hbmSkin, 0, 0);
	BLENDFUNCTION bf;
	bf.BlendOp = AC_SRC_OVER;
	bf.BlendFlags = 0;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA;
	AlphaBlend(calcs[gslot].hdcSkin, 0, 0, calcs[gslot].rectSkin.right, calcs[gslot].rectSkin.bottom, hdcOverlay,
		calcs[gslot].rectSkin.left, calcs[gslot].rectSkin.top, calcs[gslot].rectSkin.right, calcs[gslot].rectSkin.bottom, bf);
		RECT rc;
	GetClientRect(calcs[slot].hwndFrame, &rc);
	if (!calcs[slot].SkinEnabled) {			//HACK: probably not the best solution for this :|
		FillRect(hdc, &rc, GetStockBrush(GRAY_BRUSH));
	}
	HBITMAP skin;
	if (calcs[slot].bCutout && calcs[slot].SkinEnabled)	{
		hbmSkin.m_pBitmap->GetHBITMAP(Color::AlphaMask, &skin);
		if (EnableCutout(calcs[slot].hwndFrame, skin) != 0) {
			MessageBox(NULL, "Couldn't cutout window", "error",  MB_OK);
		}
	} else if (calcs[gslot].bCutout && !calcs[slot].SkinEnabled) {
		DisableCutout(calcs[slot].hwndFrame);
		calcs[gslot].bCutout = TRUE;
	} else {
		DisableCutout(calcs[slot].hwndFrame);
	}
	if (calcs[slot].hwndStatusBar != NULL)
		SendMessage(calcs[slot].hwndStatusBar, SB_SETTEXT, 1, (LPARAM) CalcModelTxt[calcs[slot].model]);

	ReleaseDC(calcs[slot].hwndFrame, hdc);
	DeleteObject(skin);
	SendMessage(calcs[slot].hwndFrame, WM_SIZE, 0, 0);
	return 0;
}
#else
int gui_frame_update(int slot) {
	BITMAP skinSize, keymapSize;
	HDC hdc = GetDC(calcs[slot].hwndFrame);
	calcs[slot].hdcKeymap = CreateCompatibleDC(hdc);
	calcs[gslot].hdcSkin = CreateCompatibleDC(hdc);
	HBITMAP hbmSkin = LoadBitmap(g_hInst, CalcModelTxt[calcs[slot].model]);
	HBITMAP hbmOld = (HBITMAP)SelectObject(calcs[gslot].hdcSkin, hbmSkin);
	GetObject(hbmSkin, sizeof(BITMAP), &skinSize);
	char *name = (char *) malloc(strlen(CalcModelTxt[calcs[slot].model]) + 7);
	strcpy(name, CalcModelTxt[calcs[slot].model]);
	strcat(name, "Keymap");
	HBITMAP hbmKeymap = LoadBitmap(g_hInst, name);
	HBITMAP hbmOldKeymap = (HBITMAP) SelectObject(calcs[slot].hdcKeymap, hbmKeymap);
	GetObject(hbmKeymap, sizeof(BITMAP), &keymapSize);	//skin and keymap must match
	free(name);
	int x, y, foundX = 0, foundY = 0;
	bool foundScreen = FALSE;
	if ((skinSize.bmWidth != keymapSize.bmWidth) || (skinSize.bmHeight != keymapSize.bmHeight)) {
		calcs[slot].SkinEnabled = false;
		MessageBox(NULL, "Skin and Keymap are not the same size", "error",  MB_OK);
	} else {
		calcs[slot].rectSkin.right = skinSize.bmWidth;
		calcs[slot].rectSkin.bottom = skinSize.bmHeight;		//find the screen size
		for(y = 0; y < skinSize.bmHeight && foundScreen == false; y++) {
			for (x = 0; x < skinSize.bmWidth && foundScreen == false; x++) {
				COLORREF pixel = GetPixel(calcs[slot].hdcKeymap, x, y);
				if (pixel == RGB(255, 0, 0) && foundScreen != true)	{
					foundX = x;
					foundY = y;	
					foundScreen = true;	
				}
			}
		}
	}
	calcs[slot].rectLCD.left = foundX;
	calcs[slot].rectLCD.top = foundY;
	calcs[slot].rectLCD.right = foundX+192;
	calcs[slot].rectLCD.bottom = foundY+128;
	if (!calcs[slot].hwndFrame)
		return 0;
	HMENU hmenu = GetMenu(calcs[slot].hwndFrame);	
	if (hmenu != NULL) {
		if (!calcs[gslot].SkinEnabled) {
			RECT rc;
			CheckMenuItem(GetSubMenu(hmenu, 2), IDM_CALC_SKIN, MF_BYCOMMAND | MF_UNCHECKED);
			// Create status bar
			if (calcs[slot].hwndStatusBar != NULL) {
				SendMessage(calcs[slot].hwndStatusBar, WM_DESTROY, 0, 0);
				SendMessage(calcs[slot].hwndStatusBar, WM_CLOSE, 0, 0);
			}
			SetRect(&rc, 0, 0, 128*calcs[slot].Scale, 64*calcs[slot].Scale);
			int iStatusWidths[] = {100, -1};
			calcs[slot].hwndStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, calcs[slot].hwndFrame, (HMENU)99, g_hInst, NULL);
			SendMessage(calcs[slot].hwndStatusBar, SB_SETPARTS, 2, (LPARAM) &iStatusWidths);
			SendMessage(calcs[slot].hwndStatusBar, SB_SETTEXT, 1, (LPARAM) CalcModelTxt[calcs[slot].model]);
			RECT src;
			GetWindowRect(calcs[slot].hwndStatusBar, &src);
			AdjustWindowRect(&rc, (WS_TILEDWINDOW | WS_CLIPCHILDREN) & ~WS_MAXIMIZEBOX, FALSE);
			rc.bottom += src.bottom - src.top;
			if (hmenu)
				rc.bottom += GetSystemMetrics(SM_CYMENU);
			SetWindowPos(calcs[slot].hwndFrame, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
			GetClientRect(calcs[slot].hwndFrame, &rc);
			SendMessage(calcs[slot].hwndStatusBar, WM_SIZE, 0, 0);
			SendMessage(calcs[slot].hwndStatusBar, SB_SETTEXT, 1, (LPARAM) CalcModelTxt[calcs[slot].model]);
			//InvalidateRect(calcs[slot].hwndFrame, NULL, FALSE);
		} else {
			CheckMenuItem(GetSubMenu(hmenu, 2), IDM_CALC_SKIN, MF_BYCOMMAND | MF_CHECKED);
			SendMessage(calcs[slot].hwndStatusBar, WM_DESTROY, 0, 0);
			SendMessage(calcs[slot].hwndStatusBar, WM_CLOSE, 0, 0);
			calcs[gslot].hwndStatusBar = NULL;
			//SetRect(&calcs[slot].rectSkin, 0, 0, 350, 725);
			RECT rc;
			CopyRect(&rc, &calcs[slot].rectSkin);
			AdjustWindowRect(&rc, WS_CAPTION | WS_TILEDWINDOW , FALSE);
			rc.bottom += GetSystemMetrics(SM_CYMENU);
			SetWindowPos(calcs[slot].hwndFrame, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_DRAWFRAME);
			//InvalidateRect(hwnd, NULL, TRUE);
		}
	}
	RECT rc;
	GetClientRect(calcs[slot].hwndFrame, &rc);
	if (!calcs[slot].SkinEnabled || !calcs[slot].bCutout)
		FillRect(calcs[slot].hdcSkin, &rc, GetStockBrush(GRAY_BRUSH));
	if (calcs[slot].model == 9) {		//TI84+SE
		if (DrawFaceplateRegion(calcs[gslot].hdcSkin))
			MessageBox(NULL, "Unable to draw faceplate", "error", MB_OK);
	}
	HDC hdcOverlay = CreateCompatibleDC(calcs[gslot].hdcSkin);
	HBITMAP bmpGray = LoadBitmap(g_hInst, CalcModelTxt[calcs[gslot].model]);
	SelectObject(hdcOverlay, bmpGray);
	BLENDFUNCTION bf;
	bf.BlendOp = AC_SRC_OVER;
	bf.BlendFlags = 0;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA;
	AlphaBlend(calcs[gslot].hdcSkin, 0, 0, calcs[gslot].rectSkin.right, calcs[gslot].rectSkin.bottom, hdcOverlay,
		calcs[gslot].rectSkin.left, calcs[gslot].rectSkin.top, calcs[gslot].rectSkin.right, calcs[gslot].rectSkin.bottom, bf);
	//BitBlt(calcs[gslot].hdcSkin, 0, 0, skinSize.bmWidth, skinSize.bmHeight, hdcOverlay, 0, 0, SRCCOPY);
	if (!calcs[slot].SkinEnabled) {			//HACK: probably not the best solution for this :|
		FillRect(hdc, &rc, GetStockBrush(GRAY_BRUSH));
	}
	if (calcs[slot].bCutout && calcs[slot].SkinEnabled)	{
		if (EnableCutout(calcs[slot].hwndFrame, hbmSkin) != 0) {
			MessageBox(NULL, "Couldn't cutout window", "error",  MB_OK);
		}
	} else if (calcs[gslot].bCutout && !calcs[slot].SkinEnabled) {
		DisableCutout(calcs[slot].hwndFrame);
		calcs[gslot].bCutout = TRUE;
	} else {
		DisableCutout(calcs[slot].hwndFrame);
	}
	if (calcs[slot].hwndStatusBar != NULL)
		SendMessage(calcs[slot].hwndStatusBar, SB_SETTEXT, 1, (LPARAM) CalcModelTxt[calcs[slot].model]);

	ReleaseDC(calcs[slot].hwndFrame, hdc);
	DeleteObject(hbmOld);
	DeleteObject(hbmOldKeymap);
	SendMessage(calcs[slot].hwndFrame, WM_SIZE, 0, 0);
	return 0;
}
#endif

char* LoadRomIntialDialog(void) {
	OPENFILENAME ofn;
	char lpstrFilter[] 	= "\
Known types ( *.sav; *.rom) \0*.sav;*.rom\0\
Save States  (*.sav)\0*.sav\0\
ROMs  (*.rom)\0*.rom\0\
All Files (*.*)\0*.*\0\0";
	char* FileName = (char *) malloc(MAX_PATH);
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(FileName, MAX_PATH);
	ofn.lStructSize		= sizeof(OPENFILENAME);
	ofn.lpstrFilter		= (LPCTSTR) lpstrFilter;
	ofn.lpstrFile		= FileName;
	ofn.nMaxFile		= MAX_PATH;
	ofn.lpstrTitle		= "Wabbitemu: Please select a ROM or save state";
	ofn.Flags			= OFN_PATHMUSTEXIST | OFN_EXPLORER |
						  OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;
	if (!GetOpenFileName(&ofn)) {
		free(FileName);
		return NULL;
	}
	return FileName;
}

extern HWND hwndProp;
extern RECT PropRect;
extern int PropPageLast;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
   LPSTR lpszCmdParam, int nCmdShow) {

	MSG Msg;
	WNDCLASSEX wc;
	LPWSTR *argv;
	int argc;
	char tmpstring[512];
	BOOL loadfiles = FALSE;
	int length,i;

	length = GetModuleFileName(NULL,ExeDir,512);
	//LCD_X = 63+16+1;	//LCD_Y = 74+16+2;
	if (length) {
		for(i=length;i>0 && (ExeDir[i]!='\\') ;i--);
		if (i) ExeDir[i+1] = 0;
	}

	argv = CommandLineToArgvW(GetCommandLineW(),&argc);

	if (argv && argc>1) {
		wcstombs(tmpstring,argv[1],512);
		if ( (tmpstring[0]=='-') && (tmpstring[1]=='n') ) loadfiles = TRUE;
		else {
			HWND Findhwnd = FindWindow(g_szAppName, NULL);
			if (Findhwnd == NULL) {
				loadfiles = TRUE;
			} else {
				HWND FindChildhwnd = FindWindowEx(Findhwnd,NULL,g_szLCDName,NULL);
				if (FindChildhwnd == NULL) {
					loadfiles = TRUE;
				} else {
					COPYDATASTRUCT cds;
					char* FileNames = NULL;
					for(i=1;i<argc;i++) {
						memset(tmpstring,0,512);
						wcstombs(tmpstring,argv[i],512);
						if (tmpstring[0]!='-') {
							//printf("%s \n",tmpstring);
							FileNames = AppendName(FileNames,tmpstring);
						} else {
							if (toupper(tmpstring[1])=='F')
								SwitchToThisWindow(FindChildhwnd, TRUE);
						}
					}
					i = SizeofFileList(FileNames);
					if (i>0) {
						cds.dwData = SEND_CUR;
						cds.cbData = i;
						cds.lpData = FileNames;
						SendMessage(FindChildhwnd, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds);
						free(FileNames);
					}
					exit(0);
				}
			}
		}
	}

    g_hInst = hInstance;

	wc.cbSize = sizeof(wc);
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(g_hInst, "W");
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName = MAKEINTRESOURCE(IDR_MAIN_MENU);
	wc.lpszClassName = g_szAppName;
	wc.hIconSm = LoadIcon(g_hInst, "W");

	RegisterClassEx(&wc);

	// LCD
	wc.lpszClassName = g_szLCDName;
	wc.lpfnWndProc = LCDProc;
	wc.lpszMenuName = NULL;
	RegisterClassEx(&wc);

	// Toolbar
	wc.lpszClassName = g_szToolbar;
	wc.lpfnWndProc = ToolBarProc;
	wc.lpszMenuName = NULL;
	wc.style = 0;
	RegisterClassEx(&wc);

/*	// subbar
	wc.lpszClassName = g_szSubbar;
	wc.lpfnWndProc = SubBarProc;
	RegisterClassEx(&wc);*/

	wc.lpfnWndProc = DebugProc;
	wc.style = CS_DBLCLKS;
	wc.lpszClassName = g_szDebugName;
	wc.lpszMenuName = MAKEINTRESOURCE(IDR_DEBUG_MENU); //MAKEINTRESOURCE(IDR_DB_MENU);
	wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE+1);
	RegisterClassEx(&wc);

	wc.lpszMenuName = NULL;
	wc.style = 0;
	wc.lpfnWndProc = DisasmProc;
	wc.lpszClassName = g_szDisasmName;
	wc.hbrBackground = (HBRUSH) NULL;
	RegisterClassEx(&wc);

	// Registers
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = RegProc;
	wc.lpszClassName = g_szRegName;
	wc.hbrBackground = NULL;
	RegisterClassEx(&wc);

	wc.style = 0;
	wc.lpfnWndProc = ExpandPaneProc;
	wc.lpszClassName = g_szExpandPane;
	RegisterClassEx(&wc);

	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = MemProc;
	wc.lpszClassName = g_szMemName;
	wc.hbrBackground = NULL;
	RegisterClassEx(&wc);

	// initialize com events
	OleInitialize(NULL);

#ifdef USE_GDIPLUS
	// Initialize GDI+.
	GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
#endif


	if (argv && argc>1) {
		for (i=1;i<argc;i++) {
			memset(tmpstring,0,512);
			wcstombs(tmpstring,argv[i],512);
			if (tmpstring[0] == '/' || tmpstring[0] == '-') {
				if (toupper(tmpstring[1]) == 'S') {
					silent_mode = TRUE;
				}
			}
		}
	}

	int slot = calc_slot_new();
	LoadRegistrySettings();

	slot = rom_load(slot, calcs[gslot].rom_path);
	if (slot != -1) gui_frame(slot);
	else {
		char* string = LoadRomIntialDialog();
		if (string) {
			slot = calc_slot_new();
			slot = rom_load(slot, string);
			if (slot != -1) gui_frame(slot);
			else return EXIT_FAILURE;
		} else return EXIT_FAILURE;
	}

	strcpy(calcs[slot].labelfn, "labels.lab");

	state_build_applist(&calcs[gslot].cpu, &calcs[gslot].applist);
	VoidLabels(slot);

	if (loadfiles) {
		if (argv && argc>1) {
			char* FileNames = NULL;
			for(i=1;i<argc;i++) {
				memset(tmpstring,0,512);
				wcstombs(tmpstring,argv[i],512);
				if (tmpstring[0]!='-') {
					printf("%s\n",tmpstring);
					FileNames = AppendName(FileNames,tmpstring);
				}
			}
			ThreadSend(FileNames, SEND_CUR);
		}
	}
	loadfiles = FALSE;


#ifdef USE_DIRECTX
	IDirect3D9 *pD3D = Direct3DCreate9(D3D_SDK_VERSION);

	D3DPRESENT_PARAMETERS d3dpp;
	ZeroMemory( &d3dpp, sizeof(d3dpp) );
	d3dpp.Windowed = TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.BackBufferCount = 1;

	if (pD3D->CreateDevice(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			calcs[gslot].hwndLCD,
			D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE ,
			&d3dpp,
			&pd3dDevice) != D3D_OK) {
		printf("failed to create device\n");
		exit(1);
	}
#endif

	// Set the one global timer for all calcs
	SetTimer(NULL, 0, TPF, TimerProc);

	hacceldebug = LoadAccelerators(g_hInst, "DisasmAccel");
	haccelmain = LoadAccelerators(g_hInst, "Z80Accel");

    while (GetMessage(&Msg, NULL, 0, 0)) {
		HACCEL haccel = hacceldebug;
		HWND hwndtop = GetForegroundWindow();
		if (hwndtop) {
			if (hwndtop == FindWindow(g_szDebugName, NULL) ) {
				haccel = hacceldebug;
			} else if (hwndtop == FindWindow(g_szAppName, NULL) ) {
				haccel = haccelmain;
				hwndtop = FindWindowEx(hwndtop,NULL,g_szLCDName,NULL);
				SetForegroundWindow(hwndtop);
			}
		}

		if (hwndProp != NULL) {
			if (PropSheet_GetCurrentPageHwnd(hwndProp) == NULL) {
				GetWindowRect(hwndProp, &PropRect);
				DestroyWindow(hwndProp);
				hwndProp = NULL;
			}
		}

		if (hwndProp == NULL || PropSheet_IsDialogMessage(hwndProp, &Msg) == FALSE) {
			if (!TranslateAccelerator(hwndtop, haccel, &Msg)) {
				TranslateMessage(&Msg);
				int slot = calc_from_hwnd(Msg.hwnd);
				if (slot != -1) gslot = slot;
	        	DispatchMessage(&Msg);
			}
		} else {
			// Get the current tab
			HWND hwndPropTabCtrl = PropSheet_GetTabControl(hwndProp);
			PropPageLast = TabCtrl_GetCurSel(hwndPropTabCtrl);
		}
    }

    // Make sure the GIF has terminated
	if (gif_write_state == GIF_FRAME) {
		gif_write_state = GIF_END;
		handle_screenshot();
	}
	
#ifdef USE_GDIPLUS
	// Shutdown GDI+
	GdiplusShutdown(gdiplusToken);
#endif

	// Shutdown COM
	OleUninitialize();
#if _DEBUG
	_CrtDumpMemoryLeaks();
#endif

    return Msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam) {
	//static HDC hdcKeymap;
	static POINT ctxtPt;

	switch (Message) {
		case WM_CREATE:
		{
			/*if (calcs[gslot].bCutout && calcs[gslot].SkinEnabled) {
				if (EnableCutout(hwnd) != 0) {
					MessageBox(NULL, "Couldn't cutout window", "error",  MB_OK);
				}
			}*/
			IDropTarget *pDropTarget = NULL;
			RegisterDropWindow(hwnd, &pDropTarget);

			// Force the current skin setting to be enacted
			calcs[gslot].SkinEnabled = !calcs[gslot].SkinEnabled;
			SendMessage(hwnd, WM_COMMAND, IDM_CALC_SKIN, 0);

			SetWindowText(hwnd, "Wabbitemu");
			/*HDC hdc = GetDC(hwnd);			
			// Only one keymap is needed for all calcs			
			// NOT TRUE WITH MULTIPLE SKINS, and we all want to use buckeyedude's sexy			
			// new skins right? right...			
			// Load it just for the first to conserve RAM			
			if (calc_count() == 1) {				
			HBITMAP hbmKeymap = LoadBitmap(g_hInst, "Keymap");				
			calcs[gslot].hdcKeymap = CreateCompatibleDC(hdc);				
			SelectObject(calcs[gslot].hdcKeymap, hbmKeymap);			
			}			
			ReleaseDC(hwnd, hdc);*/			
			return 0;
		}
		case WM_ACTIVATE: {
			//RECT rc;
			//GetClientRect(hwnd, &rc);
			//InvalidateRect(hwnd, &rc, FALSE);
			if (wParam == WA_INACTIVE)
				return 0;
			
			int temp = calc_from_hwnd(hwnd);
			if (temp != -1)
				gslot = temp;
			return 0;
		}
		case WM_PAINT:
		{
#define GIFGRAD_PEAK 15
#define GIFGRAD_TROUGH 10

			static int GIFGRADWIDTH = 1;
			static int GIFADD = 1;

			if (gif_anim_advance) {
				switch (calcs[gslot].gif_disp_state) {
					case GDS_STARTING:
						if (GIFGRADWIDTH > 15) {
							calcs[gslot].gif_disp_state = GDS_RECORDING;
							GIFADD = -1;
						} else {
							GIFGRADWIDTH ++;
						}
						break;
					case GDS_RECORDING:
						GIFGRADWIDTH += GIFADD;
						if (GIFGRADWIDTH > GIFGRAD_PEAK) GIFADD = -1;
						else if (GIFGRADWIDTH < GIFGRAD_TROUGH) GIFADD = 1;
						break;
					case GDS_ENDING:
						if (GIFGRADWIDTH) GIFGRADWIDTH--;
						else {
							calcs[gslot].gif_disp_state = GDS_IDLE;							
							gui_frame_update(gslot);						
						}						
						break;
					case GDS_IDLE:
						break;
				}
				gif_anim_advance = FALSE;
			}

			if (calcs[gslot].gif_disp_state != GDS_IDLE) {
				RECT screen, rc;
				//screen = calcs[gslot].rectLCD;
				GetWindowRect(calcs[gslot].hwndLCD, &screen);
				GetWindowRect(calcs[gslot].hwndFrame, &rc);
				//OffsetRect(&screen, rc.left, rc.top);
				int orig_w = screen.right - screen.left;
				int orig_h = screen.bottom - screen.top;

				AdjustWindowRect(&screen, WS_CAPTION, FALSE);
				screen.top -= GetSystemMetrics(SM_CYMENU);

				//printf("screen: %d\n", screen.left - rc.left);

				SetRect(&screen, screen.left - rc.left -5, screen.top - rc.top - 5,
						screen.left - rc.left + orig_w - 5,
						screen.top - rc.top + orig_h - 5);

				int grayred = (((double) GIFGRADWIDTH / GIFGRAD_PEAK) * 50);
				HDC hWindow = GetDC(hwnd);
				DrawGlow(hWindow, &screen, RGB(127-grayred, 127-grayred, 127+grayred), GIFGRADWIDTH);				
				ReleaseDC(hwnd, hWindow);
				InflateRect(&screen, GIFGRADWIDTH, GIFGRADWIDTH);
				ValidateRect(hwnd, &screen);
			}

			PAINTSTRUCT ps;
			HDC hdc;
			hdc = BeginPaint(hwnd, &ps);
			if (calcs[gslot].SkinEnabled)
				BitBlt(hdc, 0, 0, calcs[gslot].rectSkin.right, calcs[gslot].rectSkin.bottom,							
				calcs[gslot].hdcSkin, 0, 0, SRCCOPY);
			else {
				RECT rc;
				GetClientRect(calcs[gslot].hwndFrame, &rc);
				FillRect(hdc, &rc, GetStockBrush(GRAY_BRUSH));
			}
			ReleaseDC(hwnd, hdc);
			EndPaint(hwnd, &ps);

			return 0;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam)) {
				case IDM_FILE_NEW:
				{
					int slot = calc_slot_new();
					rom_load(slot, calcs[gslot].rom_path);
					gui_frame(slot);
					break;
				}
				case IDM_FILE_EXIT:
					if (calc_count() > 1) {
						char buf[256];
						sprintf(buf, "If you exit now, %d other running calculator(s) will be closed.  Are you sure you want to exit?", calc_count()-1);
						int res = MessageBoxA(NULL, buf, "Wabbitemu", MB_YESNO);
						if (res == IDCANCEL || res == IDNO)
							break;
						PostQuitMessage(0);
					}
					SendMessage(hwnd, WM_CLOSE, 0, 0);
					break;
				case IDM_FILE_GIF:
				{
					HMENU hmenu = GetMenu(hwnd);
					if (gif_write_state == GIF_IDLE) {
						gif_write_state = GIF_START;
						calcs[gslot].gif_disp_state = GDS_STARTING;
						CheckMenuItem(GetSubMenu(hmenu, MENU_FILE), IDM_FILE_GIF, MF_BYCOMMAND | MF_CHECKED);
					} else {
						gif_write_state = GIF_END;
						calcs[gslot].gif_disp_state = GDS_ENDING;
						CheckMenuItem(GetSubMenu(hmenu, MENU_FILE), IDM_FILE_GIF, MF_BYCOMMAND | MF_UNCHECKED);
					}
					break;
				}
				case IDM_FILE_CLOSE:
					return DestroyWindow(hwnd);
				case IDM_CALC_VARIABLES:
					CreateVarTreeList();
					break;
				case IDM_CALC_SKIN:
				{
					calcs[gslot].SkinEnabled = !calcs[gslot].SkinEnabled;
					gui_frame_update(gslot);
					break;
				}
				case IDM_CALC_SOUND:
				{
					HMENU hmenu = GetMenu(hwnd);
					togglesound();
					CheckMenuItem(GetSubMenu(hmenu, 2), IDM_CALC_SOUND, MF_BYCOMMAND | (calcs[gslot].audio->enabled ? MF_CHECKED : MF_UNCHECKED));
					break;
				}
				case IDM_CALC_OPTIONS:
					DoPropertySheet(NULL);
					break;
				case IDM_FILE_OPEN:
					GetOpenSendFileName(hwnd, 0);
					gui_frame_update(gslot);
					break;
				case IDM_FILE_SAVE:
					SaveStateDialog(hwnd);
					break;
				case IDM_EDIT_COPY: {
					HLOCAL ans;
					ans = (HLOCAL) GetRealAns(&calcs[gslot].cpu);
					OpenClipboard(hwnd);
					EmptyClipboard();
					SetClipboardData(CF_TEXT, ans);

					CloseClipboard();
					break;
				}
				case IDM_EDIT_PASTE: {

				}
				case IDM_DEBUG_RESET: {
					calc_reset(gslot);
					calc_run_timed(gslot, 200);
					calcs[gslot].cpu.pio.keypad->on_pressed |= KEY_FALSEPRESS;
					calc_run_timed(gslot, 300);
					calcs[gslot].cpu.pio.keypad->on_pressed &= ~KEY_FALSEPRESS;
					break;
				}
				case IDM_DEBUG_OPEN:
					gui_debug(gslot);
					break;
				case IDM_HELP_ABOUT:
					calcs[gslot].running = FALSE;
					DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DLGABOUT), hwnd, (DLGPROC)AboutDialogProc);					
					calcs[gslot].running = TRUE;					
					break;				
				case IDM_HELP_WEBSITE:					
					ShellExecute(NULL, "open", g_szWebPage,
					    NULL, NULL, SW_SHOWNORMAL);
					break;
				case IDM_FRAME_BTOGGLE:
					SendMessage(hwnd, WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(ctxtPt.x, ctxtPt.y));
					break;
				case IDM_FRAME_BUNLOCK: {
					RECT rc;
					keypad_t *kp = (keypad_t *) (&calcs[gslot].cpu)->pio.devices[1].aux;
					int group,bit;
					GetClientRect(hwnd, &rc);
					for(group=0;group<7;group++) {
						for(bit=0;bit<8;bit++) {
							kp->keys[group][bit] &=(~KEY_LOCKPRESS);
						}
					}
					calcs[gslot].cpu.pio.keypad->on_pressed &=(~KEY_LOCKPRESS);

					HBITMAP hbmSkin = LoadBitmap(g_hInst, "Skin");
					HBITMAP oldSkin = (HBITMAP) SelectObject(calcs[gslot].hdcSkin, hbmSkin);
					SendMessage(hwnd, WM_SIZE, 0, 0);
					DeleteObject(oldSkin);
					break;
				}
				case IDM_CALC_CONNECT: {					
					if (link_connect(&calcs[0].cpu, &calcs[1].cpu))						
						MessageBox(NULL, "Connection Failed", "Error", MB_OK);					
					else						
						MessageBox(NULL, "Connection Successful", "Success", MB_OK);					
					break;
				}
				case IDM_CALC_PAUSE: {					
					HMENU hmenu = GetMenu(hwnd);					
					if (calcs[gslot].running) {						
						CheckMenuItem(GetSubMenu(hmenu, 2), IDM_CALC_PAUSE, MF_BYCOMMAND | MF_CHECKED);
						calcs[gslot].running = FALSE;					
					} else {						
						CheckMenuItem(GetSubMenu(hmenu, 2), IDM_CALC_PAUSE, MF_BYCOMMAND | MF_UNCHECKED);
						calcs[gslot].running = TRUE;					
					}					
					break;
				}				
				case IDM_SPEED_QUARTER: {					
					calcs[gslot].speed = 25;										
					CheckMenuRadioItem(GetSubMenu(GetMenu(hwnd), 2), IDM_SPEED_QUARTER, IDM_SPEED_SET, IDM_SPEED_QUARTER, MF_BYCOMMAND| MF_CHECKED);
					break;				
				}
				case IDM_SPEED_HALF: {					
					calcs[gslot].speed = 50;
					HMENU hmenu = GetMenu(hwnd);
					CheckMenuRadioItem(GetSubMenu(hmenu, 2), IDM_SPEED_QUARTER, IDM_SPEED_SET, IDM_SPEED_HALF, MF_BYCOMMAND | MF_CHECKED);
					break;				
				}
				case IDM_SPEED_NORMAL: {					
					calcs[gslot].speed = 100;
					HMENU hmenu = GetMenu(hwnd);
					CheckMenuRadioItem(GetSubMenu(hmenu, 2), IDM_SPEED_QUARTER, IDM_SPEED_SET, IDM_SPEED_NORMAL, MF_BYCOMMAND | MF_CHECKED);
					break;				
				}
				case IDM_SPEED_DOUBLE: {					
					calcs[gslot].speed = 200;
					HMENU hmenu = GetMenu(hwnd);
					CheckMenuRadioItem(GetSubMenu(hmenu, 2), IDM_SPEED_QUARTER, IDM_SPEED_SET, IDM_SPEED_DOUBLE, MF_BYCOMMAND | MF_CHECKED);
					break;
				}
				case IDM_SPEED_QUADRUPLE: {					
					calcs[gslot].speed = 400;
					HMENU hmenu = GetMenu(hwnd);
					CheckMenuRadioItem(GetSubMenu(hmenu, 2), IDM_SPEED_QUARTER, IDM_SPEED_SET, IDM_SPEED_QUADRUPLE, MF_BYCOMMAND | MF_CHECKED);
					break;
				}
				case IDM_SPEED_MAX: {
					calcs[gslot].speed = 4000;
					HMENU hmenu = GetMenu(hwnd);
					CheckMenuRadioItem(GetSubMenu(hmenu, 2), IDM_SPEED_QUARTER, IDM_SPEED_SET, IDM_SPEED_MAX, MF_BYCOMMAND | MF_CHECKED);
					break;				
				}
				case IDM_SPEED_SET: {
					HMENU hmenu = GetMenu(hwnd);
					CheckMenuRadioItem(GetSubMenu(hmenu, 2), IDM_SPEED_QUARTER, IDM_SPEED_SET, IDM_SPEED_SET, MF_BYCOMMAND | MF_CHECKED);
					DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DLGSPEED), hwnd, (DLGPROC)SetSpeedProc);
					SetFocus(hwnd);
					break;
				}
			}			
			switch (HIWORD(wParam)) {

			}
			return 0;
		}
		//case WM_MOUSEMOVE:
		case WM_LBUTTONUP:
		case WM_LBUTTONDOWN:
		{
			int group,bit;
			static POINT pt;
			keypad_t *kp = calcs[gslot].cpu.pio.keypad;

			if (Message == WM_LBUTTONDOWN) {
				SetCapture(hwnd);
				pt.x	= GET_X_LPARAM(lParam);
				pt.y	= GET_Y_LPARAM(lParam);
				if (calcs[gslot].bCutout) {
					pt.y += GetSystemMetrics(SM_CYCAPTION);	
					pt.x += GetSystemMetrics(SM_CXSIZEFRAME);
				}
			} else {
				ReleaseCapture();
			}

			for(group=0;group<7;group++) {
				for(bit=0;bit<8;bit++) {
					kp->keys[group][bit] &=(~KEY_MOUSEPRESS);
				}
			}

			calcs[gslot].cpu.pio.keypad->on_pressed &= ~KEY_MOUSEPRESS;

			if (wParam != MK_LBUTTON) goto finalize_buttons;

			COLORREF c = GetPixel(calcs[gslot].hdcKeymap, pt.x, pt.y);
			if (GetRValue(c) == 0xFF) goto finalize_buttons;

			if ( (GetGValue(c)>>4)==0x05 && (GetBValue(c)>>4)==0x00){
				calcs[gslot].cpu.pio.keypad->on_pressed |= KEY_MOUSEPRESS;
			} else {
				kp->keys[GetGValue(c) >> 4][GetBValue(c) >> 4] |= KEY_MOUSEPRESS;
				if ((kp->keys[GetGValue(c) >> 4][GetBValue(c) >> 4] & KEY_STATEDOWN) == 0) {
					//DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &pt, DBS_DOWN | DBS_PRESS);
					kp->keys[GetGValue(c) >> 4][GetBValue(c) >> 4] |= KEY_STATEDOWN;
					//SendMessage(hwnd, WM_SIZE, 0, 0);
				}
			}

		//extern POINT ButtonCenter83[64];
		//extern POINT ButtonCenter84[64];
finalize_buttons:
			kp = calcs[gslot].cpu.pio.keypad;
			for(group=0;group<7;group++) {
				for(bit=0;bit<8;bit++) {
					if ((kp->keys[group][bit] & KEY_STATEDOWN) &&
						((kp->keys[group][bit] & KEY_MOUSEPRESS) == 0) &&
						((kp->keys[group][bit] & KEY_KEYBOARDPRESS) == 0)) {
						/*if (calcs[gslot].model == TI_84P || calcs[gslot].model == TI_84PSE) {
							DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &ButtonCenter84[bit+(group<<3)], DBS_UP | DBS_PRESS);
							} else {
							DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &ButtonCenter83[bit+(group<<3)], DBS_UP | DBS_PRESS);
						}*/	
							kp->keys[group][bit] &= (~KEY_STATEDOWN);
							//SendMessage(hwnd, WM_SIZE, 0, 0);
					}
				}
			}
			return 0;
		}
		case WM_MBUTTONDOWN:
		{
			int group,bit;
			POINT pt;
			keypad_t *kp = (keypad_t *) (&calcs[gslot].cpu)->pio.devices[1].aux;

			pt.x	= GET_X_LPARAM(lParam);
			pt.y	= GET_Y_LPARAM(lParam);

			COLORREF c = GetPixel(calcs[gslot].hdcKeymap, pt.x, pt.y);
			if (GetRValue(c) == 0xFF) return 0;
			group	= GetGValue(c)>>4;
			bit		= GetBValue(c)>>4;

			if (group== 0x05 && bit == 0x00) {
				calcs[gslot].cpu.pio.keypad->on_pressed ^= KEY_LOCKPRESS;
				/*if ( calcs[gslot].cpu.pio.keypad->on_pressed &  KEY_LOCKPRESS ) {
					DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &pt, DBS_DOWN | DBS_LOCK);
				} else {
					DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &pt, DBS_LOCK | DBS_UP);
				}*/
			} //else {
				kp->keys[group][bit] ^= KEY_LOCKPRESS;
				if (kp->keys[group][bit] &  KEY_LOCKPRESS ) {
					DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &pt, DBS_DOWN | DBS_LOCK);
				} else {
					DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &pt, DBS_LOCK | DBS_UP);
				}

			//}
			SendMessage(hwnd, WM_SIZE, 0, 0);
			return 0;
		}

		case WM_KEYDOWN: {
			/* make this an accel*/
			if (wParam == VK_F8) {
				if (calcs[gslot].speed == 100)
					SendMessage(hwnd, WM_COMMAND, IDM_SPEED_QUADRUPLE, 0);
				else
					SendMessage(hwnd, WM_COMMAND, IDM_SPEED_NORMAL, 0);
			}

			if (wParam == VK_SHIFT) {
				if (GetKeyState(VK_LSHIFT) & 0xFF00) {
					wParam = VK_LSHIFT;
				} else {
					wParam = VK_RSHIFT;
				}
			}

			keyprog_t *kp = keypad_key_press(&calcs[gslot].cpu, wParam);
			if (kp) {
				extern POINT ButtonCenter83[64];
				extern POINT ButtonCenter84[64];
				if ((calcs[gslot].cpu.pio.keypad->keys[kp->group][kp->bit] & KEY_STATEDOWN) == 0) {
					/*if (calcs[gslot].model == TI_84P || calcs[gslot].model == TI_84PSE) {
						DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &ButtonCenter84[kp->bit+(kp->group<<3)], DBS_DOWN | DBS_PRESS);
					} else {
						DrawButtonState(calcs[gslot].hdcSkin, calcs[gslot].hdcKeymap, &ButtonCenter83[kp->bit+(kp->group<<3)], DBS_DOWN | DBS_PRESS);
					}*/
					calcs[gslot].cpu.pio.keypad->keys[kp->group][kp->bit] |= KEY_STATEDOWN;
					SendMessage(hwnd, WM_SIZE, 0, 0);
					goto finalize_buttons;
				}
			}
			return 0;
		}
		case WM_KEYUP:
			if (wParam == VK_SHIFT) {
				keypad_key_release(&calcs[gslot].cpu, VK_LSHIFT);
				keypad_key_release(&calcs[gslot].cpu, VK_RSHIFT);
			} else {
				keypad_key_release(&calcs[gslot].cpu, wParam);
			}
			goto finalize_buttons;
		case WM_SIZING:
		{
			if (calcs[gslot].SkinEnabled)
				return TRUE;
			RECT *prc = (RECT *) lParam;
			LONG ClientAdjustWidth, ClientAdjustHeight;
			LONG AdjustWidth, AdjustHeight;

			// Adjust for border and menu
			RECT rc = {0, 0, 0, 0};
			AdjustWindowRect(&rc, WS_CAPTION | WS_TILEDWINDOW, FALSE);
			rc.bottom += GetSystemMetrics(SM_CYMENU);

			RECT src;
			if (calcs[gslot].hwndStatusBar != NULL) {
				GetWindowRect(calcs[gslot].hwndStatusBar, &src);
				rc.bottom += src.bottom - src.top;
			}

			ClientAdjustWidth = rc.right - rc.left;
			ClientAdjustHeight = rc.bottom - rc.top;


			switch (wParam) {
			case WMSZ_BOTTOMLEFT:
			case WMSZ_LEFT:
			case WMSZ_TOPLEFT:
				prc->left -= 128 / 4;
				break;
			default:
				prc->right += 128 / 4;
				break;
			}

			switch (wParam) {
			case WMSZ_TOPLEFT:
			case WMSZ_TOP:
			case WMSZ_TOPRIGHT:
				prc->top -= 64 / 4;
				break;
			default:
				prc->bottom += 64 / 4;
				break;
			}


			// Make sure the width is a nice clean proportional sizing
			AdjustWidth = (prc->right - prc->left - ClientAdjustWidth) % 128;
			//AdjustHeight = (prc->bottom - prc->top) % calcs[gslot].cpu.pio.lcd->height;
			AdjustHeight = (prc->bottom - prc->top - ClientAdjustHeight) % 64;

			int cx_mult = (prc->right - prc->left - ClientAdjustWidth) / 128;
			int cy_mult = (prc->bottom - prc->top - ClientAdjustHeight) / 64;

			while (cx_mult < 2 || cy_mult < 2) {
				if (cx_mult < 2) {cx_mult++; AdjustWidth -= 128;}
				if (cy_mult < 2) {cy_mult++; AdjustHeight -= 64;}
			}

			if (cx_mult > cy_mult)
				AdjustWidth += (cx_mult - cy_mult) * 128;
			else if (cy_mult > cx_mult)
				AdjustHeight += (cy_mult - cx_mult) * 64;

			calcs[gslot].Scale = max(cx_mult, cy_mult);

			switch (wParam) {
			case WMSZ_BOTTOMLEFT:
			case WMSZ_LEFT:
			case WMSZ_TOPLEFT:
				prc->left += AdjustWidth;
				break;
			default:
				prc->right -= AdjustWidth;
				break;
			}

			switch (wParam) {
			case WMSZ_TOPLEFT:
			case WMSZ_TOP:
			case WMSZ_TOPRIGHT:
				prc->top += AdjustHeight;
				break;
			default:
				prc->bottom -= AdjustHeight;
				break;
			}
			RECT rect;
			GetClientRect(hwnd, &rect);
			InvalidateRect(hwnd, &rect, TRUE);
			return TRUE;
		}
		case WM_SIZE:
		{
			RECT rc, screen;
			GetClientRect(hwnd, &rc);
			HMENU hmenu = GetMenu(hwnd);
			int cyMenu;			if (hmenu == NULL) {
				cyMenu = 0;
			} else {
				cyMenu = GetSystemMetrics(SM_CYMENU);
			}
			if ((calcs[gslot].bCutout && calcs[gslot].SkinEnabled))	
				rc.bottom += cyMenu;
			int desired_height;
			if (calcs[gslot].SkinEnabled)
				desired_height = calcs[gslot].rectSkin.bottom;
			else
				desired_height = 128;

			double status_height;
			if (calcs[gslot].hwndStatusBar == NULL) {
				status_height = 0.0;
			} else {
				RECT src;
				GetWindowRect(calcs[gslot].hwndStatusBar, &src);

				status_height = src.bottom - src.top;
				desired_height += status_height;

			}

			rc.bottom -= status_height;

			double xc, yc;
			if (calcs[gslot].SkinEnabled) {
				xc = 1/*((double)rc.right)/calcs[gslot].rectSkin.right*/;
				yc = 1/*((double)rc.bottom)/(calcs[gslot].rectSkin.bottom)*/;
			} else {
				xc = ((double)rc.right)/256.0;
				yc = ((double)rc.bottom)/(128.0);
			}
			SetRect(&screen,
				0, 0,
				calcs[gslot].cpu.pio.lcd->width*2*xc,
				64*2*yc);

			if (calcs[gslot].SkinEnabled)
				OffsetRect(&screen, calcs[gslot].rectLCD.left, calcs[gslot].rectLCD.top);
			else
				OffsetRect(&screen, ((rc.right - calcs[gslot].cpu.pio.lcd->width*2*xc)/2), 0);

			if ((rc.right - rc.left) & 1) rc.right++;
			if ((rc.bottom - rc.top) & 1) rc.bottom++;

			RECT client;
			client.top = 0;
			client.left = 0;
			if (calcs[gslot].SkinEnabled && calcs[gslot].bCutout)
				GetWindowRect(hwnd, &client);
			if (calcs[gslot].SkinEnabled) {
				RECT correctSize = calcs[gslot].rectSkin;
				AdjustWindowRect(&correctSize, (WS_TILEDWINDOW |  WS_VISIBLE | WS_CLIPCHILDREN) & ~(WS_MAXIMIZEBOX), cyMenu);
				if (correctSize.left < 0)
					correctSize.right -= correctSize.left;
				if (correctSize.top < 0)	
					correctSize.bottom -= correctSize.top;
				SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, correctSize.right, correctSize.bottom , SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_DRAWFRAME);
			}
			MoveWindow(calcs[gslot].hwndLCD, screen.left + client.left, screen.top + client.top,
				screen.right-screen.left, screen.bottom-screen.top, FALSE);
			ValidateRect(hwnd, &screen);
			//printf("screen: %d\n", screen.right - screen.left);
			if (calcs[gslot].hwndStatusBar != NULL)
				SendMessage(calcs[gslot].hwndStatusBar, WM_SIZE, 0, 0);

			UpdateWindow(calcs[gslot].hwndLCD);
			//InvalidateRect(hwnd, NULL, FALSE);
			return 0;
		}
		case WM_MOVE:
		//case WM_MOVING:
		{
			if (calcs[gslot].bCutout && calcs[gslot].SkinEnabled)
			{
				HDWP hdwp = BeginDeferWindowPos(3);

				RECT rc;
				GetWindowRect(hwnd, &rc);
				OffsetRect(&rc, calcs[gslot].rectLCD.left, calcs[gslot].rectLCD.top);
				DeferWindowPos(hdwp, calcs[gslot].hwndLCD, HWND_TOP, rc.left, rc.top, 0, 0, SWP_NOSIZE);
				//				SendMessage(calcs[gslot].hwndLCD, WM_PAINT, 0, 0);
				//				SetActiveWindow(calcs[gslot].hwndLCD);
				RECT wr;
				GetWindowRect(hwnd, &wr);

				HWND hwnd;
				hwnd = FindWindow(_T("WABBITSMALLBUTTON"), _T("wabbitminimize"));
				DeferWindowPos(hdwp, hwnd, NULL, wr.left + 285, wr.top + 34, 13, 13, 0);
				hwnd = FindWindow(_T("WABBITSMALLBUTTON"), _T("wabbitclose"));
				DeferWindowPos(hdwp, hwnd, NULL,wr.left + 300, wr.top + 34, 13, 13, 0);

				EndDeferWindowPos(hdwp);
			}
			return 0;
		}
		case WM_CONTEXTMENU:
		{
			ctxtPt.x = GET_X_LPARAM(lParam);
			ctxtPt.y = GET_Y_LPARAM(lParam);

			HMENU hmenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_FRAME_MENU));
		    // TrackPopupMenu cannot display the menu bar so get
		    // a handle to the first shortcut menu.
			hmenu = GetSubMenu(hmenu, 0);

			if (!OnContextMenu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), hmenu)) {
				DefWindowProc(hwnd, Message, wParam, lParam);
			}
			ScreenToClient(hwnd, &ctxtPt);
			DestroyMenu(hmenu);
			return 0;
		}
		//case WM_CLOSE:
		//	DestroyWindow(hwnd);
		//	return 0;

		case WM_DESTROY:

			// Delete the skin specific to this calc
			DeleteDC(calcs[gslot].hdcSkin);
			// If it's the last calc, delete the keymap also

			if (calc_count() == 1) {
				if (exit_save_state)
				{
					char temp_save[MAX_PATH];
					strcpy(temp_save, getenv("appdata"));
					strcat(temp_save, "\\wabbitemu.sav");
					strcpy(calcs[gslot].rom_path, temp_save);
					WriteSave(temp_save, SaveSlot(gslot), false);
				}

				printf("Releasing keymap\n");
				DeleteDC(calcs[gslot].hdcKeymap);
				printf("Saving registry settings\n");
				SaveRegistrySettings();

			}

			UnregisterDropWindow(hwnd, calcs[gslot].pDropTarget);

			printf("Freeing calculator slot\n");
			calc_slot_free(gslot);

			if (calc_count() == 0)
				PostQuitMessage(0);
			return 0;

		case WM_NCHITTEST:
		{
			int htRet = DefWindowProc(hwnd, Message, wParam, lParam);
			if (htRet != HTCLIENT) return htRet;

			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			if (calcs[gslot].bCutout && calcs[gslot].SkinEnabled) {
				pt.y += GetSystemMetrics(SM_CYCAPTION);
				pt.x += GetSystemMetrics(SM_CXFIXEDFRAME);
			}
			ScreenToClient(hwnd, &pt);
			if (GetRValue(GetPixel(calcs[gslot].hdcKeymap, pt.x, pt.y)) != 0xFF)
				return htRet;
			return HTCAPTION;
		}
		default:
			return DefWindowProc(hwnd, Message, wParam, lParam);
	}
	return 0;
}

INT_PTR CALLBACK AboutDialogProc(HWND hwndDlg, UINT Message, WPARAM wParam, LPARAM lParam) {
	switch (Message) {
	case WM_INITDIALOG:
		return FALSE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			EndDialog(hwndDlg, IDOK);
			return TRUE;
		}
	case IDCANCEL:
		EndDialog(hwndDlg, IDCANCEL);
		break;
	}
	return FALSE;
}