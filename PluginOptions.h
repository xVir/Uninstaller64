#pragma once

#include "Globals.h"


//////////////////////////////////////////////////////////////////////////
// Class for managing user-configurable options.

// Possible locations for storing the plugin's configuration file.
enum StorageLocation
{
	OPTIONS_STORAGE_UNDEFINED      = -1,
	OPTIONS_STORAGE_PLUGIN_DIR     =  0,
	OPTIONS_STORAGE_TOTALCMD_DIR   =  1,
	OPTIONS_STORAGE_WINCMD_INI_DIR =  2,
	OPTIONS_STORAGE_NUM            =  3
};

class PluginOptions
{
private:
	// List of paths to the available INI files (ordered by their priority).
	wstring m_AvailableLocations[OPTIONS_STORAGE_NUM];

public:
	// The options themselves:
	bool m_ShowHiddenEntries;                   // Whether hidden entries should be listed in TC panel.
	bool m_ShowHiddenEntryIcons;                // If hidden entries are shown, which icon they should have (default TC icon or entry-specific).
	bool m_ShowHotfixes;                        // Whether hotfixes should be listed in TC panel.
	bool m_RequestUninstallationLaunch;         // Whether the plugin displays additional request for launching non-hidden uninstallers.
	bool m_RequestHiddenUninstallationLaunch;   // Whether the plugin displays additional request for launching hidden uninstallers.
	wchar_t m_Language[MAX_PATH];               // Interface language.

	// Current INI location.
	StorageLocation m_StorageLocation;

	PluginOptions();

	// Initialization function (cannot be moved to constructor: it's called too early).
	void Initialize();

	// Determines the currently used location for storing options.
	void CheckStorageLocation();

	// Switches to the new location for the configuration file.
	bool SetStorageLocation(StorageLocation NewStorageLocation);

	// Load settings from the current location.
	bool Read();

	// Save settings to the current location.
	bool Write();
};

extern PluginOptions GlobalOptions;
