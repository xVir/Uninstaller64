#include "stdafx.h"
#include "CSWrapper.h"


// TODO: Implement as a singleton.
CriticalSectionWrapper CSWrapper;

CriticalSectionWrapper::CriticalSectionWrapper()
{
	InitializeCriticalSection(&m_CS);
}

CriticalSectionWrapper::~CriticalSectionWrapper()
{
	DeleteCriticalSection(&m_CS);
}


CriticalSectionLock::CriticalSectionLock()
{
	EnterCriticalSection(&CSWrapper.m_CS);
}

CriticalSectionLock::~CriticalSectionLock()
{
	LeaveCriticalSection(&CSWrapper.m_CS);
}
