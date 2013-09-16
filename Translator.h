#pragma once

#include "LanguageIDs.h"


//////////////////////////////////////////////////////////////////////////
// Class for managing plugin interface translation.

class Translator
{
private:
	// Translation map ID => text.
	typedef map<DWORD, wstring> MapIdToString;
	MapIdToString m_TranslationMap;

	// Parses the language data from buffer in memory and updates the translation map.
	bool ParseLanguageData(const WCHAR* LngData, size_t LngDataSz);

public:
	Translator();
	~Translator();

	// Reads the language file and updates the translation map.
	bool ReadLanguageFile(const WCHAR* FileName);

	// Returns the translation text by its ID.
	const wstring& GetLine(DWORD Id) const;

	// Translate dialog controls according to the fixed mapping of controls to language IDs.
	void TranslateDialog(HWND hwndDlg, const DWORD* LangIDs);
};

// Global translator object.
extern Translator GlobalTranslator;
