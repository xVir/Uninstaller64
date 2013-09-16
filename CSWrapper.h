#pragma once


//////////////////////////////////////////////////////////////////////////
// Classes for simple wrapping critical section primitive

// Usage: Wrap a section of code you need to protect into a block
//        and declare a CriticalSectionLock variable in the beginning
//        of this block. Constructor will lock the critical section,
//        destructor will unlock it.

class CriticalSectionWrapper
{
private:
	CRITICAL_SECTION m_CS;
public:
	CriticalSectionWrapper();
	~CriticalSectionWrapper();

	friend class CriticalSectionLock;
};

// Global critical section object.
extern CriticalSectionWrapper CSWrapper;


class CriticalSectionLock
{
public:
	CriticalSectionLock();
	~CriticalSectionLock();
};
