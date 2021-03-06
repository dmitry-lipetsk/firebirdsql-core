/*
 *	PROGRAM:	Common Access Method
 *	MODULE:		init.h
 *	DESCRIPTION:	InitMutex, InitInstance - templates to help with initialization
 *
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Alexander Peshkoff
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Alexander Peshkoff <peshkoff@mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef CLASSES_INIT_INSTANCE_H
#define CLASSES_INIT_INSTANCE_H

#include "fb_types.h"
#include "../common/classes/alloc.h"

namespace Firebird {

namespace StaticMutex
{
	// Support for common mutex for various inits
	extern Mutex* mutex;
	void create();
	void release();
}

// InstanceControl - interface for almost all global variables

class InstanceControl
{
public:
	enum DtorPriority
	{
		STARTING_PRIORITY,			// Not to be used out of class InstanceControl
		PRIORITY_DETECT_UNLOAD,
		PRIORITY_DELETE_FIRST,
		PRIORITY_REGULAR,
		PRIORITY_TLS_KEY
	};

    InstanceControl();

	//
	// GlobalPtr should not be directly derived from class with virtual functions -
	// virtual table for its instances may become invalid in the moment,
	// when cleanup is needed. Therefore indirect link via InstanceList and
	// InstanceLink is established. This means more calls to memory allocator,
	// but who cares for 100 global variables?
	//

	class InstanceList
	{
	public:
		explicit InstanceList(DtorPriority p);
		virtual ~InstanceList();
		static void destructors();

	private:
		InstanceList* next;
		DtorPriority priority;
		virtual void dtor() = 0;
	};

	template <typename T, InstanceControl::DtorPriority P = InstanceControl::PRIORITY_REGULAR>
	class InstanceLink : private InstanceList, public GlobalStorage
	{
	private:
		T* link;

	public:
		explicit InstanceLink(T* l)
			: InstanceControl::InstanceList(P)
            , link(l)
		{
			fb_assert(this->link);
		}

		virtual void dtor() override final
		{
			fb_assert(this->link);

			if (this->link)
			{
				this->link->dtor();
				this->link = NULL;
			}
		}//dtor
	};//class InstanceLink

public:
	static void destructors();
	static void registerGdsCleanup(FPTR_VOID cleanup);
	static void registerShutdown(FPTR_VOID shutdown);

	static void cancelCleanup();
};


// GlobalPtr - template to help declaring global varables

template <typename T, InstanceControl::DtorPriority P = InstanceControl::PRIORITY_REGULAR>
class GlobalPtr : private InstanceControl
{
private:
	T* instance;

public:
	void dtor()
	{
		delete instance;
		instance = 0;
	}

	GlobalPtr()
	{
		// This means - for objects with ctors/dtors that want to be global,
		// provide ctor with MemoryPool& parameter. Even if it is ignored!
		instance = FB_NEW_POOL(*getDefaultMemoryPool()) T(*getDefaultMemoryPool());
		// Put ourselves into linked list for cleanup.
		// Allocated pointer is saved by InstanceList::constructor.
		FB_NEW InstanceControl::InstanceLink<GlobalPtr, P>(this);
	}

	T* operator->() throw()
	{
		return instance;
	}
	operator T&() throw()
	{
		return *instance;
	}
	T* operator&() throw()
	{
		return instance;
	}
};

// InitMutex - executes static void C::init() once and only once

template <typename C>
class InitMutex
{
private:
	volatile bool flag;
#ifdef DEV_BUILD
	const char* from;
#endif
public:
	explicit InitMutex(const char* f)
		: flag(false)
#ifdef DEV_BUILD
			  , from(f)
#define FB_LOCKED_FROM from
#else
#define FB_LOCKED_FROM NULL
#endif
	{ }
	void init()
	{
		if (!this->flag)
		{
			MutexLockGuard guard(*StaticMutex::mutex, FB_LOCKED_FROM);
			if (!this->flag)
			{
				C::init();
				this->flag = true;
			}
		}
	}
	void cleanup()
	{
		if (this->flag)
		{
			MutexLockGuard guard(*StaticMutex::mutex, FB_LOCKED_FROM);
			if (this->flag)
			{
				C::cleanup();
				this->flag = false;
			}
		}
	}
};//class InitMutex

#undef FB_LOCKED_FROM

////////////////////////////////////////////////////////////////////////////////
//class DefaultInstanceAllocator

template <typename T>
class DefaultInstanceAllocator
{
public:
	static T* create()
	{
		return FB_NEW_POOL(*getDefaultMemoryPool()) T(*getDefaultMemoryPool());
	}//create

	static void destroy(T* const inst)
	{
		delete inst;
	}//destroy
};//class DefaultInstanceAllocator

////////////////////////////////////////////////////////////////////////////////
//class InitInstance

// InitInstance - allocate instance of class T on first request.

template <typename T, class A = DefaultInstanceAllocator<T> >
class InitInstance : private InstanceControl
{
private:
	T* instance;
	volatile bool flag;
	A allocator;

public:
	InitInstance()
		: instance(nullptr)
        , flag(false)
	{ }

	T& operator () ()
	{
		if (!this->flag)
		{
			MutexLockGuard guard(*StaticMutex::mutex, "InitInstance");

			if (!this->flag)
			{
                this->instance = this->allocator.create(); //throw

                fb_assert(this->instance);

				try
                {
				    // Put ourselves into linked list for cleanup.
				    // Allocated pointer is saved by InstanceList::constructor.
				    FB_NEW InstanceControl::InstanceLink<InitInstance>(this); //throw
                }
                catch(...)
                {
                    T* const tmp=this->instance;

		            this->instance=nullptr;

                    A::destroy(tmp);

                    throw;
                }//catch

				this->flag = true;
			}//if
		}//if

        fb_assert(this->instance);

		return *this->instance;
	}//operator ()

	void dtor()
	{
        //! \todo
        //!  [BUG] This lock not prevent the access to this->instance
		MutexLockGuard guard(*StaticMutex::mutex, "InitInstance - dtor");

		this->flag = false;

		A::destroy(this->instance);

		this->instance = nullptr;
	}//dtor
};//class InitInstance

// Static - create instance of some class in static char[] buffer. Never destroy it.

template <typename T>
class StaticInstanceAllocator
{
private:
	char buf[sizeof(T) + FB_ALIGNMENT];

public:
	T* create()
	{
		return new((void*) FB_ALIGN(buf, FB_ALIGNMENT)) T();
	}

	static void destroy(T*)
	{ }
};

template <typename T>
class Static : private InitInstance<T, StaticInstanceAllocator<T> >
{
public:
	Static()
	{ }

	T* operator->()
	{
		return &(this->operator()());
	}

	T* operator&()
	{
		return operator->();
	}
};

} //namespace Firebird

#endif // CLASSES_INIT_INSTANCE_H
