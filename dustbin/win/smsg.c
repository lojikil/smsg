#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <string.h>
#include "smsgres.h"

static BOOL CALLBACK dialogfunc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hinstPrev, LPSTR lpCmdLine, int nCmdShow)
{
	WNDCLASS wc;

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = DefDlgProc;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hinst;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = "smsg";
	RegisterClass(&wc);

	return DialogBox(hinst, MAKEINTRESOURCE(IDD_MAINDIALOG), NULL, (DLGPROC)dialogfunc);
}


static int init_app(HWND hDlg, WPARAM wParam, LPARAM lParam)
{
	return 1;
}

static BOOL CALLBACK dialogfunc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG:
		init_app(hwndDlg, wParam, lParam);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			EndDialog(hwndDlg, 1);
			return 1;
		case IDCANCEL:
			EndDialog(hwndDlg, 0);
			return 1;
		}
		break;
	case WM_CLOSE:
		EndDialog(hwndDlg, 0);
		return TRUE;
	}
	return FALSE;
}
