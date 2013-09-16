#include "stdafx.h"
#include "resource.h"
#include "Globals.h"
#include "PluginOptions.h"
#include "Translator.h"
#include "UninstEntries.h"

// Global variables.
bool SystemX64 = false;
HICON DefaultIconSmall, DefaultIconLarge;
HINSTANCE PluginInstance;
HCURSOR HandCursor;
HFONT UnderlineFont;
WCHAR PluginDirectory[MAX_PATH_EX];
WCHAR VersionStr[24];


BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	UNUSED(lpReserved);

    switch (ul_reason_for_call)
	{
    case DLL_PROCESS_ATTACH:
		{
			PluginInstance = (HINSTANCE)hModule;

			// Get the plugin file location.
			GetModuleFileName(PluginInstance, PluginDirectory, MAX_PATH_EX);

			// Get the plugin version number from its VERSIONINFO resource.
			VS_FIXEDFILEINFO* fi;
			UINT tmp_val;
			DWORD sz = GetFileVersionInfoSize(PluginDirectory, (DWORD*)&tmp_val);
			void* hlres = new BYTE[sz];
			if (GetFileVersionInfo(PluginDirectory, 0, sz, hlres) && VerQueryValue(hlres, L"\\", (void**)&fi, &tmp_val))
			{
				if (LOWORD(fi->dwProductVersionLS))
					swprintf_s(VersionStr, L"%d.%d.%d.%d", HIWORD(fi->dwProductVersionMS), LOWORD(fi->dwProductVersionMS), HIWORD(fi->dwProductVersionLS), LOWORD(fi->dwProductVersionLS));
				else
					swprintf_s(VersionStr, L"%d.%d.%d", HIWORD(fi->dwProductVersionMS), LOWORD(fi->dwProductVersionMS), HIWORD(fi->dwProductVersionLS));
			}
			else
			{
				wcscpy_len(VersionStr, L"<?>");
			}
			delete[] hlres;

			// The full path to the plugin file will not be needed anymore, but its location will be required, so store it globally.
			WCHAR* pos = wcsrchr(PluginDirectory, L'\\');
			if (pos != NULL)
				*pos = L'\0';

			// Obtain the real system architecture.
			// GetNativeSystemInfo is missing in Win2000, so load it dynamically.
			WCHAR syspath[MAX_PATH];
			HMODULE hKernel32;
			typedef void (WINAPI *tGetNativeSystemInfo)(LPSYSTEM_INFO);
			tGetNativeSystemInfo fGetNativeSystemInfo;
			SYSTEM_INFO sInfo;
			GetSystemDirectory(syspath, MAX_PATH);
			wcscat_s(syspath, MAX_PATH, L"\\kernel32.dll");
			hKernel32 = LoadLibrary(syspath);
			fGetNativeSystemInfo = (tGetNativeSystemInfo)GetProcAddress(hKernel32, "GetNativeSystemInfo");
			sInfo.wProcessorArchitecture = PROCESSOR_ARCHITECTURE_UNKNOWN;
			if (fGetNativeSystemInfo != NULL)
				fGetNativeSystemInfo(&sInfo);
			FreeLibrary(hKernel32);
			SystemX64 = (sInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64);

			// Load default icons for uninstallation entries.
			DefaultIconSmall = (HICON)LoadImage((HINSTANCE)hModule, MAKEINTRESOURCE(IDI_DEFAULT_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
			DefaultIconLarge = (HICON)LoadImage((HINSTANCE)hModule, MAKEINTRESOURCE(IDI_DEFAULT_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);

			// Load the "hand" cursor for highlighting hyperlinks in dialogs.
			HandCursor = (HCURSOR)LoadImage(NULL, IDC_HAND, IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
		}
		break;
    case DLL_THREAD_ATTACH:
		break;
    case DLL_THREAD_DETACH:
		break;
    case DLL_PROCESS_DETACH:
		{
			// Unload icons to free resources.
			DestroyIcon(DefaultIconSmall);
			DestroyIcon(DefaultIconLarge);
		}
		break;
    }
    return TRUE;
}


size_t wcscpy_len(wchar_t* dst, size_t bufsize, const wchar_t* src)
{
	if (dst == NULL)
		return 0;
	if (src == NULL)
	{
		dst[0] = L'\0';
		return 0;
	}

	dst[bufsize - 1] = L'\0';
	size_t res = 0;
	while ((res < bufsize - 1) && ((*dst++ = *src++) != L'\0'))
		++res;
	return res;
}

size_t strcpy_len(char* dst, size_t bufsize, const char* src)
{
	if (dst == NULL)
		return 0;
	if (src == NULL)
	{
		dst[0] = '\0';
		return 0;
	}

	dst[bufsize - 1] = '\0';
	size_t res = 0;
	while ((res < bufsize - 1) && ((*dst++ = *src++) != '\0'))
		++res;
	return res;
}

bool FileExists(const wchar_t* Path)
{
	// Results of performance testing (for existing/not existing file):
	// FindFirstFile+FindClose : 91.72/66.41 microsec/call
	// GetFileAttributes       : 10.00/15.47 microsec/call <-- best
	// CreateFile+CloseHandle  : 52.50/34.37 microsec/call
	DWORD attr = GetFileAttributes(Path);
	return ((attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0));
}

// Internal implementation for recursive deleting the registry key.
bool RegDeleteKeyRecursiveImpl(HKEY hKey, const WCHAR* KeyName, DWORD PermsX64)
{
	HKEY hKeyEntry;
	if (RegOpenKeyEx(hKey, KeyName, 0, KEY_ENUMERATE_SUB_KEYS | DELETE | PermsX64, &hKeyEntry) != ERROR_SUCCESS)
		return false;

	bool res = true;

	// Enumerating all the subkeys and recursively delete them.
	DWORD idx = 0;
	WCHAR SubKeyName[BUF_SZ];
	DWORD SubKeyNameSz = BUF_SZ;
	while (RegEnumKeyEx(hKeyEntry, idx, SubKeyName, &SubKeyNameSz, NULL, NULL, 0, NULL) == ERROR_SUCCESS)
	{
		res = (RegDeleteKeyRecursiveImpl(hKeyEntry, SubKeyName, PermsX64) == ERROR_SUCCESS) && res;
		SubKeyNameSz = BUF_SZ;
		++idx;
	}
	RegCloseKey(hKeyEntry);

	// Now delete the key itself.
	res = (RegDeleteKey(hKey, KeyName) == ERROR_SUCCESS) && res;
	return res;
}

bool RegDeleteKeyRecursive(HKEY hKey, const WCHAR* RegBranch, const WCHAR* KeyName, DWORD PermsX64)
{
	// Open the registry branch, then perform deletion relative to this branch.
	HKEY hKeyBranch;
	if (RegOpenKeyEx(hKey, RegBranch, 0, KEY_ENUMERATE_SUB_KEYS | DELETE | PermsX64, &hKeyBranch) != ERROR_SUCCESS)
		return false;
	bool res = RegDeleteKeyRecursiveImpl(hKeyBranch, KeyName, PermsX64);
	RegCloseKey(hKeyBranch);
	return res;
}

// Internal implementation for updating a registry key's value or deleting it without reporting an error if the value is missing already.
bool RegUpdateValue(HKEY hKey, const WCHAR* ValueName, const WCHAR* Value, int len)
{
	if (len > 0)
		return (RegSetValueEx(hKey, ValueName, NULL, REG_SZ, (LPBYTE)Value, (len + 1) * sizeof(WCHAR)) == ERROR_SUCCESS);
	else
	{
		LONG res = RegDeleteValue(hKey, ValueName);
		return ((res == ERROR_SUCCESS) || (res == ERROR_FILE_NOT_FOUND));
	}
}

// Internal function for opening a URL using the default application.
void OpenURL(HWND ParentWnd, const WCHAR* URL)
{
	// TODO: Find out why ShellExecute returns ERROR_FILE_NOT_FOUND for mailto action if DDE is used.
	ShellExecute(ParentWnd, L"open", URL, NULL, NULL, SW_SHOWNORMAL);
// 	int exec_res = (int)ShellExecute(ParentWnd, L"open", URL, NULL, NULL, SW_SHOWNORMAL);
// 	if (exec_res <= 32)
// 		MessageBox(ParentWnd, GlobalTranslator.GetLine(LNG_MSG_FAILED_TO_OPEN_URL).c_str(), PluginNameW, MB_OK | MB_ICONEXCLAMATION);
}

// Internal function for relocating a dialog so that it was centered over the parent window.
void CenterDialog(HWND hwndDlg)
{
	// Calculate the center point of the parent window and move the dialog appropriately.
	HWND hParentWnd = GetParent(hwndDlg);
	RECT rcMain, rcDlg;
	GetWindowRect(hParentWnd, &rcMain);
	GetWindowRect(hwndDlg, &rcDlg);
	POINT ptMainCenter = { (rcMain.left + rcMain.right) / 2, (rcMain.top + rcMain.bottom) / 2 };
	SIZE szDlg = { rcDlg.right - rcDlg.left, rcDlg.bottom - rcDlg.top };
	MoveWindow(hwndDlg, ptMainCenter.x - szDlg.cx / 2, ptMainCenter.y - szDlg.cy / 2, szDlg.cx, szDlg.cy, FALSE);

	// Relocate the dialog to make it fully visible if it came over the screen edge.
	SendMessage(hwndDlg, DM_REPOSITION, NULL, NULL);
}

// Translation map for the Settings dialog.
static DWORD LangIDs_Settings[] = {
	(DWORD)-1,                 LNG_SETTINGS_DLGTITLE,
	IDC_SHOW_HIDDEN,           LNG_SETTINGS_SHOW_HIDDEN_ENTRIES,
	IDC_SHOW_HIDDEN_ICONS,     LNG_SETTINGS_SHOW_HIDDEN_ICONS,
	IDC_SHOW_HOTFIXES,         LNG_SETTINGS_SHOW_HOTFIXES,
	IDC_CONFIRM_UNINST,        LNG_SETTINGS_CONFIRM_UNINSTALLATION,
	IDC_CONFIRM_UNINST_HIDDEN, LNG_SETTINGS_CONFIRM_UNINSTALLATION_HIDDEN,
	IDC_INI_LOCATION_TXT,      LNG_SETTINGS_INI_LOCATION,
	IDC_LANGUAGE_TXT,          LNG_SETTINGS_LANGUAGE,
	IDC_ABOUT,                 LNG_SETTINGS_ABOUT,
	IDOK,                      LNG_SETTINGS_OK,
	IDCANCEL,                  LNG_SETTINGS_CANCEL,
	0
};

INT_PTR CALLBACK SettingsDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	UNUSED(lParam);

	switch (uMsg)
	{
	case WM_INITDIALOG:
		{
			// Center the dialog relative to the parent window.
			CenterDialog(hwndDlg);

			// Translate the dialog.
			GlobalTranslator.TranslateDialog(hwndDlg, LangIDs_Settings);

			// Set checkboxes according to currently used options.
			Button_SetCheck(GetDlgItem(hwndDlg, IDC_SHOW_HIDDEN), GlobalOptions.m_ShowHiddenEntries);
			Button_SetCheck(GetDlgItem(hwndDlg, IDC_SHOW_HIDDEN_ICONS), GlobalOptions.m_ShowHiddenEntryIcons);
			if (!GlobalOptions.m_ShowHiddenEntries)
				Button_Enable(GetDlgItem(hwndDlg, IDC_SHOW_HIDDEN_ICONS), FALSE);
			Button_SetCheck(GetDlgItem(hwndDlg, IDC_SHOW_HOTFIXES), GlobalOptions.m_ShowHotfixes);
			Button_SetCheck(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST), GlobalOptions.m_RequestUninstallationLaunch);
			if (GlobalOptions.m_RequestUninstallationLaunch)
			{
				Button_SetCheck(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST_HIDDEN), TRUE);
				Button_Enable(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST_HIDDEN), FALSE);
			}
			else
			{
				Button_SetCheck(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST_HIDDEN), GlobalOptions.m_RequestHiddenUninstallationLaunch);
			}

			// Initialize the list of available storage locations.
			HWND hIniLocation = GetDlgItem(hwndDlg, IDC_INI_LOCATION);
			ComboBox_AddString(hIniLocation, GlobalTranslator.GetLine(LNG_SETTINGS_INI_PLUGIN_DIRECTORY).c_str());
			ComboBox_AddString(hIniLocation, GlobalTranslator.GetLine(LNG_SETTINGS_INI_TC_DIRECTORY).c_str());
			ComboBox_AddString(hIniLocation, GlobalTranslator.GetLine(LNG_SETTINGS_INI_WINCMD_INI_DIRECTORY).c_str());
			ComboBox_SetCurSel(hIniLocation, GlobalOptions.m_StorageLocation);

			// Initialize the list of languages.
			HWND hLanguage = GetDlgItem(hwndDlg, IDC_LANGUAGE);

			// Insert embedded English (no external file is associated).
			ComboBox_AddString(hLanguage, L"<English>");
			ComboBox_SetItemData(hLanguage, 0, NULL);

			// Enumerate all LNG files in the Language subdirectory of the plugin directory.
			WIN32_FIND_DATA ffData;
			WCHAR* LngFileData = NULL;
			size_t LngFileDataSz = 0;
			wstring LngDirectory = wstring(PluginDirectory) + L"\\Language\\";
			bool LangSelected = false;
			HANDLE ff = FindFirstFile((LngDirectory + L"*.lng").c_str(), &ffData);
			if (ff != INVALID_HANDLE_VALUE)
			{
				do {
					// Read each language file and extract language name from it (by convention it is stored as line 0).
					HANDLE LngFile = CreateFile((LngDirectory + ffData.cFileName).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
					if (LngFile != INVALID_HANDLE_VALUE)
					{
						// Read file contents into memory
						LARGE_INTEGER FileSz;
						if (!GetFileSizeEx(LngFile, &FileSz))
						{
							CloseHandle(LngFile);
							continue;
						}
						size_t NewLngFileDataSz = (size_t)(FileSz.QuadPart / sizeof(WCHAR));
						if (NewLngFileDataSz > LngFileDataSz)
						{
							// Reallocate, if the former allocation was not large enough.
							delete[] LngFileData;
							LngFileDataSz = NewLngFileDataSz;
							LngFileData = new WCHAR[LngFileDataSz + 1];
						}
						DWORD dr;
						BOOL res = (ReadFile(LngFile, LngFileData, (DWORD)FileSz.QuadPart, &dr, NULL) && (dr == FileSz.QuadPart));
						CloseHandle(LngFile);
						if (!res)
							continue;
						LngFileData[LngFileDataSz] = L'\0';

						// Searching for the language name.
						WCHAR* pos1 = wcsstr(LngFileData, L"\x0a" L"0=\"");
						if (pos1 == NULL)
							continue;
						pos1 += 4;
						WCHAR* pos2 = wcsstr(pos1, L"\"");
						if (pos2 == NULL)
							continue;
						*pos2 = L'\0';

						// Insert the new language entry into the combobox.
						int idx = ComboBox_AddString(hLanguage, (wstring(pos1) + L" - " + ffData.cFileName).c_str());

						// Associate this entry with the file name.
						WCHAR* FileName = _wcsdup(ffData.cFileName);
						ComboBox_SetItemData(hLanguage, idx, FileName);

						// Check whether this language file is now used (to pre-select it in the list).
						if (_wcsicmp(FileName, GlobalOptions.m_Language) == 0)
						{
							ComboBox_SetCurSel(hLanguage, idx);
							LangSelected = true;
						}
					}
				} while (FindNextFile(ff, &ffData));
			}
			delete[] LngFileData;

			// If no language file was selected, select the embedded English.
			if (!LangSelected)
				ComboBox_SetCurSel(hLanguage, 0);

			return TRUE;
		}
		break;
	case WM_DESTROY:
		{
			// When unloading, free the resources (allocated strings with file names).
			HWND hLanguage = GetDlgItem(hwndDlg, IDC_LANGUAGE);
			for (int i = 0; i < ComboBox_GetCount(hLanguage); ++i)
				free((WCHAR*)ComboBox_GetItemData(hLanguage, i));
			return TRUE;
		}
		break;
	case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case IDC_SHOW_HIDDEN:
				{
					// ShowHiddenEntryIcons makes sense only if hidden entries themselves are listed.
					Button_Enable(GetDlgItem(hwndDlg, IDC_SHOW_HIDDEN_ICONS), Button_GetCheck(GetDlgItem(hwndDlg, IDC_SHOW_HIDDEN)));
					return TRUE;
				}
			case IDC_CONFIRM_UNINST:
				{
					// If all uninstallations are confirmed, then the hidden ones even more so.
					if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST)))
					{
						Button_SetCheck(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST_HIDDEN), TRUE);
						Button_Enable(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST_HIDDEN), FALSE);
					}
					else
					{
						Button_Enable(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST_HIDDEN), TRUE);
					}
					return TRUE;
				}
			case IDOK:
				{
					// Store and use the new configuration.
					GlobalOptions.m_ShowHiddenEntries = (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SHOW_HIDDEN)) ? true : false);
					GlobalOptions.m_ShowHiddenEntryIcons = (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SHOW_HIDDEN_ICONS)) ? true : false);
					GlobalOptions.m_ShowHotfixes = (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SHOW_HOTFIXES)) ? true : false);
					GlobalOptions.m_RequestUninstallationLaunch = (Button_GetCheck(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST)) ? true : false);
					if (GlobalOptions.m_RequestUninstallationLaunch)
						GlobalOptions.m_RequestHiddenUninstallationLaunch = true;
					else
						GlobalOptions.m_RequestHiddenUninstallationLaunch = (Button_GetCheck(GetDlgItem(hwndDlg, IDC_CONFIRM_UNINST_HIDDEN)) ? true : false);
					GlobalOptions.SetStorageLocation((StorageLocation)ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_INI_LOCATION)));
					HWND hLanguage = GetDlgItem(hwndDlg, IDC_LANGUAGE);
					WCHAR* FileName = (WCHAR*)ComboBox_GetItemData(hLanguage, ComboBox_GetCurSel(hLanguage));
					if (FileName != NULL)
						wcscpy_len(GlobalOptions.m_Language, FileName);
					else
						GlobalOptions.m_Language[0] = L'\0';
					if (!GlobalTranslator.ReadLanguageFile(FileName))
						MessageBox(hwndDlg, L"Failed to apply translation. Using English instead.", PluginNameW, MB_ICONEXCLAMATION | MB_OK);

					// Save the settings.
					if (!GlobalOptions.Write())
						MessageBox(hwndDlg, GlobalTranslator.GetLine(LNG_MSG_FAILED_TO_WRITE_INI).c_str(), PluginNameW, MB_ICONERROR | MB_OK);

					// Close the dialog.
					EndDialog(hwndDlg, IDOK);
					return TRUE;
				}
			case IDCANCEL:
				{
					// Close the dialog (IDCANCEL is also sent when pressing Esc or [X] titlebar button).
					EndDialog(hwndDlg, IDCANCEL);
					return TRUE;
				}
			case IDC_ABOUT:
				{
					// Open the About dialog.
					DialogBox(PluginInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hwndDlg, AboutDialogProc);
					return TRUE;
				}
			}
		}
		break;
	}
	return FALSE;
}

// Translation map for the About dialog (partial, only fixed labels).
static DWORD LangIDs_About[] = {
	(DWORD)-1,        LNG_ABOUT_DLGTITLE,
	IDC_HOMEPAGE_TXT, LNG_ABOUT_HOMEPAGE,
	IDC_EMAIL_TXT,    LNG_ABOUT_EMAIL,
	IDOK,             LNG_PROPERTIES_OK,
	0
};

INT_PTR CALLBACK AboutDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		{
			// Center the dialog relative to the parent window.
			CenterDialog(hwndDlg);

			// Translate the dialog.
			GlobalTranslator.TranslateDialog(hwndDlg, LangIDs_About);

			// Additional translation for variable fields.
			SetDlgItemText(hwndDlg, IDC_PROGRAM, (GlobalTranslator.GetLine(LNG_ABOUT_PROGRAM) + L" " + PluginNameW).c_str());
			SetDlgItemText(hwndDlg, IDC_VERSION, (GlobalTranslator.GetLine(LNG_ABOUT_VERSION) + L" " + VersionStr).c_str());
			const wstring& str = GlobalTranslator.GetLine(LNG_ABOUT_AUTHOR);
			if (str == L"Разработчик:")
				SetDlgItemText(hwndDlg, IDC_AUTHOR, L"Разработчик: Власов Константин");
			else
				SetDlgItemText(hwndDlg, IDC_AUTHOR, (str + L" Konstantin Vlasov").c_str());

			// Create underlined font for the hyperlinks.
			LOGFONT lf;
			HFONT f = (HFONT)SendMessage(hwndDlg, WM_GETFONT, 0, 0);
			GetObject(f, sizeof(LOGFONT), &lf);
			lf.lfUnderline = 1;
			UnderlineFont = CreateFontIndirect(&lf);

			// Set this font for the hyperlink controls.
			SendMessage(GetDlgItem(hwndDlg, IDC_EMAIL), WM_SETFONT, (WPARAM)UnderlineFont, TRUE);
			SendMessage(GetDlgItem(hwndDlg, IDC_HOMEPAGE), WM_SETFONT, (WPARAM)UnderlineFont, TRUE);

			return TRUE;
		}
		break;
	case WM_DESTROY:
		{
			// When unloading, free the resources.
			DeleteObject(UnderlineFont);
			return TRUE;
		}
		break;
	case WM_CTLCOLORSTATIC:
		{
			// Draw hyperlinks with blue color.
			switch (GetDlgCtrlID((HWND)lParam))
			{
			case IDC_EMAIL:
			case IDC_HOMEPAGE:
				{
					HDC pDC = (HDC)wParam;
					SetTextColor(pDC, RGB(0, 0, 255));
					SetBkMode(pDC, TRANSPARENT);
					return (INT_PTR)GetStockObject(NULL_BRUSH);
				}
				break;
			}
		}
		break;
	case WM_SETCURSOR:
		{
			// Show the "hand" cursor over hyperlinks.
			switch (GetDlgCtrlID((HWND)wParam))
			{
			case IDC_EMAIL:
			case IDC_HOMEPAGE:
				SetCursor(HandCursor);
				SetWindowLong(hwndDlg, DWLP_MSGRESULT, TRUE);
				return TRUE;
			}
		}
		break;
	case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case IDOK:
			case IDCANCEL:
				// Close the dialog (IDCANCEL is sent when pressing Esc or [X] titlebar button).
				EndDialog(hwndDlg, IDOK);
				return TRUE;
			case IDC_EMAIL:
				// Hyperlink clicked - launch the URL.
				if (HIWORD(wParam) == STN_CLICKED)
					OpenURL(hwndDlg, L"mailto:support@flint-inc.ru");
				return TRUE;
			case IDC_HOMEPAGE:
				// Hyperlink clicked - launch the URL.
				if (HIWORD(wParam) == STN_CLICKED)
					OpenURL(hwndDlg, L"http://flint-inc.ru/");
				return TRUE;
			}
		}
		break;
	}
	return FALSE;
}

// Translation map for the Properties dialog.
static DWORD LangIDs_Properties[] = {
	IDC_GRP_STANDARD, LNG_PROPERTIES_STD_PARAMS,
	IDC_GRP_HIDDEN,   LNG_PROPERTIES_HIDDEN_PARAMS,
	IDOK,             LNG_PROPERTIES_OK,
	IDCANCEL,         LNG_PROPERTIES_CANCEL,
	0
};

INT_PTR CALLBACK PropertiesDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
		{
			UninstEntry* Entry = (UninstEntry*)lParam;

			// Center the dialog relative to the parent window.
			CenterDialog(hwndDlg);

			// Translate the dialog.
			GlobalTranslator.TranslateDialog(hwndDlg, LangIDs_Properties);

			// Additional translation for title with variable contents.
			SetWindowText(hwndDlg, (GlobalTranslator.GetLine(LNG_PROPERTIES_DLGTITLE) + L": " + Entry->m_DisplayName).c_str());

			// Special flag for handling registry redirection.
			DWORD PermsX64 = (SystemX64 ? (Entry->m_RegBranch->m_NativeBranch ? KEY_WOW64_64KEY : KEY_WOW64_32KEY) : 0);

			// Set edit boxes according to the entry data.
			HKEY hKey;
			if (RegOpenKeyEx(Entry->m_RegBranch->m_Root, (wstring(Entry->m_RegBranch->m_Path) + L"\\" + Entry->m_KeyName).c_str(), 0, KEY_SET_VALUE | KEY_QUERY_VALUE | PermsX64, &hKey) != ERROR_SUCCESS)
			{
				// Opening the key for writing failed, so we make the editboxes read-only.
				Edit_SetReadOnly(GetDlgItem(hwndDlg, IDC_DISPLAY_NAME), TRUE);
				Edit_SetReadOnly(GetDlgItem(hwndDlg, IDC_UNINSTALL_STRING), TRUE);
				Edit_SetReadOnly(GetDlgItem(hwndDlg, IDC_QUIET_DISPLAY_NAME), TRUE);
				Edit_SetReadOnly(GetDlgItem(hwndDlg, IDC_QUIET_UNINSTALL_STRING), TRUE);

				// Also disable the OK button - there is nothing to confirm.
				Button_Enable(GetDlgItem(hwndDlg, IDOK), FALSE);

				if (RegOpenKeyEx(Entry->m_RegBranch->m_Root, (wstring(Entry->m_RegBranch->m_Path) + L"\\" + Entry->m_KeyName).c_str(), 0, KEY_QUERY_VALUE | PermsX64, &hKey) != ERROR_SUCCESS)
				{
					// Registry key could be opened at all, make the editboxes disabled and print error messages.
					Edit_Enable(GetDlgItem(hwndDlg, IDC_DISPLAY_NAME), FALSE);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_UNINSTALL_STRING), FALSE);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_QUIET_DISPLAY_NAME), FALSE);
					Edit_Enable(GetDlgItem(hwndDlg, IDC_QUIET_UNINSTALL_STRING), FALSE);

					const WCHAR* msg = GlobalTranslator.GetLine(LNG_PROPERTIES_NO_ENTRY).c_str();
					Edit_SetText(GetDlgItem(hwndDlg, IDC_DISPLAY_NAME), msg);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_UNINSTALL_STRING), msg);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_QUIET_DISPLAY_NAME), msg);
					Edit_SetText(GetDlgItem(hwndDlg, IDC_QUIET_UNINSTALL_STRING), msg);
					hKey = NULL;
				}
			}
			// Keep the registry key opened and store it in the window data for future use.
			SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (LONG_PTR)hKey);
			if (hKey != NULL)
			{
				// Read and fill in the actual values.
				WCHAR* TempBuf = new WCHAR[LARGE_BUF_SZ];
				DWORD TempBufSz;
				TempBufSz = LARGE_BUF_SZ;
				if (RegQueryValueEx(hKey, L"DisplayName", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) == ERROR_SUCCESS)
					Edit_SetText(GetDlgItem(hwndDlg, IDC_DISPLAY_NAME), TempBuf);
				TempBufSz = LARGE_BUF_SZ;
				if (RegQueryValueEx(hKey, L"UninstallString", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) == ERROR_SUCCESS)
					Edit_SetText(GetDlgItem(hwndDlg, IDC_UNINSTALL_STRING), TempBuf);
				TempBufSz = LARGE_BUF_SZ;
				if (RegQueryValueEx(hKey, L"QuietDisplayName", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) == ERROR_SUCCESS)
					Edit_SetText(GetDlgItem(hwndDlg, IDC_QUIET_DISPLAY_NAME), TempBuf);
				TempBufSz = LARGE_BUF_SZ;
				if (RegQueryValueEx(hKey, L"QuietUninstallString", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) == ERROR_SUCCESS)
					Edit_SetText(GetDlgItem(hwndDlg, IDC_QUIET_UNINSTALL_STRING), TempBuf);
				delete[] TempBuf;
			}

			return TRUE;
		}
		break;
	case WM_DESTROY:
		{
			// Free the resources.
			HKEY hKey = (HKEY)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);
			if (hKey != NULL)
				RegCloseKey(hKey);
			return TRUE;
		}
		break;
	case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case IDOK:
				{
					// Store the new data.
					HKEY hKey = (HKEY)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);
					if (hKey != NULL)
					{
						WCHAR* TempBuf = new WCHAR[LARGE_BUF_SZ];
						bool res = true;
						int len;
						len = Edit_GetText(GetDlgItem(hwndDlg, IDC_DISPLAY_NAME), TempBuf, LARGE_BUF_SZ);
						res = RegUpdateValue(hKey, L"DisplayName", TempBuf, len) && res;
						len = Edit_GetText(GetDlgItem(hwndDlg, IDC_UNINSTALL_STRING), TempBuf, LARGE_BUF_SZ);
						res = RegUpdateValue(hKey, L"UninstallString", TempBuf, len) && res;
						len = Edit_GetText(GetDlgItem(hwndDlg, IDC_QUIET_DISPLAY_NAME), TempBuf, LARGE_BUF_SZ);
						res = RegUpdateValue(hKey, L"QuietDisplayName", TempBuf, len) && res;
						len = Edit_GetText(GetDlgItem(hwndDlg, IDC_QUIET_UNINSTALL_STRING), TempBuf, LARGE_BUF_SZ);
						res = RegUpdateValue(hKey, L"QuietUninstallString", TempBuf, len) && res;
						delete[] TempBuf;

						// Report the error if necessary.
						if (!res)
							MessageBox(hwndDlg, GlobalTranslator.GetLine(LNG_MSG_FAILED_TO_WRITE_INI).c_str(), PluginNameW, MB_ICONERROR | MB_OK);
					}

					// Close the dialog.
					EndDialog(hwndDlg, IDOK);
					return TRUE;
				}
			case IDCANCEL:
				{
					// Close the dialog (IDCANCEL is also sent when pressing Esc or [X] titlebar button).
					EndDialog(hwndDlg, IDCANCEL);
					return TRUE;
				}
			}
		}
		break;
	}
	return FALSE;
}
