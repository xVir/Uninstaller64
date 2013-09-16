#pragma once

#include "Globals.h"

class UninstRegBranch;


#pragma pack(1)
// ARPCache::SlowInfoCache registry parameter internal composition.
struct SlowInfoCache
{
	DWORD m_Size;
	BOOL m_HasName;
	LONGLONG m_InstallSize;
	FILETIME m_LastUsed;
	DWORD m_Frequency;
	WCHAR m_Name[1];
};
#pragma pack()


//////////////////////////////////////////////////////////////////////////
// Class for storing information about single uninstallation entry.

class UninstEntry
{
private:
	// List of possible icon paths (each is path to the file and icon index).
	struct IconLocation
	{
		wstring m_Path;
		size_t m_Index;
	};
	vector<IconLocation*> m_IconLocations;

public:
	// Whether this entry is deleted from registry.
	bool m_Deleted;

	// Registry branch of the entry.
	UninstRegBranch* m_RegBranch;

	// Name of the registry key.
	wchar_t m_KeyName[BUF_SZ];

	// Additional key for MSI (empty string if installer is not MSI).
	wchar_t m_KeyNameMSI[33];

	// Display name: taken either from "DisplayName" parameter; if unspecified m_KeyName is copied.
	// To ensure uniqueness and avoid splitting part of name as extension, suffix is appended in form:
	// <.XHHHH> where HHHH is the entry index; X is either 'L' for HKLM, 'W' for HKLM/WOW64, or 'U' for HKCU.
	wchar_t* m_DisplayName;

	// Total size of the product, if present in ARPCashe.
	// Default value is set to -2 which shows empty column in TC.
	__int64 m_Size;

	// Whether the product is hidden. The term is derived from UnInstTC plugin and set to true when:
	// 1) DisplayName parameter is missing, or
	// 2) UnInstallString parameter is missing, or
	// 3) m_KeyName is a GUID, and WindowsInstaller == 1, and SystemComponent == 1.
	bool m_Hidden;

	// Whether the product is a hotfix.
	bool m_IsHotfix;

	// Last write timestamp: somewhat of an indication to when the product was installed.
	FILETIME m_Timestamp;

	// Uninstall string: taken from "UninstallString" parameter.
	wchar_t* m_UninstallString;

	// Information from the ARP Cache.
	SlowInfoCache* m_SIC;

	UninstEntry();
	~UninstEntry();

	// Creates a copy of the object, duplicating dynamic memory blocks.
	UninstEntry* Duplicate() const;

	// Checks whether the KeyName is a GUID.
	bool IsNameGUID() const;

	// Extracts the specific entry icon trying each of the stored icon locations.
	// Returns NULL if no icon could be extracted.
	HICON GetEntryIcon(bool ReqSmallIcon) const;

	// Adds a new icon location to the list.
	// str - source string to parse.
	// ParseIcon - true if str is an icon location ("path,index");
	//             false if str is an executable command line.
	void AddIconLocation(const wchar_t* str, bool ParseIcon);

	// Dumps information about the current entry into HTML file.
	bool WriteFormattedData(const WCHAR* FileName) const;
};
