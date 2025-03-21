	/*******************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2012-2013 Intel Corporation. All Rights Reserved.

*******************************************************************************/
#include <Windows.h>
#include <WindowsX.h>
#include <commctrl.h>
#include <map>
#include "math.h"
#include "resource.h"
#include "pxcfacemodule.h"
#include "pxcfacedata.h"
#include "pxcvideomodule.h"
#include "pxcfaceconfiguration.h"
#include "FaceTrackingFrameRateCalculator.h"
#include "FaceTrackingRenderer.h"
#include "FaceTrackingUtilities.h"
#include "FaceTrackingProcessor.h"
#include "SerialClass.h"



pxcCHAR fileName[1024] = { 0 };
PXCSession* session = NULL;
FaceTrackingRenderer* renderer = NULL;
FaceTrackingProcessor* processor = NULL;
Serial* serial = NULL;


volatile bool isRunning = false;
volatile bool isStopped = false;

static HANDLE h_python_process = NULL;


//Fun��o que abre o python
void RunPython() {
	STARTUPINFO info = { 0, };
	PROCESS_INFORMATION pinfo;
	if (CreateProcess(L"C:\\Python27\\python.exe", L"C:\\Python27\\python.exe \"C:\\Users\\eduardo\\Desktop\\Faculdade e Projetos\\face_tracking\\teste.py\"",
		NULL, NULL, FALSE, 0, NULL, NULL, &info, &pinfo)) {
		h_python_process = pinfo.hProcess;
		CloseHandle(pinfo.hThread);
	}
}

//Fun��o que fecha o python
void StopPython() {
	if (h_python_process) {
		TerminateProcess(h_python_process, 0);
		CloseHandle(h_python_process);
		h_python_process = NULL;
	}
}

static int controls[] = { IDC_SCALE, IDC_MIRROR, IDC_LOCATION, IDC_LANDMARK, IDC_POSE, IDC_EXPRESSIONS, ID_START, ID_STOP, IDC_RECOGNITION, ID_REGISTER, ID_UNREGISTER };
static RECT layout[3 + sizeof(controls) / sizeof(controls[0])];

std::map<int, PXCFaceConfiguration::TrackingModeType> CreateProfileMap()
{
	
	std::map<int, PXCFaceConfiguration::TrackingModeType> map;
	map[0] = PXCFaceConfiguration::TrackingModeType::FACE_MODE_COLOR_PLUS_DEPTH;
	map[1] = PXCFaceConfiguration::TrackingModeType::FACE_MODE_COLOR;
	return map;
}

std::map<int, PXCFaceConfiguration::TrackingModeType> s_profilesMap = CreateProfileMap();

pxcCHAR* GetStringFromFaceMode(PXCFaceConfiguration::TrackingModeType mode)
{
	switch (mode) 
	{
	case PXCFaceConfiguration::TrackingModeType::FACE_MODE_COLOR:
		return L"2D";
	case PXCFaceConfiguration::TrackingModeType::FACE_MODE_COLOR_PLUS_DEPTH:
		return L"3D";
	}

	return L"";
}

void GetPlaybackFile(void) 
{
	OPENFILENAME filename;
	memset(&filename, 0, sizeof(filename));
	filename.lStructSize = sizeof(filename);
	filename.lpstrFilter = L"RSSDK clip (*.rssdk)\0*.rssdk\0Old format clip (*.pcsdk)\0*.pcsdk\0All Files (*.*)\0*.*\0\0";
	filename.lpstrFile = fileName; 
	fileName[0] = 0;
	filename.nMaxFile = sizeof(fileName) / sizeof(pxcCHAR);
	filename.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (!GetOpenFileName(&filename)) 
		fileName[0] = 0;
}

void GetRecordFile(void) 
{
	OPENFILENAME filename;
	memset(&filename, 0, sizeof(filename));
	filename.lStructSize = sizeof(filename);
	filename.lpstrFilter = L"RSSDK clip (*.rssdk)\0*.rssdk\0All Files (*.*)\0*.*\0\0";
	filename.lpstrFile = fileName; 
	fileName[0] = 0;
	filename.nMaxFile = sizeof(fileName) / sizeof(pxcCHAR);
	filename.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (GetSaveFileName(&filename)) {
		if (filename.nFilterIndex==1 && filename.nFileExtension==0) {
			int len = wcslen(fileName);
			if (len>1 && len<sizeof(fileName)/sizeof(pxcCHAR)-7) {
				wcscpy_s(&fileName[len], rsize_t(7), L".rssdk\0");
			}
		}
	} else fileName[0] = 0;
}

void PopulateDevice(HMENU menu)
{
	DeleteMenu(menu, 0, MF_BYPOSITION);

	PXCSession::ImplDesc desc;
	memset(&desc, 0, sizeof(desc)); 
	desc.group = PXCSession::IMPL_GROUP_SENSOR;
	desc.subgroup = PXCSession::IMPL_SUBGROUP_VIDEO_CAPTURE;
	HMENU menu1 = CreatePopupMenu();
	for (int i = 0, k = ID_DEVICEX; ; ++i)
	{
		PXCSession::ImplDesc desc1;
		if (session->QueryImpl(&desc, i, &desc1) < PXC_STATUS_NO_ERROR)
			break;

		PXCCapture *capture;
		if (session->CreateImpl<PXCCapture>(&desc1, &capture) < PXC_STATUS_NO_ERROR) 
			continue;

		for (int j = 0; ; ++j) {
			PXCCapture::DeviceInfo deviceInfo;
			if (capture->QueryDeviceInfo(j, &deviceInfo) < PXC_STATUS_NO_ERROR) 
				break;

			AppendMenu(menu1, MF_STRING, k++, deviceInfo.name);
		}

		capture->Release();
	}
	CheckMenuRadioItem(menu1, 0, GetMenuItemCount(menu1), 0, MF_BYPOSITION);
	InsertMenu(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)menu1, L"Device");
}

void PopulateModule(HMENU menu) 
{
	DeleteMenu(menu, 1, MF_BYPOSITION);

	PXCSession::ImplDesc desc, desc1;
	memset(&desc, 0, sizeof(desc));
	desc.cuids[0] = PXCFaceModule::CUID;
	HMENU menu1 = CreatePopupMenu();

	for (int i = 0; ; ++i)
	{
		if (session->QueryImpl(&desc, i, &desc1) < PXC_STATUS_NO_ERROR) 
			break;
		AppendMenu(menu1, MF_STRING, ID_MODULEX + i, desc1.friendlyName);
	}

	CheckMenuRadioItem(menu1, 0, GetMenuItemCount(menu1), 0, MF_BYPOSITION);
	InsertMenu(menu, 1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)menu1, L"Module");
}

void PopulateProfile(HWND dialogWindow)
{
	HMENU menu = GetMenu(dialogWindow);
	DeleteMenu(menu, 2, MF_BYPOSITION);
	HMENU menu1 = CreatePopupMenu();

	PXCSession::ImplDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.cuids[0] = PXCFaceModule::CUID;
	wcscpy_s<sizeof(desc.friendlyName) / sizeof(pxcCHAR)>(desc.friendlyName, FaceTrackingUtilities::GetCheckedModule(dialogWindow));

	PXCFaceModule *faceModule;
	if (session->CreateImpl<PXCFaceModule>(&desc, &faceModule) >= PXC_STATUS_NO_ERROR) 
	{
		for (unsigned int i = 0; i < s_profilesMap.size(); ++i)
		{
			WCHAR line[256];
			swprintf_s<sizeof(line) / sizeof(WCHAR)>(line, L"%s", GetStringFromFaceMode(s_profilesMap[i]));
			AppendMenu(menu1, MF_STRING, ID_PROFILEX + i, line);
		}

		CheckMenuRadioItem(menu1, 0, GetMenuItemCount(menu1), 0, MF_BYPOSITION);
	}

	InsertMenu(menu, 2, MF_BYPOSITION | MF_POPUP, (UINT_PTR)menu1, L"Profile");
}

void SaveLayout(HWND dialogWindow) 
{
	GetClientRect(dialogWindow, &layout[0]);
	ClientToScreen(dialogWindow, (LPPOINT)&layout[0].left);
	ClientToScreen(dialogWindow, (LPPOINT)&layout[0].right);
	GetWindowRect(GetDlgItem(dialogWindow, IDC_PANEL), &layout[1]);
	GetWindowRect(GetDlgItem(dialogWindow, IDC_STATUS), &layout[2]);
	for (int i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
		GetWindowRect(GetDlgItem(dialogWindow, controls[i]), &layout[3 + i]);
}

void RedoLayout(HWND dialogWindow)
{
	RECT rectangle;
	GetClientRect(dialogWindow, &rectangle);

	/* Status */
	SetWindowPos(GetDlgItem(dialogWindow, IDC_STATUS), dialogWindow, 
		0,
		rectangle.bottom - (layout[2].bottom - layout[2].top),
		rectangle.right - rectangle.left,
		(layout[2].bottom - layout[2].top),
		SWP_NOZORDER);

	/* Panel */
	SetWindowPos(
		GetDlgItem(dialogWindow,IDC_PANEL), dialogWindow,
		(layout[1].left - layout[0].left),
		(layout[1].top - layout[0].top),
		rectangle.right - (layout[1].left-layout[0].left) - (layout[0].right - layout[1].right),
		rectangle.bottom - (layout[1].top - layout[0].top) - (layout[0].bottom - layout[1].bottom),
		SWP_NOZORDER);

	/* Buttons & CheckBoxes */
	for (int i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
	{
		SetWindowPos(
			GetDlgItem(dialogWindow,controls[i]), dialogWindow,
			rectangle.right - (layout[0].right - layout[3 + i].left),
			(layout[3 + i].top - layout[0].top),
			(layout[3 + i].right - layout[3 + i].left),
			(layout[3 + i].bottom - layout[3 + i].top),
			SWP_NOZORDER);
	}
}

static DWORD WINAPI RenderingThread(LPVOID arg)
{
	while (true)
		renderer->Render();
}

static DWORD WINAPI ProcessingThread(LPVOID arg) 
{
	HWND window = (HWND)arg;
	processor->Process(window);

	isRunning = false;
	return 0;
}

INT_PTR CALLBACK MessageLoopThread(HWND dialogWindow, UINT message, WPARAM wParam, LPARAM) 
{ 
	HMENU menu1 = GetMenu(dialogWindow);
	HMENU menu2;

	switch (message) 
	{ 
	case WM_INITDIALOG:
		CheckDlgButton(dialogWindow, IDC_MIRROR, BST_CHECKED);
		CheckDlgButton(dialogWindow, IDC_LOCATION, BST_CHECKED);
		CheckDlgButton(dialogWindow, IDC_LANDMARK, BST_UNCHECKED);
		CheckDlgButton(dialogWindow, IDC_POSE, BST_CHECKED);
		CheckDlgButton(dialogWindow, IDC_EXPRESSIONS, BST_UNCHECKED);
		CheckDlgButton(dialogWindow, IDC_SCALE, BST_UNCHECKED);
		CheckDlgButton(dialogWindow, IDC_RECOGNITION, BST_UNCHECKED);
		Button_Enable(GetDlgItem(dialogWindow, ID_REGISTER), false);
		Button_Enable(GetDlgItem(dialogWindow, ID_UNREGISTER), false);
		PopulateDevice(menu1);
		PopulateModule(menu1);
		PopulateProfile(dialogWindow);
		SaveLayout(dialogWindow);
		return TRUE; 

	case WM_COMMAND: 
		menu2 = GetSubMenu(menu1, 0);
		if (LOWORD(wParam) >= ID_DEVICEX && LOWORD(wParam) < ID_DEVICEX + GetMenuItemCount(menu2)) 
		{
			CheckMenuRadioItem(menu2, 0, GetMenuItemCount(menu2), LOWORD(wParam) - ID_DEVICEX, MF_BYPOSITION);
			return TRUE;
		}

		menu2 = GetSubMenu(menu1, 1);
		if (LOWORD(wParam) >= ID_MODULEX && LOWORD(wParam) < ID_MODULEX + GetMenuItemCount(menu2)) 
		{
			CheckMenuRadioItem(menu2, 0, GetMenuItemCount(menu2), LOWORD(wParam) - ID_MODULEX,MF_BYPOSITION);
			PopulateProfile(dialogWindow);
			return TRUE;
		}

		menu2 = GetSubMenu(menu1, 2);
		if (LOWORD(wParam) >= ID_PROFILEX && LOWORD(wParam) < ID_PROFILEX + GetMenuItemCount(menu2)) 
		{
			CheckMenuRadioItem(menu2, 0, GetMenuItemCount(menu2), LOWORD(wParam) - ID_PROFILEX,MF_BYPOSITION);
			return TRUE;
		}

		switch (LOWORD(wParam)) 
		{
		case IDCANCEL:
			isStopped = true;
			if (isRunning) {
				PostMessage(dialogWindow, WM_COMMAND, IDCANCEL, 0);
			} else 
			{
				DestroyWindow(dialogWindow); 
				PostQuitMessage(0);
			}
			return TRUE;
		case ID_START:
			Button_Enable(GetDlgItem(dialogWindow, ID_START), false);
			Button_Enable(GetDlgItem(dialogWindow, ID_STOP), true);
			Button_Enable(GetDlgItem(dialogWindow, IDC_MIRROR), false);

			for (int i = 0;i < GetMenuItemCount(menu1); ++i)
				EnableMenuItem(menu1, i, MF_BYPOSITION | MF_GRAYED);

			DrawMenuBar(dialogWindow);
			isStopped = false;
			isRunning = true;

			if (processor) 
				delete processor;

			processor = new FaceTrackingProcessor(dialogWindow);

			CreateThread(0, 0, ProcessingThread, dialogWindow, 0, 0);
			if (FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_RECOGNITION))
			{
				Button_Enable(GetDlgItem(dialogWindow, ID_REGISTER), true);
				Button_Enable(GetDlgItem(dialogWindow, ID_UNREGISTER), true);
			}

			Button_Enable(GetDlgItem(dialogWindow, IDC_LOCATION), false);
			Button_Enable(GetDlgItem(dialogWindow, IDC_LANDMARK), false);
			Button_Enable(GetDlgItem(dialogWindow, IDC_POSE), false);
			Button_Enable(GetDlgItem(dialogWindow, IDC_EXPRESSIONS), false);
			Button_Enable(GetDlgItem(dialogWindow, IDC_RECOGNITION), false);
			
			RunPython();

			Sleep(0); //TODO: remove
			return TRUE;

		case ID_STOP:
			isStopped = true;
			if (isRunning) 
			{
				PostMessage(dialogWindow, WM_COMMAND, ID_STOP, 0);
			}
			else 
			{
				for (int i = 0; i < GetMenuItemCount(menu1); ++i)
					EnableMenuItem(menu1, i, MF_BYPOSITION | MF_ENABLED);

				DrawMenuBar(dialogWindow);
				Button_Enable(GetDlgItem(dialogWindow, ID_START), true);
				Button_Enable(GetDlgItem(dialogWindow, ID_STOP), false);
				Button_Enable(GetDlgItem(dialogWindow, IDC_MIRROR), true);
				if (FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_RECOGNITION))
				{
					Button_Enable(GetDlgItem(dialogWindow, ID_REGISTER), false);
					Button_Enable(GetDlgItem(dialogWindow, ID_UNREGISTER), false);
				}

				Button_Enable(GetDlgItem(dialogWindow, IDC_LOCATION), true);
				Button_Enable(GetDlgItem(dialogWindow, IDC_LANDMARK), true);
				Button_Enable(GetDlgItem(dialogWindow, IDC_POSE), true);
				Button_Enable(GetDlgItem(dialogWindow, IDC_EXPRESSIONS), true);
				Button_Enable(GetDlgItem(dialogWindow, IDC_RECOGNITION), true);
				StopPython();
			}
			return TRUE;

		case ID_MODE_LIVE:
			CheckMenuItem(menu1, ID_MODE_LIVE, MF_CHECKED);
			CheckMenuItem(menu1, ID_MODE_PLAYBACK, MF_UNCHECKED);
			CheckMenuItem(menu1, ID_MODE_RECORD, MF_UNCHECKED);
			return TRUE;

		case ID_MODE_PLAYBACK:
			CheckMenuItem(menu1, ID_MODE_LIVE, MF_UNCHECKED);
			CheckMenuItem(menu1, ID_MODE_PLAYBACK, MF_CHECKED);
			CheckMenuItem(menu1, ID_MODE_RECORD, MF_UNCHECKED);
			GetPlaybackFile();
			return TRUE;

		case ID_MODE_RECORD:
			CheckMenuItem(menu1, ID_MODE_LIVE, MF_UNCHECKED);
			CheckMenuItem(menu1, ID_MODE_PLAYBACK, MF_UNCHECKED);
			CheckMenuItem(menu1, ID_MODE_RECORD, MF_CHECKED);
			GetRecordFile();
			return TRUE;

		case IDC_RECOGNITION:
			return TRUE;

		case ID_REGISTER:
			processor->RegisterUser();
			return TRUE;

		case ID_UNREGISTER:
			processor->UnregisterUser();
			return TRUE;
		} 
		break;
	case WM_SIZE:
		RedoLayout(dialogWindow);
		return TRUE;

	} 
	return FALSE; 
} 

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int) {

	InitCommonControls();
	
	session = PXCSession_Create();
	if (session == NULL) 
	{
        MessageBoxW(0, L"Failed to create an SDK session", L"Face Viewer", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    HWND dialogWindow = CreateDialogW(hInstance, MAKEINTRESOURCE(IDD_MAINFRAME), 0, MessageLoopThread);
    if (!dialogWindow)  
	{
        MessageBoxW(0, L"Failed to create a window", L"Face Viewer", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

	HWND statusWindow = CreateStatusWindow(WS_CHILD | WS_VISIBLE | WS_SIZEBOX, L"", dialogWindow, IDC_STATUS);
	if (!statusWindow) 
	{
	   MessageBoxW(0, L"Failed to create a status bar", L"Face Viewer", MB_ICONEXCLAMATION | MB_OK);
        return 1;
	}

	int statusWindowParts[] = {230, -1};
	SendMessage(statusWindow, SB_SETPARTS, sizeof(statusWindowParts)/sizeof(int), (LPARAM) statusWindowParts);
	SendMessage(statusWindow, SB_SETTEXT, (WPARAM)(INT) 0, (LPARAM) (LPSTR) TEXT("OK"));
	UpdateWindow(dialogWindow);

	
	renderer = new FaceTrackingRenderer(dialogWindow);
	CreateThread(NULL, NULL, RenderingThread, NULL, NULL, NULL);

    MSG msg;
	while (int status = GetMessageW(&msg, NULL, 0, 0))
	{
        if (status == -1)
			return status;

        TranslateMessage(&msg);
        DispatchMessage(&msg);

		Sleep(0); // let other threads breathe
    }

	CloseHandle(FaceTrackingUtilities::GetRenderingFinishedSignal());

	if (processor)
		delete processor;

	if (renderer)
		delete renderer;

	session->Release();
    return (int)msg.wParam;
}
