#include "stdafx.h"
#include "UninstEntries.h"
#include "Globals.h"

//////////////////////////////////////////////////////////////////////////
// class UninstRegBranch

UninstRegBranch::UninstRegBranch(HKEY Root, const WCHAR* Path, bool NativeBranch, const WCHAR* Suffix)
: m_Root(Root)
, m_Path(Path)
, m_NativeBranch(NativeBranch)
, m_Suffix(Suffix)
{
	m_Timestamp.dwHighDateTime = m_Timestamp.dwLowDateTime = 0;
}


//////////////////////////////////////////////////////////////////////////
// class UninstRegBranches

// TODO: Implement as singleton.
UninstRegBranches UninstRegBranchesList;

UninstRegBranches::UninstRegBranches()
{
	m_UninstRegBranches.clear();
}

void UninstRegBranches::Initialize()
{
	m_UninstRegBranches.push_back(UninstRegBranch(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", true, L"L"));
	m_UninstRegBranches.push_back(UninstRegBranch(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", true, L"U"));
	if (SystemX64)
		m_UninstRegBranches.push_back(UninstRegBranch(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", false, L"W"));
}


//////////////////////////////////////////////////////////////////////////
// class UninstEntries

UninstEntries::UninstEntries()
: m_CurrentIndex(0)
{
	m_Entries.clear();
	m_EntryFromKeyName.clear();
}

UninstEntries::~UninstEntries()
{
	for (size_t i = 0; i < m_Entries.size(); ++i)
		delete m_Entries[i];
}

UninstEntries* UninstEntries::Duplicate()
{
	// Start with updating the current list of entries.
	RereadEntries();

	// Create and fill in the new object.
	UninstEntries* res = new UninstEntries;
	res->m_Entries.resize(m_Entries.size());
	for (size_t i = 0; i < m_Entries.size(); ++i)
		res->m_Entries[i] = ((m_Entries[i] == NULL) ? NULL : m_Entries[i]->Duplicate());
	res->m_EntryFromKeyName = m_EntryFromKeyName;

	return res;
}

bool UninstEntries::RereadEntries()
{
	// Mark all entries as deleted.
	for (size_t i = 0; i < m_Entries.size(); ++i)
	{
		if (m_Entries[i] != NULL)
			m_Entries[i]->m_Deleted = true;
	}

	// Enumerate registry branches and update entries from them.
	m_CurrentIndex = 0;
	bool res = true;
	for (size_t i = 0; i < UninstRegBranchesList.m_UninstRegBranches.size(); ++i)
		res = UpdateUninstLocation(UninstRegBranchesList.m_UninstRegBranches[i]) && res;

	// Delete orphaned entries (which existed earlier but now are no longer present in the registry).
	for (size_t i = 0; i < m_Entries.size(); ++i)
	{
		if ((m_Entries[i] != NULL) && m_Entries[i]->m_Deleted)
		{
			delete m_Entries[i];
			m_Entries[i] = NULL;
		}
	}
	return res;
}

bool UninstEntries::UpdateUninstLocation(UninstRegBranch& RegBranch)
{
	// Open the registry branches that will be needed.
	HKEY hKey, hKeyARPC, hKeyMSIL, hKeyMSIU, hKeyHF;
	DWORD PermsX64 = (SystemX64 ? (RegBranch.m_NativeBranch ? KEY_WOW64_64KEY : KEY_WOW64_32KEY) : 0);

	// Main branch with the list of uninstallation entries.
	if (RegOpenKeyEx(RegBranch.m_Root, RegBranch.m_Path, 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | PermsX64, &hKey) != ERROR_SUCCESS)
		return false;

	// Informational branches; failure to open is not critical (just some extra info will be unavailable).
	if (RegOpenKeyEx(RegBranch.m_Root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Management\\ARPCache", 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyARPC) != ERROR_SUCCESS)
		hKeyARPC = NULL;
	if (RegOpenKeyEx(HKEY_CLASSES_ROOT, L"Installer\\Products", 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyMSIL) != ERROR_SUCCESS)
		hKeyMSIL = NULL;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Installer\\Products", 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyMSIU) != ERROR_SUCCESS)
		hKeyMSIU = NULL;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\HotFix", 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyHF) != ERROR_SUCCESS)
		hKeyHF = NULL;

	// TODO: Cache listing using LastWriteTime.
	RegQueryInfoKey(hKey, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &RegBranch.m_Timestamp);

	DWORD idx = 0;
	WCHAR KeyName[BUF_SZ];
	DWORD KeyNameSz = BUF_SZ;
	WCHAR* TempBuf = new WCHAR[LARGE_BUF_SZ];
	DWORD TempBufSz;

	// Enumerating all the subkeys for updating/inserting their data into the list.
	while (RegEnumKeyEx(hKey, idx, KeyName, &KeyNameSz, NULL, NULL, 0, NULL) == ERROR_SUCCESS)
	{
		// Open the subkey that contains information about specific uninstallation entry.
		HKEY hKeyEntry;
		if (RegOpenKeyEx(hKey, KeyName, 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyEntry) == ERROR_SUCCESS)
		{
			// Check whether this key is already present in the list.
			wstring RegKeyName = wstring(RegBranch.m_Suffix) + KeyName;
			MapStringToIndex::const_iterator CurrentEntryIter = m_EntryFromKeyName.find(RegKeyName);
			UninstEntry* CurrentEntry;
			size_t CurrentEntryIdx;
			if ((CurrentEntryIter == m_EntryFromKeyName.end()) || (m_Entries[CurrentEntryIter->second] == NULL))
			{
				// The key is not present: create a new entry and add it to the end of the list.
				CurrentEntry = new UninstEntry;
				CurrentEntry->m_RegBranch = &RegBranch;
				wcscpy_len(CurrentEntry->m_KeyName, KeyName);
				CurrentEntryIdx = m_Entries.size();
				m_Entries.push_back(CurrentEntry);
				m_EntryFromKeyName[RegKeyName] = CurrentEntryIdx;
			}
			else
			{
				// The key is present, fetch it and mark undeleted.
				CurrentEntryIdx = CurrentEntryIter->second;
				CurrentEntry = m_Entries[CurrentEntryIdx];
				CurrentEntry->m_Deleted = false;
				// We will recalculate some of the stored parameters, so reset them.
				CurrentEntry->m_Hidden = false;
				CurrentEntry->m_IsHotfix = false;
				CurrentEntry->m_Size = -2;
				CurrentEntry->m_KeyNameMSI[0] = L'\0';
			}

			// Get [Quiet]DisplayName.
			size_t len = 0;
			TempBufSz = LARGE_BUF_SZ;
			if ((RegQueryValueEx(hKeyEntry, L"DisplayName", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) != ERROR_SUCCESS) || (TempBuf[0] == L'\0'))
			{
				// DisplayName is not specified - mark the entry as hidden and try QuietDisplayName.
				CurrentEntry->m_Hidden = true;
				if ((RegQueryValueEx(hKeyEntry, L"QuietDisplayName", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) != ERROR_SUCCESS) || (TempBuf[0] == L'\0'))
				{
					// QuietDisplayName is not there either - use KeyName as a substitute.
					len = wcscpy_len(TempBuf, LARGE_BUF_SZ, KeyName);
				}
			}
			// Recalculate real length of the string to protect from possible zero-chars within.
			if (len == 0)
				len = wcslen(TempBuf);

			// Append the suffix to the display name.
			len += swprintf_s(TempBuf + len, LARGE_BUF_SZ - len, L".%s%04X", RegBranch.m_Suffix, CurrentEntryIdx);
			if ((CurrentEntry->m_DisplayName == NULL) || (wcscmp(TempBuf, CurrentEntry->m_DisplayName) != 0))
			{
				// If the resultant display name is different from the stored one, update it.
				delete[] CurrentEntry->m_DisplayName;
				CurrentEntry->m_DisplayName = new WCHAR[len + 1];
				wcscpy_len(CurrentEntry->m_DisplayName, len + 1, TempBuf);
			}

			// Get [Quiet]UninstallString.
			TempBufSz = LARGE_BUF_SZ;
			if ((RegQueryValueEx(hKeyEntry, L"UninstallString", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) != ERROR_SUCCESS) || (TempBuf[0] == L'\0'))
			{
				// UninstallString is not specified - mark the entry as hidden and try QuietUninstallString.
				CurrentEntry->m_Hidden = true;
				if ((RegQueryValueEx(hKeyEntry, L"QuietUninstallString", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) != ERROR_SUCCESS) || (TempBuf[0] == L'\0'))
				{
					// QuietDisplayName is not there either, there's nothing we can do.
					TempBufSz = (DWORD)-1;
				}
			}

			if (TempBufSz != (DWORD)-1)
			{
				// One of the UninstallString's was found, check it.
				if ((CurrentEntry->m_UninstallString == NULL) || (wcscmp(TempBuf, CurrentEntry->m_UninstallString) != 0))
				{
					// If the resultant UninstallString is different from the stored one, update it.
					delete[] CurrentEntry->m_UninstallString;
					CurrentEntry->m_UninstallString = new WCHAR[TempBufSz];
					wcscpy_len(CurrentEntry->m_UninstallString, TempBufSz, TempBuf);
				}
			}
			else
			{
				// If UninstallString is missing at all, clear the previously stored one (if it was).
				delete[] CurrentEntry->m_UninstallString;
				CurrentEntry->m_UninstallString = NULL;
			}

			// The last check for the Hidden flag: name is a GUID, and WindowsInstaller and SystemComponent are both set to 1.
			DWORD val;
			DWORD valSz = sizeof(DWORD);
			if (CurrentEntry->IsNameGUID())
			{
				if ((RegQueryValueEx(hKeyEntry, L"WindowsInstaller", NULL, NULL, (LPBYTE)&val, &valSz) == ERROR_SUCCESS) && (val == 1))
				{
					// This is a Windows Installer entry. Additional information may be present in one of the "Installer\Products" branches.
					// Key name there is a concatenation of each GUID group in reversed form.
					// GUID key:                     {ABCDEFGH-IJKL-MNOP-QRST-UVWXYZ012345}
					// Internal GUID representation: {ABCDEFGH-IJKL-MNOP-QR-ST-UV-WX-YZ-01-23-45}
					// MSI key:                      HGFEDCBALKJIPONMRQTSVUXWZY103254
					// Form the MSI key by fetching selected characters from the GUID string by their indices.
					const size_t GUID_to_MSI[32] = { 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x0d, 0x0c, 0x0b, 0x0a, 0x12, 0x11, 0x10, 0x0f, 0x15, 0x14, 0x17, 0x16, 0x1a, 0x19, 0x1c, 0x1b, 0x1e, 0x1d, 0x20, 0x1f, 0x22, 0x21, 0x24, 0x23 };
					for (size_t i = 0; i < 32; ++i)
						CurrentEntry->m_KeyNameMSI[i] = KeyName[GUID_to_MSI[i]];
					CurrentEntry->m_KeyNameMSI[32] = L'\0';

				    if ((RegQueryValueEx(hKeyEntry, L"SystemComponent", NULL, NULL, (LPBYTE)&val, &valSz) == ERROR_SUCCESS) && (val == 1))
						CurrentEntry->m_Hidden = true;
				}
			}

			// Get the timestamp of the key which corresponds to the timestamp of installation.
			if (RegQueryInfoKey(hKeyEntry, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &CurrentEntry->m_Timestamp) != ERROR_SUCCESS)
				memset(&CurrentEntry->m_Timestamp, 0, sizeof(FILETIME));

			// Get the icon.
			if ((CurrentEntry->m_KeyNameMSI[0] != L'\0') && ((hKeyMSIL != NULL) || (hKeyMSIU != NULL)))
			{
				HKEY hKeyMSIEntry;
				if ((RegOpenKeyEx(hKeyMSIL, CurrentEntry->m_KeyNameMSI, 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyMSIEntry) == ERROR_SUCCESS) ||
				    (RegOpenKeyEx(hKeyMSIU, CurrentEntry->m_KeyNameMSI, 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyMSIEntry) == ERROR_SUCCESS))
				{
					// Try to get the icon location.
					TempBufSz = LARGE_BUF_SZ;
					if (RegQueryValueEx(hKeyMSIEntry, L"ProductIcon", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) == ERROR_SUCCESS)
						CurrentEntry->AddIconLocation(TempBuf, true);
					RegCloseKey(hKeyMSIEntry);
				}
				else
				{
					// The corresponding MSI key is not present => Windows seems to hide such entries, so do we.
					CurrentEntry->m_Hidden = true;
				}
			}
			// Add non-MSI icon location to the search list.
			TempBufSz = LARGE_BUF_SZ;
			if (RegQueryValueEx(hKeyEntry, L"DisplayIcon", NULL, NULL, (LPBYTE)TempBuf, &TempBufSz) == ERROR_SUCCESS)
				CurrentEntry->AddIconLocation(TempBuf, true);

			// Get the size of the installed package (if stored).
			if (RegQueryValueEx(hKeyEntry, L"EstimatedSize", NULL, NULL, (LPBYTE)&val, &valSz) == ERROR_SUCCESS)
				CurrentEntry->m_Size = val * 1024;

			// Look into ARPCache for additional information (used in WinXP).
			if (hKeyARPC != NULL)
			{
				TempBufSz = 0;
				HKEY hKeyARPCEntry;
				if (RegOpenKeyEx(hKeyARPC, KeyName, 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyARPCEntry) == ERROR_SUCCESS)
				{
					// First get the size required to store the SlowInfoCache value.
					if (RegQueryValueEx(hKeyARPCEntry, L"SlowInfoCache", NULL, NULL, NULL, &TempBufSz) == ERROR_SUCCESS)
					{
						// Allocate the buffer, get the info and fill in the entry fields.
						CurrentEntry->m_SIC = (SlowInfoCache*)malloc(TempBufSz);
						if (RegQueryValueEx(hKeyARPCEntry, L"SlowInfoCache", NULL, NULL, (LPBYTE)CurrentEntry->m_SIC, &TempBufSz) == ERROR_SUCCESS)
						{
							// If estimated size is present and was not defined earlier by the EstimatedSize value, get it from ARPCache.
							if ((CurrentEntry->m_SIC->m_InstallSize >= 0) && (CurrentEntry->m_Size == -2))
								CurrentEntry->m_Size = CurrentEntry->m_SIC->m_InstallSize;
							// If there is a file name stored use it as an additional possible icon.
							if (CurrentEntry->m_SIC->m_HasName)
								CurrentEntry->AddIconLocation(CurrentEntry->m_SIC->m_Name, false);
						}
					}
					RegCloseKey(hKeyARPCEntry);
				}
			}

			// Check whether the product is a hotfix.
			if (hKeyHF != NULL)
			{
				HKEY hKeyHFEntry;
				if (RegOpenKeyEx(hKeyHF, KeyName, 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | PermsX64, &hKeyHFEntry) == ERROR_SUCCESS)
				{
					CurrentEntry->m_IsHotfix = true;
					RegCloseKey(hKeyHFEntry);
				}
			}

			// Finally, add the uninstallation command line as one more possible icon location
			// (in case nothing else worked).
			CurrentEntry->AddIconLocation(CurrentEntry->m_UninstallString, false);

			RegCloseKey(hKeyEntry);
		}
		++idx;
		KeyNameSz = BUF_SZ;
	}

	// Cleaning up.
	RegCloseKey(hKey);
	if (hKeyARPC != NULL)
		RegCloseKey(hKeyARPC);
	if (hKeyMSIL != NULL)
		RegCloseKey(hKeyMSIL);
	if (hKeyMSIU != NULL)
		RegCloseKey(hKeyMSIU);
	if (hKeyHF != NULL)
		RegCloseKey(hKeyHF);
	delete[] TempBuf;

	return true;
}

const UninstEntry* UninstEntries::GetNextEntry(bool AllowHidden, bool AllowHotfixes)
{
	const UninstEntry* tmp;
	while (m_CurrentIndex < m_Entries.size())
	{
		tmp = m_Entries[m_CurrentIndex++];
		// Skip empty entries.
		if (tmp == NULL)
			continue;
		// Skip hidden entries if needed.
		if (tmp->m_Hidden && !AllowHidden)
			continue;
		// Skip hotfixes if needed.
		if (tmp->m_IsHotfix && !AllowHotfixes)
			continue;
		return tmp;
	}
	// Reached the end: rotate to the beginning and return NULL
	m_CurrentIndex = 0;
	return NULL;
}

size_t UninstEntries::GetEntryIndexByDisplayName(const WCHAR* DisplayName) const
{
	if ((DisplayName == NULL) || (DisplayName[0] == L'\0'))
		return (size_t)-1;

	// Get the index from the extension.
	const WCHAR* pos = wcsrchr(DisplayName, L'.');
	if (pos == NULL)
		return (size_t)-1;
	++pos;
	if (*pos == L'\0')
		return (size_t)-1;
	++pos;
	return wcstoul(pos, NULL, 16);
}

UninstEntry* UninstEntries::GetEntryByDisplayName(const WCHAR* DisplayName) const
{
	size_t idx = GetEntryIndexByDisplayName(DisplayName);
	if (idx == (size_t)-1)
		return NULL;
	return m_Entries[idx];
}

bool UninstEntries::DeleteEntryByDisplayName(const WCHAR* DisplayName)
{
	size_t idx = GetEntryIndexByDisplayName(DisplayName);
	if (idx == -1)
		return false;

	const UninstEntry* Entry = m_Entries[idx];
	if (Entry == NULL)
		return false;

	DWORD PermsX64 = (SystemX64 ? (Entry->m_RegBranch->m_NativeBranch ? KEY_WOW64_64KEY : KEY_WOW64_32KEY) : 0);

	// Main uninstallation entry.
	if (!RegDeleteKeyRecursive(Entry->m_RegBranch->m_Root, Entry->m_RegBranch->m_Path, Entry->m_KeyName, PermsX64))
		return false;

	// Additional branches (if present).
	RegDeleteKeyRecursive(Entry->m_RegBranch->m_Root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Management\\ARPCache", Entry->m_KeyName, PermsX64);
	if (Entry->m_KeyNameMSI[0] != L'\0')
	{
		RegDeleteKeyRecursive(HKEY_CLASSES_ROOT, L"Installer\\Products", Entry->m_KeyNameMSI, PermsX64);
		RegDeleteKeyRecursive(HKEY_CURRENT_USER, L"Software\\Microsoft\\Installer\\Products", Entry->m_KeyNameMSI, PermsX64);
	}

	// Clean the stored entry.
	m_Entries[idx] = NULL;
	delete Entry;

	return true;
}
