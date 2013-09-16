#pragma once

//////////////////////////////////////////////////////////////////////////
// Constants

#define MAX_PATH_EX 1024

const wchar_t* const PluginNameW = L"Uninstaller64";
const char* const PluginNameA = "Uninstaller64";

const size_t BUF_SZ = MAX_PATH;
const size_t LARGE_BUF_SZ = 32767;

const DWORD cm_RereadSource = 540;


//////////////////////////////////////////////////////////////////////////
// Variables

// true if working on Windows x64, false otherwise (bitness of the current process does not matter).
extern bool SystemX64;

// Default icons for entries which don't have specific icon.
extern HICON DefaultIconSmall, DefaultIconLarge;

// HINSTANCE of the current DLL.
extern HINSTANCE PluginInstance;

// Directory where the plugin file is located.
extern WCHAR PluginDirectory[];

// Plugin version stored as string.
extern WCHAR VersionStr[];


//////////////////////////////////////////////////////////////////////////
// Macros

#define SIZEOF_ARRAY(ar) (sizeof(ar) / sizeof((ar)[0]))

#define UNUSED(x) x

//////////////////////////////////////////////////////////////////////////
// Functions

// wcscpy_len/strcpy_len: securely copies one string into another. Resultant string is always zero-terminated.
// Paramerets:
// 	[out] dst     - destination buffer.
// 	[in]  bufsize - destination buffer size in charactres.
// 	[in]  src     - source buffer.
// Return value:
// 	resultant length of the destination string.
size_t wcscpy_len(wchar_t* dst, size_t bufsize, const wchar_t* src);

template <size_t size>
size_t wcscpy_len(wchar_t (&dst)[size], const wchar_t* src)
{
	return wcscpy_len(dst, size, src);
}

size_t strcpy_len(char* dst, size_t bufsize, const char* src);

template <size_t size>
size_t strcpy_len(char (&dst)[size], const char* src)
{
	return strcpy_len(dst, size, src);
}

// Check whether file exists.
// Parameters:
//	[in] Path - full path to the file.
// Return value:
//	true if specific file exists and is not a directory; false otherwise.
bool FileExists(const wchar_t* Path);

// Delete registry key with all its subkeys.
bool RegDeleteKeyRecursive(HKEY hKey, const WCHAR* RegBranch, const WCHAR* KeyName, DWORD PermsX64);

// Window procedure for the About dialog box.
INT_PTR CALLBACK AboutDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Window procedure for the Settings dialog box.
INT_PTR CALLBACK SettingsDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Window procedure for the Properties dialog box.
INT_PTR CALLBACK PropertiesDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
