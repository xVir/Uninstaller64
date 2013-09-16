#include "stdafx.h"
#include "PluginOptions.h"
#include "Globals.h"

// TODO: Implement as singleton.
PluginOptions GlobalOptions;

PluginOptions::PluginOptions()
: m_ShowHiddenEntries(false)
, m_ShowHiddenEntryIcons(false)
, m_ShowHotfixes(false)
, m_RequestUninstallationLaunch(false)
, m_RequestHiddenUninstallationLaunch(true)
{
	m_Language[0] = L'\0';
}

void PluginOptions::Initialize()
{
	WCHAR tmp[MAX_PATH_EX];

	// Fill in the available INI locations, also searching for the currently used one.
	// Order is reversed so that the higher-priority location overrode the lower-priority ones.
	m_StorageLocation = OPTIONS_STORAGE_UNDEFINED;

	// Location of wincmd.ini.
	if (GetEnvironmentVariable(L"COMMANDER_INI", tmp, MAX_PATH_EX) > 0)
	{
		WCHAR* pos = wcsrchr(tmp, L'\\');
		if (pos != NULL)
		{
			m_AvailableLocations[OPTIONS_STORAGE_WINCMD_INI_DIR].assign(tmp, pos);
			m_AvailableLocations[OPTIONS_STORAGE_WINCMD_INI_DIR] = m_AvailableLocations[OPTIONS_STORAGE_WINCMD_INI_DIR] + L"\\" + PluginNameW + L".ini";
			if (GetPrivateProfileInt(L"Settings", L"UseThisINI", 0, m_AvailableLocations[OPTIONS_STORAGE_WINCMD_INI_DIR].c_str()))
				m_StorageLocation = OPTIONS_STORAGE_WINCMD_INI_DIR;
		}
	}

	// Location of Total Commander.
	if (GetEnvironmentVariable(L"COMMANDER_PATH", tmp, MAX_PATH_EX) > 0)
	{
		m_AvailableLocations[OPTIONS_STORAGE_TOTALCMD_DIR].assign(tmp);
		m_AvailableLocations[OPTIONS_STORAGE_TOTALCMD_DIR] = m_AvailableLocations[OPTIONS_STORAGE_TOTALCMD_DIR] + L"\\" + PluginNameW + L".ini";
		if (GetPrivateProfileInt(L"Settings", L"UseThisINI", 0, m_AvailableLocations[OPTIONS_STORAGE_TOTALCMD_DIR].c_str()))
			m_StorageLocation = OPTIONS_STORAGE_TOTALCMD_DIR;
	}

	// Location of the plugin itself.
	m_AvailableLocations[OPTIONS_STORAGE_PLUGIN_DIR] = PluginDirectory;
	m_AvailableLocations[OPTIONS_STORAGE_PLUGIN_DIR] = m_AvailableLocations[OPTIONS_STORAGE_PLUGIN_DIR] + L"\\" + PluginNameW + L".ini";
	if (GetPrivateProfileInt(L"Settings", L"UseThisINI", 0, m_AvailableLocations[OPTIONS_STORAGE_PLUGIN_DIR].c_str()))
		m_StorageLocation = OPTIONS_STORAGE_PLUGIN_DIR;

	// If no INI file was detected, using wincmd.ini location by default.
	if (m_StorageLocation == OPTIONS_STORAGE_UNDEFINED)
	{
		m_StorageLocation = OPTIONS_STORAGE_WINCMD_INI_DIR;
		WritePrivateProfileString(L"Settings", L"UseThisINI", L"1", m_AvailableLocations[OPTIONS_STORAGE_WINCMD_INI_DIR].c_str());
	}
}

void PluginOptions::CheckStorageLocation()
{
	// Enumerate all possible locations searching for that currently used.
	for (size_t i = 0; i < OPTIONS_STORAGE_NUM; ++i)
	{
		if (GetPrivateProfileInt(L"Settings", L"UseThisINI", 0, m_AvailableLocations[i].c_str()))
		{
			// Got ya!
			m_StorageLocation = (StorageLocation)i;
			return;
		}
	}

	// If no INI file was detected, using wincmd.ini location by default.
	m_StorageLocation = OPTIONS_STORAGE_WINCMD_INI_DIR;
	WritePrivateProfileString(L"Settings", L"UseThisINI", L"1", m_AvailableLocations[OPTIONS_STORAGE_WINCMD_INI_DIR].c_str());
}

bool PluginOptions::SetStorageLocation(StorageLocation NewStorageLocation)
{
	bool res = true;

	// Enumerate all possible locations to set the UseThisINI flag in the new INI and drop it in the others.
	for (int i = 0; i < OPTIONS_STORAGE_NUM; ++i)
	{
		if (i == NewStorageLocation)
		{
			if (WritePrivateProfileString(L"Settings", L"UseThisINI", L"1", m_AvailableLocations[i].c_str()))
				m_StorageLocation = NewStorageLocation;
			else
				res = false;
		}
		else
		{
			// Drop the flag only if it was set (to avoid creating unused INI file or getting error because of read-only permissions).
			if (GetPrivateProfileInt(L"Settings", L"UseThisINI", 0, m_AvailableLocations[i].c_str())
				&& !WritePrivateProfileString(L"Settings", L"UseThisINI", L"0", m_AvailableLocations[i].c_str()))
				res = false;
		}
	}

	if (res)
	{
		// Save current settings to the new location.
		res = Write();
	}
	return res;
}

bool PluginOptions::Read()
{
	const WCHAR* IniPath = m_AvailableLocations[m_StorageLocation].c_str();
	m_ShowHiddenEntries                 = (GetPrivateProfileInt(L"Settings", L"ShowHiddenEntries",                 0, IniPath) != 0);
	m_ShowHiddenEntryIcons              = (GetPrivateProfileInt(L"Settings", L"ShowHiddenEntryIcons",              0, IniPath) != 0);
	m_ShowHotfixes                      = (GetPrivateProfileInt(L"Settings", L"ShowHotfixes",                      0, IniPath) != 0);
	m_RequestUninstallationLaunch       = (GetPrivateProfileInt(L"Settings", L"RequestUninstallationLaunch",       0, IniPath) != 0);
	m_RequestHiddenUninstallationLaunch = (GetPrivateProfileInt(L"Settings", L"RequestHiddenUninstallationLaunch", 1, IniPath) != 0);
	GetPrivateProfileString(L"Settings", L"Language", L"", m_Language, MAX_PATH, IniPath);
	return true;
}

bool PluginOptions::Write()
{
	const WCHAR* IniPath = m_AvailableLocations[m_StorageLocation].c_str();
	bool res = true;
	res = WritePrivateProfileString(L"Settings", L"ShowHiddenEntries",                 (m_ShowHiddenEntries ? L"1" : L"0"),                 IniPath) && res;
	res = WritePrivateProfileString(L"Settings", L"ShowHiddenEntryIcons",              (m_ShowHiddenEntryIcons ? L"1" : L"0"),              IniPath) && res;
	res = WritePrivateProfileString(L"Settings", L"ShowHotfixes",                      (m_ShowHotfixes ? L"1" : L"0"),                      IniPath) && res;
	res = WritePrivateProfileString(L"Settings", L"RequestUninstallationLaunch",       (m_RequestUninstallationLaunch ? L"1" : L"0"),       IniPath) && res;
	res = WritePrivateProfileString(L"Settings", L"RequestHiddenUninstallationLaunch", (m_RequestHiddenUninstallationLaunch ? L"1" : L"0"), IniPath) && res;
	res = WritePrivateProfileString(L"Settings", L"Language",                           m_Language,                                         IniPath) && res;
	return res;
}
