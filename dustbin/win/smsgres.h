/* Weditres generated include file. Do NOT edit */
#include <windows.h>
#define	IDD_MAINDIALOG	100
/*@ Prototypes @*/
#ifndef WEDIT_PROTOTYPES
#define WEDIT_PROTOTYPES
typedef struct tagWeditDlgParams {
	int underscoreYOffset;
	DWORD TextColor;
	int Styles;
} WEDITDLGPARAMS;

#endif
void SetDlgBkColor(HWND,COLORREF);
BOOL APIENTRY HandleCtlColor(UINT,DWORD);
/*
 * Callbacks for dialog Dlg100
 */
void AddGdiObject(HWND,HANDLE);
BOOL WINAPI HandleDefaultMessages(HWND hwnd,UINT msg,WPARAM wParam,DWORD lParam);
void Dlg100Init(HWND hDlg,UINT wParam,DWORD lParam);
void CenterWindow(HWND,int);
HFONT SetDialogFont(HWND hwnd,char *name,int size,int type);
BOOL APIENTRY Dlg100(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
extern void *GetDialogArguments(HWND);
extern char *GetDico(int,long);
/*@@ End Prototypes @@*/
