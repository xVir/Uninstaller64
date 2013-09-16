#pragma once

#include "UninstEntry.h"


//////////////////////////////////////////////////////////////////////////
// Class describing a single registry branch.

class UninstRegBranch
{
public:
	// Registry root (e.g. HKLM or HKCU).
	HKEY m_Root;

	// Path to the key containing subkeys of uninstallation entries.
	const WCHAR* m_Path;

	// Whether to use native branch (x64) or WOW64-redirected (x32); applicable for 64-bit Windows only.
	bool m_NativeBranch;

	// Suffix for display names (to ensure uniqueness of file entry names).
	// At the moment, it is required that all suffixes had length exactly 1 character! (see UninstEntries::m_EntryFromKeyName)
	const WCHAR* m_Suffix;

	// Last write timestamp (used for caching data). At the moment, unused.
	// Problem: Updating is performed individually by branch, but the list is combined.
	// Possible solutions:
	// 1) search & remove specific entries by their suffix => ineffective;
	// 2) first read & check timestamps for all branches, if one or more updated - clear & update all => architecture changes required.
	FILETIME m_Timestamp;

	// Constructs an object with the parameters specified (timestamp is set to zero).
	UninstRegBranch(HKEY Root, const WCHAR* Path, bool NativeBranch, const WCHAR* Suffix);
};


//////////////////////////////////////////////////////////////////////////
// Class for storing the list of registry branches with uninstallation
// entries.

class UninstRegBranches
{
public:
	// List of registry branches.
	vector<UninstRegBranch> m_UninstRegBranches;

	// Constructs an empty object.
	UninstRegBranches();

	// Initializes the list of registry branches specific to the current Windows version.
	// Cannot be called in constructor, because SystemX64 variable is initialized after global objects.
	void Initialize();
};

// Global list of registry branches.
extern UninstRegBranches UninstRegBranchesList;


//////////////////////////////////////////////////////////////////////////
// Class for storing collection of uninstallation entries and enumerating
// them.

class UninstEntries
{
private:
	// Current index for enumerating.
	size_t m_CurrentIndex;

	// List of all stored entries.
	vector<UninstEntry*> m_Entries;

	// Map for searching specific entry by its unique key name.
	// The unique key name is built as a concatenation of the registry branch suffix and the key name proper.
	typedef map<wstring, size_t> MapStringToIndex;
	MapStringToIndex m_EntryFromKeyName;

	// Finds the index of the specific entry by its display name.
	size_t GetEntryIndexByDisplayName(const WCHAR* DisplayName) const;

public:
	UninstEntries();
	~UninstEntries();

	// Updates the list of uninstallation entries and creates a fresh copy of the object
	// for thread-safe processing.
	UninstEntries* Duplicate();

	// Performs updating the list of entries from the registry.
	bool RereadEntries();

	// Performs updating the list of entries from the specific registry branch.
	bool UpdateUninstLocation(UninstRegBranch& RegBranch);

	// Returns the current entry and increments the internal index.
	// If AllowHidden is set to false, all hiddden entries are skipped.
	const UninstEntry* GetNextEntry(bool AllowHidden, bool AllowHotfixes);

	// Returns the entry from its display name.
	UninstEntry* GetEntryByDisplayName(const WCHAR* DisplayName) const;

	// Deletes the specified entry from the registry.
	bool DeleteEntryByDisplayName(const WCHAR* DisplayName);
};
