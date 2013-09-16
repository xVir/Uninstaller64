#include "stdafx.h"
#include "resource.h"
#include "Translator.h"
#include "Globals.h"


// TODO: Implement as a singleton.
Translator GlobalTranslator;

Translator::Translator()
{
	m_TranslationMap.clear();
}

Translator::~Translator()
{
}

bool Translator::ParseLanguageData(const WCHAR* LngData, size_t LngDataSz)
{
	// The parser is implemented as Finite State Machine.

	// Skip the BOM signature if it is present.
	size_t pos = ((LngData[0] == 0xfeff) ? 1 : 0);

	// Available states of the FSM.
	enum ParserFSM
	{
		PARSER_NEWLINE,
		PARSER_COMMENT,
		PARSER_NUMBER,
		PARSER_QUOTE,
		PARSER_STRING
	};
	ParserFSM mode = PARSER_NEWLINE;    // The current FSM state.
	DWORD code = 0;
	size_t str_begin = 0;
	while (pos < LngDataSz)
	{
		switch (mode)
		{
		case PARSER_NEWLINE:
			// New line starts: expect a number, if not found skip the line as comment.
			if ((LngData[pos] >= L'0') && (LngData[pos] <= L'9'))
			{
				mode = PARSER_NUMBER;
				code = LngData[pos] - L'0';
			}
			else
				mode = PARSER_COMMENT;
			break;
		case PARSER_COMMENT:
			// Comment lasts till the end of line.
			if (LngData[pos] == L'\x0a')
				mode = PARSER_NEWLINE;
			break;
		case PARSER_NUMBER:
			// Number either continues with digits or meets the equal sign after which the translation string begins.
			if ((LngData[pos] >= L'0') && (LngData[pos] <= L'9'))
				code = code * 10 + (LngData[pos] - L'0');
			else if (LngData[pos] == L'=')
				mode = PARSER_QUOTE;
			else
				return false;
			break;
		case PARSER_QUOTE:
			// Translation string must start with double-quote or can be empty.
			if (LngData[pos] == L'"')
			{
				str_begin = pos + 1;
				mode = PARSER_STRING;
			}
			else if (LngData[pos] == L'\x0a')
				mode = PARSER_NEWLINE;
			break;
		case PARSER_STRING:
			if (LngData[pos] == L'"')
			{
				// Found end of translation string (closing double-quote), everything else up to the end of line is a comment.
				// Finalize the string: replace "\n" with real end-of-lines.
				// TODO: Support for embedded double-quote characters inside the string by escaping them.
				wstring str;
				str.assign(LngData + str_begin, pos - str_begin);
				for (size_t i = 0; i < str.size(); ++i)
				{
					if ((str[i] == L'\\') && (str[i + 1] == L'n'))
					{
						str[i] = L'\x0d';
						str[i + 1] = L'\x0a';
					}
				}
				m_TranslationMap[code] = str;
				mode = PARSER_COMMENT;
			}
			break;
		default:
			return false;
		}
		++pos;
	}
	return true;
}

bool Translator::ReadLanguageFile(const WCHAR* FileName)
{
	WCHAR* LngData = NULL;

	// First, parse the embedded language file from resources to fill all the IDs with default language.
	HRSRC hres = FindResource(PluginInstance, MAKEINTRESOURCE(IDR_DEFAULT_LANGUAGE), RT_RCDATA);
	if (hres == NULL)
		return false;
	HGLOBAL hg = LoadResource(PluginInstance, hres);
	if (hg == NULL)
		return false;
	LngData = (WCHAR*)LockResource(hg);
	if (LngData == NULL)
		return false;
	if (!ParseLanguageData(LngData, SizeofResource(PluginInstance, hres) / sizeof(WCHAR)))
		return false;

	// If external LNG file is not specified, internal English translation will remain.
	if ((FileName == NULL) || (FileName[0] == L'\0'))
		return true;

	// Otherwise read and parse the language file replacing the English lines with translated ones.
	HANDLE LngFile = CreateFile((wstring(PluginDirectory) + L"\\Language\\" + FileName).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (LngFile == INVALID_HANDLE_VALUE)
		return false;

	// Read file contents into memory.
	LARGE_INTEGER FileSz;
	if (!GetFileSizeEx(LngFile, &FileSz))
	{
		CloseHandle(LngFile);
		return false;
	}
	size_t LngFileDataSz = (size_t)(FileSz.QuadPart / sizeof(WCHAR));
	LngData = new WCHAR[LngFileDataSz];
	DWORD dr;
	bool res = (ReadFile(LngFile, LngData, (DWORD)FileSz.QuadPart, &dr, NULL) && (dr == FileSz.QuadPart));
	CloseHandle(LngFile);
	if (!res)
		return false;

	// Parse the file contents.
	if (!ParseLanguageData(LngData, LngFileDataSz))
		return false;

	return true;
}

const wstring& Translator::GetLine(DWORD Id) const
{
	// Search for the line with ID requested and return it or (if not found) special fixed string.
	static const wstring Untranslated = L"<UNTRANSLATED>";
	MapIdToString::const_iterator LineIter = m_TranslationMap.find(Id);
	return ((LineIter == m_TranslationMap.end()) ? Untranslated : LineIter->second);
}

void Translator::TranslateDialog(HWND hwndDlg, const DWORD* LangIDs)
{
	// The LangIDs array contains pairs (DialogControlID, TranslationLineID).
	// DialogControlID == -1 means the dialog title.
	// DialogControlID == 0 means the end of the array.
	size_t i = 0;
	while (LangIDs[i] != 0)
	{
		if (LangIDs[i] == (DWORD)-1)
			SetWindowText(hwndDlg, GetLine(LangIDs[i + 1]).c_str());
		else
			SetDlgItemText(hwndDlg, LangIDs[i], GetLine(LangIDs[i + 1]).c_str());
		i += 2;
	}
}
