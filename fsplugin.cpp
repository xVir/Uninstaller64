#include "stdafx.h"
#include "resource.h"
#include "fsplugin.h"
#include "UninstEntries.h"
#include "Globals.h"
#include "PluginOptions.h"
#include "Translator.h"
#include "CSWrapper.h"


// Translates the data from uninstallation entry into FindFirst/FindNext structure.
void FillFindData(const UninstEntry* entry, WIN32_FIND_DATAW* FindData)
{
	// TODO: Ask users whether show size in the standard TC Size column, or in custom field only.
	// Reason: When Size column is used, and a bunch of uninstallation entries is copied, TC updates
	// the total progress bar according to these sizes which is irrelevant, because it takes the same
	// time to copy each entry, no matter what size it displays.
	memset(FindData, 0, sizeof(WIN32_FIND_DATAW));
	wcscpy_len(FindData->cFileName, entry->m_DisplayName);
	FindData->ftLastWriteTime = entry->m_Timestamp;
	FindData->dwFileAttributes = (entry->m_Hidden ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL);
	FindData->nFileSizeLow = entry->m_Size & 0xffffffff;
	FindData->nFileSizeHigh = entry->m_Size >> 32;
}


// Fake functions to make TC load the plugin.
int __stdcall FsInit(int PluginNr, tProgressProc pProgressProc, tLogProc pLogProc, tRequestProc pRequestProc)
{
	UNUSED(PluginNr);
	UNUSED(pProgressProc);
	UNUSED(pLogProc);
	UNUSED(pRequestProc);

	return 0;
}

HANDLE __stdcall FsFindFirst(char* Path, WIN32_FIND_DATA* FindData)
{
	UNUSED(Path);
	UNUSED(FindData);

	return NULL;
}

BOOL __stdcall FsFindNext(HANDLE Hdl, WIN32_FIND_DATA* FindData)
{
	UNUSED(Hdl);
	UNUSED(FindData);

	return FALSE;
}


// Global list of uninstallation entrires.
// It can be used for single-entry operations, but for listing a Duplicate() must be used.
UninstEntries GlobalUninstList;

// Plugin callback-specific data.
int PluginId;
tProgressProcW ProgressProcW;
tLogProcW      LogProcW;
tRequestProcW  RequestProcW;

int __stdcall FsInitW(int PluginNr, tProgressProcW pProgressProcW, tLogProcW pLogProcW, tRequestProcW pRequestProcW)
{
	PluginId = PluginNr;
	ProgressProcW = pProgressProcW;
	LogProcW = pLogProcW;
	RequestProcW = pRequestProcW;

	// Load global plugin options.
	GlobalOptions.Initialize();
	GlobalOptions.Read();

	// Load translation.
	GlobalTranslator.ReadLanguageFile(GlobalOptions.m_Language);

	// Initialize the list of registry branches and get the initial list of uninstallation entries.
	UninstRegBranchesList.Initialize();
	GlobalUninstList.RereadEntries();
	return 0;
}

void __stdcall FsGetDefRootName(char* DefRootName, int maxlen)
{
	strcpy_len(DefRootName, maxlen, PluginNameA);
}

HANDLE __stdcall FsFindFirstW(WCHAR* Path, WIN32_FIND_DATAW* FindData)
{
	if ((Path[0] == L'\\') && (Path[1] == L'\0'))
	{
		// Root directory. Creating a temporary copy of the list and return the first entry from it (if present).
		UninstEntries* data;
		{
			// Duplicating is called for a global object, so protect it from accessing
			// from different threads simultaneously.
			CriticalSectionLock CSLock;

			data = GlobalUninstList.Duplicate();
		}

		const UninstEntry* entry = data->GetNextEntry(GlobalOptions.m_ShowHiddenEntries, GlobalOptions.m_ShowHotfixes);
		if (entry == NULL)
		{
			delete data;
			SetLastError(ERROR_NO_MORE_FILES);
			return INVALID_HANDLE_VALUE;
		}
		else
		{
			FillFindData(entry, FindData);
			// The list itself works as a handle.
			return (HANDLE)data;
		}
	}
	else
	{
		// There are no subdirectories.
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
}

BOOL __stdcall FsFindNextW(HANDLE Hdl, WIN32_FIND_DATAW* FindData)
{
	// Return the subsequent entries from the list, until the end is reached.
	UninstEntries* data = (UninstEntries*)Hdl;
	const UninstEntry* entry = data->GetNextEntry(GlobalOptions.m_ShowHiddenEntries, GlobalOptions.m_ShowHotfixes);
	if (entry == NULL)
		return FALSE;
	else
	{
		FillFindData(entry, FindData);
		return TRUE;
	}
}

int __stdcall FsFindClose(HANDLE Hdl)
{
	// Delete the temporary list.
	delete (UninstEntries*)Hdl;
	return 0;
}

int __stdcall FsGetBackgroundFlags()
{
	return BG_DOWNLOAD;
}

int __stdcall FsExecuteFileW(HWND MainWin, WCHAR* RemoteName, WCHAR* Verb)
{
	// We are running in foreground and perform no writing (except for 4 parameters from the Properties dialog which don't matter),
	// so there is no need to protect the function with critical section. Even more, protecting it would block background icons retrieving.

	if (wcscmp(Verb, L"open") == 0)
	{
		// Double-click or Enter pressed: start uninstallation.
		const UninstEntry* Entry = GlobalUninstList.GetEntryByDisplayName(RemoteName);
		if (Entry != NULL)
		{
			if ((Entry->m_UninstallString != NULL) && (Entry->m_UninstallString[0] != L'\0'))
			{
				if ((GlobalOptions.m_RequestUninstallationLaunch || (Entry->m_Hidden && GlobalOptions.m_RequestHiddenUninstallationLaunch)) &&
					!RequestProcW(PluginId, RT_MsgOKCancel, PluginNameW, GlobalTranslator.GetLine(Entry->m_Hidden ? LNG_MSG_CONFIRM_UNINSTALL_HIDDEN : LNG_MSG_CONFIRM_UNINSTALL).c_str(), NULL, 0))
					return FS_EXEC_OK;

				// Split command line to file path and arguments.
				wstring File = L"", Params = L"";
				const WCHAR* pos;

				if ((_wcsnicmp(Entry->m_UninstallString, L"msiexec", 7) == 0) || (_wcsnicmp(Entry->m_UninstallString, L"rundll32", 8) == 0))
				{
					// Hard-coded support for MSI and RunDLL32
					pos = wcschr(Entry->m_UninstallString, L' ');
					if (pos == NULL)
						File.assign(Entry->m_UninstallString);
					else
					{
						File.assign(Entry->m_UninstallString, pos);
						Params.assign(pos + 1);
					}
				}
				else if (Entry->m_UninstallString[0] == L'"')
				{
					// Path is enclosed in quotes - just find the closing quote, and we have the path/arguments.
					pos = wcschr(Entry->m_UninstallString + 1, L'"');
					if (pos == NULL)
						File.assign(Entry->m_UninstallString + 1);
					else
					{
						File.assign(Entry->m_UninstallString + 1, pos);
						Params.assign(pos + 1);
					}
				}
				else
				{
					// No quotes, have to guess which of the space characters is a delimiter between file path
					// and arguments. Just try them all until we find an existing file.
					pos = Entry->m_UninstallString;
					while (*pos != L'\0')
					{
						pos = wcschr(pos, L' ');
						if (pos == NULL)
						{
							File.assign(Entry->m_UninstallString);
							break;
						}
						File.assign(Entry->m_UninstallString, pos);
						if (FileExists(File.c_str()))
						{
							Params.assign(pos + 1);
							break;
						}
						++pos;
					}
				}

				// Run the uninstallation.
				int res = (int)ShellExecute(NULL, NULL, File.c_str(), Params.c_str(), NULL, SW_SHOW);
				if (res < 32)
				{
					// Something went wrong.
					// TODO: Add the error code to the text (also to all the other error messages).
					// TODO: (Alternative; better) Transform error code into the text message.
					RequestProcW(PluginId, RT_MsgOK, PluginNameW, GlobalTranslator.GetLine(LNG_MSG_UNINSTALL_FAILED).c_str(), NULL, 0);
					return FS_EXEC_ERROR;
				}

				// Uninstallation (if completed) removed the entry (or several entries), so make TC reread the list.
				SendMessage(MainWin, WM_USER + 51, cm_RereadSource, 0);
				return FS_EXEC_OK;
			}
			else
				RequestProcW(PluginId, RT_MsgOK, PluginNameW, GlobalTranslator.GetLine(LNG_MSG_UNINSTALL_STRING_EMPTY).c_str(), NULL, 0);
		}
	}
	else if (wcscmp(Verb, L"properties") == 0)
	{
		// Alt+Enter pressed.
		if (wcscmp(RemoteName, L"\\") == 0)
		{
			// The plugin folder itself is selected: open general Settings dialog.
			DialogBox(PluginInstance, MAKEINTRESOURCE(IDD_SETTINGS), MainWin, SettingsDialogProc);
			return FS_EXEC_OK;
		}
		else
		{
			// An unstallation entry is selected: open its Properties dialog.
			UninstEntry* Entry = GlobalUninstList.GetEntryByDisplayName(RemoteName);
			if (Entry != NULL)
			{
				if (DialogBoxParam(PluginInstance, MAKEINTRESOURCE(IDD_ENTRY_PROPERTIES), MainWin, PropertiesDialogProc, (LPARAM)Entry) == IDOK)
				{
					// If OK was pressed the entry name might be changed, so make TC reread the list.
					SendMessage(MainWin, WM_USER + 51, cm_RereadSource, 0);
				}
				return FS_EXEC_OK;
			}
			else
				return FS_EXEC_ERROR;
		}
	}
	return FS_EXEC_ERROR;
}

int __stdcall FsGetFileW(WCHAR* RemoteName, WCHAR* LocalName, int CopyFlags, RemoteInfoStruct* ri)
{
	UNUSED(ri);

	// We are in background, protect from foreground thread changing the global list contents.
	CriticalSectionLock CSLock;

	// Get the specific entry.
	const UninstEntry* Entry = GlobalUninstList.GetEntryByDisplayName(RemoteName);
	if (Entry == NULL)
		return FS_FILE_NOTFOUND;

	// Check for file presence and overwriting option.
	if (FileExists(LocalName) && ((CopyFlags & FS_COPYFLAGS_OVERWRITE) == 0))
		return FS_FILE_EXISTS;

	// Dump the information into the file.
	if (ProgressProcW(PluginId, RemoteName, LocalName, 0))
		return FS_FILE_USERABORT;
	int res = Entry->WriteFormattedData(LocalName) ? FS_FILE_OK : FS_FILE_WRITEERROR;
	if (ProgressProcW(PluginId, RemoteName, LocalName, 100))
		return FS_FILE_USERABORT;
	return res;
}

BOOL __stdcall FsDeleteFileW(WCHAR* RemoteName)
{
	// Protect from accessing the list item from background.
	CriticalSectionLock CSLock;

	// TODO: Override TC's dialog and display better message in case of errors.
	// TODO: Would be nice to request elevation if not enough permissions.
	return GlobalUninstList.DeleteEntryByDisplayName(RemoteName);
}

int __stdcall FsExtractCustomIconW(WCHAR* RemoteName, int ExtractFlags, HICON* TheIcon)
{
	// Loading of icons is a slow process, working only in background.
	if ((ExtractFlags & FS_ICONFLAG_BACKGROUND) == 0)
		return FS_ICON_DELAYED;

	// No special icon for [..]
	if (wcscmp(RemoteName, L"\\..\\") == 0)
		return FS_ICON_USEDEFAULT;

	// For other entries try to load the specific icon.
	int res = -1;
	{
		// We are in background, protect from foreground thread changing the global list contents.
		CriticalSectionLock CSLock;

		const UninstEntry* Entry = GlobalUninstList.GetEntryByDisplayName(RemoteName);
		if (Entry != NULL)
		{
			if (!GlobalOptions.m_ShowHiddenEntryIcons && Entry->m_Hidden)
				res = FS_ICON_USEDEFAULT;
			else
			{
				*TheIcon = Entry->GetEntryIcon((ExtractFlags & FS_ICONFLAG_SMALL) != 0);
				if (*TheIcon != NULL)
					res = FS_ICON_EXTRACTED_DESTROY;
			}
		}
	}

	if (res != -1)
		return res;

	// No icon was found, display our own default icon.
	*TheIcon = (((ExtractFlags & FS_ICONFLAG_SMALL) == 0) ? DefaultIconLarge : DefaultIconSmall);
	wcscpy_len(RemoteName, MAX_PATH, L"DefaultIcon");   // Make TC reuse this default icon to preserve resources.
	return FS_ICON_EXTRACTED;
}


// Names of the plugin fields
static const char* FieldNames[] = {
	"Registry Key",
	"Uninstall Command",
	"Hotfix"
};

enum FiledIndices {
	F_REGISTRY_KEY      = 0,
	F_UNINSTALL_COMMAND = 1,
	F_HOTFIX            = 2
};

static const char* RegKeyVariants = "Full path|Root|Path|Key name";

int __stdcall FsContentGetSupportedField(int FieldIndex, char* FieldName, char* Units, int maxlen)
{
	if ((FieldIndex < 0) || (FieldIndex >= SIZEOF_ARRAY(FieldNames)))
		return ft_nomorefields;

	strcpy_len(FieldName, maxlen, FieldNames[FieldIndex]);
	Units[0] = '\0';

	switch (FieldIndex)
	{
	case F_REGISTRY_KEY:                // Registry Key
		strcpy_len(Units, maxlen, RegKeyVariants);
		return ft_stringw;
	case F_UNINSTALL_COMMAND:           // Uninstall Command
		return ft_stringw;
	case F_HOTFIX:                      // Hotfix
		return ft_boolean;
	default:
		return ft_nomorefields;
	}
}

int __stdcall FsContentGetValueW(WCHAR* FileName, int FieldIndex, int UnitIndex, void* FieldValue, int maxlen, int flags)
{
	UNUSED(flags);

	// There are no slow fields for now, but they may appear in the future, so add race condition protection in advance.
	CriticalSectionLock CSLock;

	const UninstEntry* Entry = GlobalUninstList.GetEntryByDisplayName(FileName);
	if (Entry == NULL)
		return ft_fileerror;

	switch (FieldIndex)
	{
	case F_REGISTRY_KEY:                // Registry Key
		{
			size_t len = 0;
			size_t max_len = maxlen / sizeof(WCHAR);
			WCHAR* dst = (WCHAR*)FieldValue;

			// Fill in the field contents sequentially: if full path is requested concatenate all parts together,
			// otherwise only the specific requested part will be copied.
			// The len variable contains the current length of the copied text (in characters).

			// First is the registry root.
			if ((UnitIndex == 0) || (UnitIndex == 1))
			{
				if (Entry->m_RegBranch->m_Root == HKEY_LOCAL_MACHINE)
					len = wcscpy_len(dst, max_len, L"HKLM");
				else if (Entry->m_RegBranch->m_Root == HKEY_CURRENT_USER)
					len = wcscpy_len(dst, max_len, L"HKCU");
			}
			// If full path is requested append the backslash delimiter.
			if (UnitIndex == 0)
				len += wcscpy_len(dst + len, max_len - len, L"\\");

			// Second is the path to the specific Uninstall branch.
			// At the moment it is almost always the same (except for WOW64), but who knows of the future...
			if ((UnitIndex == 0) || (UnitIndex == 2))
			{
				// rest is what's left of the path to append.
				const WCHAR* rest = Entry->m_RegBranch->m_Path;
				if (SystemX64 && !Entry->m_RegBranch->m_NativeBranch)
				{
					// Adding Wow6432Node to HKLM(WOW64) path.
					if (_wcsnicmp(Entry->m_RegBranch->m_Path, L"Software\\", 9) == 0)
					{
						len += wcscpy_len(dst + len, max_len - len, L"Software\\Wow6432Node\\");
						rest += 9;
					}
				}

				len += wcscpy_len(dst + len, max_len - len, rest);
			}
			if (UnitIndex == 0)
				len += wcscpy_len(dst + len, max_len - len, L"\\");

			// Third, the name of the specific Uninstall subkey.
			if ((UnitIndex == 0) || (UnitIndex == 3))
				len += wcscpy_len(dst + len, max_len - len, Entry->m_KeyName);
			if (UnitIndex == 0)
				len += wcscpy_len(dst + len, max_len - len, L"\\");
		}
		return ft_stringw;
	case F_UNINSTALL_COMMAND:           // Uninstall Command
		{
			wcscpy_len((WCHAR*)FieldValue, maxlen / sizeof(WCHAR), Entry->m_UninstallString);
		}
		return ft_stringw;
	case F_HOTFIX:                      // Hotfix
		{
			*((BOOL*)FieldValue) = (Entry->m_IsHotfix ? TRUE : FALSE);
		}
		return ft_boolean;
	default:
		return ft_nosuchfield;
	}
}

int __stdcall FsContentGetDefaultSortOrder(int FieldIndex)
{
	switch (FieldIndex)
	{
	case F_REGISTRY_KEY:                // Registry Key
	case F_UNINSTALL_COMMAND:           // Uninstall Command
	case F_HOTFIX:                      // Hotfix
		return 1;
	default:
		return 1;
	}
}

/*
int __stdcall FsContentGetSupportedFieldFlags(int FieldIndex)
{
	UNUSED(FieldIndex);

	// TODO: Implement editing fields.
	// TODO: Find out what advantages contflags_subst* provide (should the plugin return size as now, or get a custom field for it with contflags_substsize flag).
	return 0;
}

int __stdcall FsContentSetValueW(WCHAR* FileName, int FieldIndex, int UnitIndex, int FieldType, void* FieldValue, int flags)
{
	UNUSED(FileName);
	UNUSED(FieldIndex);
	UNUSED(UnitIndex);
	UNUSED(FieldType);
	UNUSED(FieldValue);
	UNUSED(flags);

	// TODO: Implement editing fields.
	return ft_fileerror;
}

BOOL __stdcall FsContentGetDefaultViewW(WCHAR* ViewContents, WCHAR* ViewHeaders, WCHAR* ViewWidths, WCHAR* ViewOptions, int maxlen)
{
	UNUSED(ViewContents);
	UNUSED(ViewHeaders);
	UNUSED(ViewWidths);
	UNUSED(ViewOptions);
	UNUSED(maxlen);

	// TODO: Think over the default scheme.
	return FALSE;
}
*/
