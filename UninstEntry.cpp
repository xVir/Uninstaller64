#include "stdafx.h"
#include "Globals.h"
#include "Translator.h"
#include "UninstEntry.h"
#include "UninstEntries.h"

UninstEntry::UninstEntry()
: m_Deleted(false)
, m_DisplayName(NULL)
, m_Size(-2)
, m_Hidden(false)
, m_IsHotfix(false)
, m_UninstallString(NULL)
, m_SIC(NULL)
{
	m_KeyName[0] = L'\0';
	m_KeyNameMSI[0] = L'\0';
	memset(&m_Timestamp, 0, sizeof(FILETIME));
	m_IconLocations.clear();
	m_RegBranch = NULL;
}

UninstEntry::~UninstEntry()
{
	delete[] m_DisplayName;
	delete[] m_UninstallString;
	if (m_SIC)
		free(m_SIC);
	for (size_t i = 0; i < m_IconLocations.size(); ++i)
		delete m_IconLocations[i];
}

UninstEntry* UninstEntry::Duplicate() const
{
	// Create a new entry and copy all the current data into it.
	size_t len;
	UninstEntry* res = new UninstEntry;
	res->m_Deleted = m_Deleted;
	res->m_RegBranch = m_RegBranch;
	wcscpy_len(res->m_KeyName, m_KeyName);
	if (m_DisplayName != NULL)
	{
		len = wcslen(m_DisplayName) + 1;
		res->m_DisplayName = new wchar_t[len];
		wcscpy_len(res->m_DisplayName, len, m_DisplayName);
	}
	res->m_Size = m_Size;
	res->m_Hidden = m_Hidden;
	res->m_IsHotfix = m_IsHotfix;
	res->m_Timestamp = m_Timestamp;
	if (m_UninstallString != NULL)
	{
		len = wcslen(m_UninstallString) + 1;
		res->m_UninstallString = new wchar_t[len];
		wcscpy_len(res->m_UninstallString, len, m_UninstallString);
	}
	if (m_SIC != NULL)
	{
		res->m_SIC = (SlowInfoCache*)malloc(m_SIC->m_Size);
		memcpy(res->m_SIC, m_SIC, m_SIC->m_Size);
	}
	res->m_IconLocations.resize(m_IconLocations.size());
	for (size_t i = 0; i < m_IconLocations.size(); ++i)
	{
		res->m_IconLocations[i] = new IconLocation;
		*(res->m_IconLocations[i]) = *(m_IconLocations[i]);
	}
	return res;
}

bool UninstEntry::IsNameGUID() const
{
	// Scheme of GUIDs. 'X' is a hex digit, other characters are literals.
	const wchar_t* GUID_scheme = L"{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}";

	// Look for divergence from the pattern. Stop when any of the strings ends.
	size_t pos = 0;
	while ((m_KeyName[pos] != L'\0') && (GUID_scheme[pos] != L'\0'))
	{
		if (GUID_scheme[pos] == L'X')
		{
			if (((m_KeyName[pos] < L'0') || (m_KeyName[pos] > L'9')) && ((m_KeyName[pos] < L'a') || (m_KeyName[pos] > L'f')) && ((m_KeyName[pos] < L'A') || (m_KeyName[pos] > L'F')))
				return false;
		}
		else
		{
			if (m_KeyName[pos] != GUID_scheme[pos])
				return false;
		}
		++pos;
	}

	// No divergence was found, the last check is whether the KeyName is not too short or too long.
	return ((m_KeyName[pos] == L'\0') && (GUID_scheme[pos] == L'\0'));
}

HICON UninstEntry::GetEntryIcon(bool ReqSmallIcon) const
{
	HICON IconSmall, IconLarge;
	for (size_t i = 0; i < m_IconLocations.size(); ++i)
	{
		// Return value of ExtractIconEx in case of errors is undocumented. Experimentally found that
		// it returns -1, so we treat positive values as valid (could be 1 or 2).
		if ((int)ExtractIconEx(m_IconLocations[i]->m_Path.c_str(), (int)m_IconLocations[i]->m_Index, &IconLarge, &IconSmall, 1) > 0)
			return ReqSmallIcon ? IconSmall : IconLarge;
	}
	return NULL;
}

void UninstEntry::AddIconLocation(const wchar_t* str, bool ParseIcon)
{
	if ((str == NULL) || (str[0] == L'\0'))
		return;

	IconLocation* tmp = new IconLocation;
	tmp->m_Index = 0;
	const wchar_t* pos;
	if (ParseIcon)
	{
		// Split str to path and icon index (if the latter is specified).
		pos = wcsrchr(str, L',');
		if (pos != NULL)
		{
			// Comma is present; get the index and the rest is the path.
			// Don't forget that path can be enclosed in quotes.
			tmp->m_Index = _wtoi(pos + 1);
			if ((str[0] == L'"') && (*(pos-1) == L'"'))
				tmp->m_Path.assign(str + 1, pos - 1);
			else
				tmp->m_Path.assign(str, pos);
		}
		else
		{
			// No comma, whole string is just a path.
			tmp->m_Path.assign(str + ((str[0] == L'"') ? 1 : 0));
			if (*(tmp->m_Path.end() - 1) == L'"')
				tmp->m_Path.resize(tmp->m_Path.size() - 1);
		}
	}
	else
	{
		// Parse command line and try to extract main executable.
		if (str[0] == L'"')
		{
			// Path is enclosed in quotes - just find the closing quote, and we have the path.
			pos = wcschr(str + 1, L'"');
			if (pos != NULL)
				tmp->m_Path.assign(str + 1, pos);
			else
				tmp->m_Path.assign(str + 1);
		}
		else
		{
			// No quotes, have to guess which of the space characters is a delimiter between file path
			// and arguments. Just try them all until we find an existing file.
			pos = str;
			while (*pos != L'\0')
			{
				pos = wcschr(pos, L' ');
				if (pos == NULL)
				{
					tmp->m_Path.assign(str);
					break;
				}
				tmp->m_Path.assign(str, pos);
				if (FileExists(tmp->m_Path.c_str()))
					break;
				++pos;
			}
		}
	}
	m_IconLocations.push_back(tmp);
}

// Additional chain operator for printing UTF-16 string into ANSI output stream as UTF-8.
// Supported the width flag; alignment is always 'left'.
// Additionally, HTML escaping is performed.
// TODO: Add support for the alignment flag.
ostream& operator<<(ostream& os, const wchar_t* wstr)
{
/*
	// TODO: Measure performance and decide whether to use an intermediate buffer or not.
	// Sample code:
	const size_t TMPBUF_SZ = 1024;
	char buf[TMPBUF_SZ];
	size_t buf_idx = 0;
	for (size_t i = 0; i < BUF_SZ; ++i)
	{
		if (buf_idx == TMPBUF_SZ)
		{
			os.write(buf, TMPBUF_SZ);
			buf_idx = 0;
		}
		buf[buf_idx++] = c;
	}
	if (buf_idx > 0)
		os.write(buf, buf_idx - 1);
*/
	// half_pair is the first half of a surrogate pair.
	DWORD half_pair = 0;
	int cnt = 0;
	while (*wstr != L'\0')
	{
		wchar_t c = *wstr++;
		DWORD u32;
		// Transform into UTF-32.
		if (half_pair == 0)
		{
			if ((c >= 0xd800) && (c <= 0xdbff))
			{
				// First part of surrogate pair: store it and continue with the next wchar.
				half_pair = c;
				continue;
			}
			else if ((c >= 0xdc00) && (c <= 0xdfff))
			{
				// Invalid character, skipping.
				continue;
			}
			else
			{
				u32 = c;
			}
		}
		else
		{
			if ((c >= 0xdc00) && (c <= 0xdfff))
			{
				// Second part of surrogate pair: combine together with the first part.
				u32 = (((half_pair - 0xd800) << 10) | (c - 0xdc00)) + 0x10000;
				half_pair = 0;
			}
			else
			{
				// Invalid character, skipping.
				half_pair = 0;
				continue;
			}
		}

		// Now write the UTF-32 character into stream as UTF-8.
		if (u32 <= 0x7f)
		{
			// Escape special characters (they all have codes <0x7f).
			if (u32 == '"')
				os.write("&quot;", 6);
			else if (u32 == '&')
				os.write("&amp;", 5);
			else if (u32 == '<')
				os.write("&lt;", 4);
			else if (u32 == '>')
				os.write("&gt;", 4);
			else
				os.put((char)u32);
		}
		else if (u32 <= 0x7ff)
		{
			os.put((char)(0xc0 | (u32 >> 6)));
			os.put((char)(0x80 | (u32 & 0x3f)));
		}
		else if (u32 <= 0xffff)
		{
			os.put((char)(0xe0 | (u32 >> 12)));
			os.put((char)(0x80 | ((u32 >> 6) & 0x3f)));
			os.put((char)(0x80 | (u32 & 0x3f)));
		}
		else if (u32 <= 0x1fffff)
		{
			os.put((char)(0xf0 | (u32 >> 18)));
			os.put((char)(0x80 | ((u32 >> 12) & 0x3f)));
			os.put((char)(0x80 | ((u32 >> 6) & 0x3f)));
			os.put((char)(0x80 | (u32 & 0x3f)));
		}
		++cnt;
	}
	// Append with spaces if width flag is requested.
	for (int i = 0; i < os.width() - cnt; ++i)
		os.put(' ');
	os.width(0);
	return os;
}

bool UninstEntry::WriteFormattedData(const WCHAR* FileName) const
{
	// List of main registry parameters to dump.
	const WCHAR* StdParams[] =    {L"DisplayName",
	                               L"QuietDisplayName",
	                               L"DisplayVersion",
	                               L"Publisher",
	                               L"InstallLocation",
	                               L"UninstallString",
	                               L"QuietUninstallString",
	                               L"DisplayIcon",
	                               L"ModifyPath"};
	const WCHAR* StdParamsURL[] = {L"ReadMe",
	                               L"HelpLink",
	                               L"URLInfoAbout",
	                               L"URLUpdateInfo"};

	// Key and value names are case-insensitive so we need a name-value map with case-insensitive name comparison.
	struct wcs_comp_nocase : public binary_function<wstring, wstring, bool>
	{
		bool operator()(const wstring& _Left, const wstring& _Right) const
		{
			return _wcsicmp(_Left.c_str(), _Right.c_str()) < 0;
		}
	};

	// This map contains registry parameter name as key, and parameter value as value.
	// The value is HTML-ready, which means it's in UTF-8 encoding and with all necessary HTML entities escaped.
	typedef map<wstring, string, wcs_comp_nocase> MapStrStr;
	MapStrStr RegValues;

	const char* RegRootName = "";
	if (m_RegBranch->m_Root == HKEY_LOCAL_MACHINE)
		RegRootName = "HKLM";
	else if (m_RegBranch->m_Root == HKEY_CURRENT_USER)
		RegRootName = "HKCU";

	// Strip the extension.
	WCHAR* DisplayName = _wcsdup(m_DisplayName);
	WCHAR* pos = wcsrchr(DisplayName, L'.');
	if (pos != NULL)
		*pos = L'\0';

	HKEY hKeyEntry;
	wstring RegPath = m_RegBranch->m_Path;
	DWORD PermsX64 = (SystemX64 ? (m_RegBranch->m_NativeBranch ? KEY_WOW64_64KEY : KEY_WOW64_32KEY) : 0);
	if (RegOpenKeyEx(m_RegBranch->m_Root, (RegPath + L'\\' + m_KeyName).c_str(), 0, STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | PermsX64, &hKeyEntry) != ERROR_SUCCESS)
		return false;

	ofstream f;
	f.open(FileName, ios_base::out | ios_base::trunc);
	if (!f)
	{
		RegCloseKey(hKeyEntry);
		return false;
	}

	if (SystemX64 && !m_RegBranch->m_NativeBranch)
	{
		// Adding Wow6432Node to HKLM(WOW64) path. This is a bad idea to access a key using this node
		// (MSDN claims this is subject to change), but it's hard to visualize 64-32 redirection anyhow else.
		// So, we use this node only for visualizing. Actual work with registry is performed using KEY_WOW64_32KEY.
		if (_wcsnicmp(RegPath.c_str(), L"Software\\", 9) == 0)
			RegPath.insert(9, L"Wow6432Node\\");
	}

	// Dump the HTML header and initial information (application name and uninstallation registry path).
	f << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n\
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\" dir=\"ltr\">\n\
\n\
<head>\n\
  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n\
  <title>" << DisplayName << "</title>\n\
  <style type=\"text/css\">\n\
    body {\n\
      font-family: Courier New, monospace;\n\
      font-size: 75%;\n\
    }\n\
    h3 {\n\
      color: #006000;\n\
    }\n\
    h4 {\n\
      color: #600000;\n\
    }\n\
    table {\n\
      border: solid 1px gray;\n\
      border-collapse: collapse;\n\
      font-family: Courier New, monospace;\n\
      font-size: 100%;\n\
    }\n\
    td {\n\
      border: solid 1px gray;\n\
      padding: 2px 6px;\n\
	  vertical-align: top;\n\
    }\n\
  </style>\n\
</head>\n\
\n\
<body>\n\
<h3>" << DisplayName << "</h3>\n<p>" << GlobalTranslator.GetLine(LNG_INFO_REGISTRY_KEY).c_str() << " <b>" << RegRootName << '\\' << RegPath.c_str() << '\\' << m_KeyName << "</b></p>\n<table>\n";

	// Enumerate and store all available parameters.
	DWORD idx = 0;
	DWORD ValueType;
	WCHAR* TempBufName = new WCHAR[LARGE_BUF_SZ];
	BYTE* TempBufValue = new BYTE[LARGE_BUF_SZ];
	DWORD TempBufNameSz, TempBufValueSz;
	TempBufNameSz = TempBufValueSz = LARGE_BUF_SZ;
	while (RegEnumValue(hKeyEntry, idx, TempBufName, &TempBufNameSz, NULL, &ValueType, (LPBYTE)TempBufValue, &TempBufValueSz) == ERROR_SUCCESS)
	{
		switch (ValueType)
		{
		case REG_DWORD:
			{
				DWORD Value = *((DWORD*)TempBufValue);
				ostringstream s;
				s << "0x" << setw(8) << setfill('0') << setbase(16) << Value << " (" << setbase(10) << Value << ")";
				RegValues[TempBufName] = s.str();
			}
			break;
		case REG_QWORD:
			{
				ULONGLONG Value = *((ULONGLONG*)TempBufValue);
				ostringstream s;
				s << "0x" << setw(16) << setfill('0') << setbase(16) << Value << " (" << setbase(10) << Value << ")";
				RegValues[TempBufName] = s.str();
			}
			break;
		case REG_SZ:
		case REG_EXPAND_SZ:
			{
				ostringstream s;
				s << (const WCHAR*)TempBufValue;
				RegValues[TempBufName] = s.str();
			}
			break;
		case REG_MULTI_SZ:
			{
				ostringstream s;
				const WCHAR* str = (const WCHAR*)TempBufValue;
				while (*str != L'\0')
				{
					s << str << "<br/>";
					str += wcslen(str) + 1;
				}
				RegValues[TempBufName] = s.str();
			}
			break;
		default:
			{
				ostringstream s;
				s << setbase(16);
				for (DWORD i = 0; i < TempBufValueSz; ++i)
				{
					s << setw(2) << setfill('0') << TempBufValue[i] << " ";
				}
				RegValues[TempBufName] = s.str();
			}
		}
		++idx;
		TempBufNameSz = TempBufValueSz = LARGE_BUF_SZ;
	}

	// List and dump the main text keys.
	for (size_t i = 0; i < SIZEOF_ARRAY(StdParams); ++i)
	{
		MapStrStr::iterator item = RegValues.find(StdParams[i]);
		if (item != RegValues.end())
		{
			f << "<tr>\n  <td>" << item->first.c_str() << "</td>\n  <td>" << item->second.c_str() << "</td>\n</tr>\n";
			RegValues.erase(item);
		}
	}
	// List and dump the main URL keys.
	for (size_t i = 0; i < SIZEOF_ARRAY(StdParamsURL); ++i)
	{
		MapStrStr::iterator item = RegValues.find(StdParamsURL[i]);
		if (item != RegValues.end())
		{
			const char* url;
			char* TempBufUtf8 = (char*)TempBufName;
			size_t PrefixSz = 0;
			if ((item->second.size() >= 3) && (tolower(item->second[0]) >= 'a') && (tolower(item->second[0]) <= 'z') && (item->second[1] == ':') && ((item->second[2] == '\\') || (item->second[2] == '/')))
			{
				// URL scheme is a local file path, prepend with the needed URI prefix.
				PrefixSz = strcpy_len(TempBufUtf8, LARGE_BUF_SZ, "file:///");
			}
			else if ((item->second.size() >= 2) && (item->second[0] == '\\') && (item->second[1] == '\\'))
			{
				// URL scheme is a network file path, prepend with the needed URI prefix.
				PrefixSz = strcpy_len(TempBufUtf8, LARGE_BUF_SZ, "file:");
			}
			if (PrefixSz != 0)
			{
				// Path is being transformed from Windows-like file path into URI.
				strcpy_len(TempBufUtf8 + PrefixSz, LARGE_BUF_SZ - PrefixSz, item->second.c_str());
				char* pos = TempBufUtf8 + PrefixSz;
				while (*pos != '\0')
				{
					if (*pos == '\\')
						*pos = '/';
					++pos;
				}
				url = TempBufUtf8;
			}
			else
				url = item->second.c_str();
			f << "<tr>\n  <td>" << item->first.c_str() << "</td>\n  <td><a href=\"" << url << "\" target=\"_blank\">" << item->second.c_str() << "</a></td>\n</tr>\n";
			RegValues.erase(item);
		}
	}
	f << "</table>\n";

	delete[] TempBufName;
	delete[] TempBufValue;

	// Now output the ARP cache data if present.
	if (m_SIC != NULL)
	{
		f << "<br/>\n<hr/>\n<h4>" << GlobalTranslator.GetLine(LNG_INFO_ARP_CACHE).c_str() << "</h4>\n<table>\n";

		// Format size.
		if (m_SIC->m_InstallSize > 0)
		{
			double InstallSizeValue = (double)m_SIC->m_InstallSize;
			DWORD SzUnitIDs[] = {
				LNG_INFO_ARP_INSTALL_SIZE_B,
				LNG_INFO_ARP_INSTALL_SIZE_KB,
				LNG_INFO_ARP_INSTALL_SIZE_MB,
				LNG_INFO_ARP_INSTALL_SIZE_GB
			};
			size_t UnitIdx = 0;
			while ((InstallSizeValue > 1024) && (UnitIdx < SIZEOF_ARRAY(SzUnitIDs) - 1))
			{
				++UnitIdx;
				InstallSizeValue /= 1024.0;
			}
			f << "<tr>\n  <td>" << GlobalTranslator.GetLine(LNG_INFO_ARP_INSTALL_SIZE).c_str() << "</td>\n  <td>" << setiosflags(ios::fixed) << setprecision(2) << InstallSizeValue << " " << GlobalTranslator.GetLine(SzUnitIDs[UnitIdx]).c_str() << "</td>\n</tr>\n";
		}

		// Format last used timestamp.
		if (m_SIC->m_LastUsed.dwHighDateTime != (DWORD)-1)
		{
			SYSTEMTIME st;
			FileTimeToSystemTime(&m_SIC->m_LastUsed, &st);
			f << "<tr>\n  <td>" << GlobalTranslator.GetLine(LNG_INFO_ARP_LAST_USED).c_str() << "</td>\n  <td>" << st.wDay << "." << setw(2) << setfill('0') << st.wMonth << "." << st.wYear << "</td>\n</tr>\n";
		}

		// Format usage frequency.
		if (m_SIC->m_Frequency != (DWORD)-1)
		{
			DWORD LngID = 0;
			if (m_SIC->m_Frequency <= 2)
				LngID = LNG_INFO_ARP_FREQUENCY_RARELY;
			else if (m_SIC->m_Frequency <= 10)
				LngID = LNG_INFO_ARP_FREQUENCY_OCCASIONALLY;
			else
				LngID = LNG_INFO_ARP_FREQUENCY_FREQUENTLY;
			f << "<tr>\n  <td>" << GlobalTranslator.GetLine(LNG_INFO_ARP_FREQUENCY).c_str() << "</td>\n  <td>" << GlobalTranslator.GetLine(LngID).c_str() << "</td>\n</tr>\n";
		}

		// Format application main file name.
		if (m_SIC->m_HasName)
		{
			f << "<tr>\n  <td>" << GlobalTranslator.GetLine(LNG_INFO_ARP_NAME).c_str() << "</td>\n  <td>" << m_SIC->m_Name << "</td>\n</tr>\n";
		}

		f << "</table>\n";
	}

	// List the rest of the uninstall key values.
	if (!RegValues.empty())
	{
		f << "<br/>\n<hr/>\n<h4>" << GlobalTranslator.GetLine(LNG_INFO_OTHER_VALUES).c_str() << "</h4>\n<table>\n";
		for (MapStrStr::iterator item = RegValues.begin(); item != RegValues.end(); ++item)
			f << "<tr>\n  <td>" << item->first.c_str() << "</td>\n  <td>" << item->second.c_str() << "</td>\n</tr>\n";
		f << "</table>\n";
	}

	// Append HTML footer.
	f << "</body>\n</html>\n";

	free(DisplayName);
	f.close();
	RegCloseKey(hKeyEntry);
	return true;
}
