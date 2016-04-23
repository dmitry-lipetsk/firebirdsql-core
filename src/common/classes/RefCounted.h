/*
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
 *  The Original Code was created by Vlad Horsun
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2008 Vlad Horsun <hvlad@users.sf.net>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 *  Dmitry Yemanov <dimitr@users.sf.net>
 */

#ifndef COMMON_REF_COUNTED_H
#define COMMON_REF_COUNTED_H

#include "../common/classes/fb_atomic.h"
#include "../common/gdsassert.h"

namespace Firebird{
////////////////////////////////////////////////////////////////////////////////
//class RefCounted

class RefCounted
{
public:
	virtual int addRef()
	{
		return ++m_refCnt;
	}

	virtual int release()
	{
		fb_assert(m_refCnt.value() > 0);
		const int refCnt = --m_refCnt;
		if (!refCnt)
			delete this;
		return refCnt;
	}

protected:
	RefCounted() : m_refCnt(0) {}

	virtual ~RefCounted()
	{
		fb_assert(m_refCnt.value() == 0);
	}

private:
	AtomicCounter m_refCnt;
};//class RefCounted

////////////////////////////////////////////////////////////////////////////////
//class Reference

// reference counted object guard

class Reference
{
public:
	explicit Reference(RefCounted& refCounted) :
		r(refCounted)
	{
		r.addRef();
	}

	~Reference()
	{
		try {
			r.release();
		}
		catch (const Exception&)
		{
			DtorException::devHalt();
		}
	}

private:
	RefCounted& r;
};//class Reference

////////////////////////////////////////////////////////////////////////////////

enum NoIncrement {REF_NO_INCR};

////////////////////////////////////////////////////////////////////////////////
//class RefPtr

// controls reference counter of the object where points
template <typename T>
class RefPtr
{
private:
    typedef RefPtr<T>                   self_type;

public:
	RefPtr()
     : ptr(nullptr)
	{ }

	explicit RefPtr(T* const p)
     : ptr(p)
	{
		if (this->ptr)
		{
			helper__addRef(this->ptr);
		}
	}//RefPtr

	// This special form of ctor is used to create refcounted ptr from interface,
	// returned by a function (which increments counter on return)
	RefPtr(NoIncrement /*x*/, T* const p)
     : ptr(p)
	{ }

	RefPtr(const self_type& r)
     : ptr(r.ptr)
	{
		if (this->ptr)
		{
			helper__addRef(this->ptr);
		}
	}//RefPtr

	RefPtr(self_type&& rr)
     : ptr(std::move(rr.ptr))
    {
        rr.ptr=nullptr;
    }//move constructor

   ~RefPtr()
	{
        T* const tmp=this->ptr;

        this->ptr=nullptr;

        if(tmp)
        {
            helper__release(tmp);
        }
	}//~RefPtr

	self_type& assignRefNoIncr(T* const p)
	{
		if(p != this->ptr)
        {
             T* const tmp=this->ptr;

             this->ptr=p;

             if(tmp)
             {
                 helper__release(tmp);
             }
        }//if

		return *this;
	}//assignRefNoIncr

	self_type& operator = (T* const p)
	{
		return this->assign(p);
	}

	self_type& operator = (const self_type& r)
	{
		return this->assign(r.ptr);
	}

	self_type& operator = (self_type&& rr)
    {
        if(&rr!=this)
        {
            this->ptr=std::move(rr.ptr);

            rr.ptr=nullptr;
        }

        return *this;
    }//move operator

	operator T* () const
	{
		return this->ptr;
	}

	T* operator -> () const
	{
		return this->ptr;
	}

	bool hasData() const
	{
		return this->ptr ? true : false;
	}

	T* getPtr() const
	{
		return this->ptr;
	}

private:
	self_type& assign(T* const p)
	{
		if (this->ptr != p)
		{
			if (p)
			{
				helper__addRef(p);
			}

			T* const tmp = this->ptr;
			this->ptr = p;

			if (tmp)
			{
				helper__release(tmp);
			}
		}//if

		return *this;
	}//assign

private:
    template<typename T1>
    static void helper__addRef(T1* const p)
    {
        fb_assert(p);
        p->addRef();
    }//helper__addRef

    template<typename T1>
    static void helper__release(T1* const p)
    {
        fb_assert(p);
        p->release();
    }//helper__release

private:
    template<typename T1>
    static void helper__addRef(const T1* const p)
    {
        fb_assert(p);
        const_cast<T1*>(p)->addRef();
    }//helper__addRef - const version

    template<typename T1>
    static void helper__release(const T1* const p)
    {
        fb_assert(p);
        const_cast<T1*>(p)->release();
    }//helper__release - const version

private:
	T* ptr;
};//class RefPtr

////////////////////////////////////////////////////////////////////////////////
//class AnyRef

template <typename T>
class AnyRef : public T
             , public RefCounted
{
public:
	inline AnyRef() : T() {}

	inline AnyRef(const T& v) : T(v) {}

	inline explicit AnyRef(MemoryPool& p) : T(p) {}

	inline AnyRef(MemoryPool& p, const T& v) : T(p, v) {}
};//class AnyRef

////////////////////////////////////////////////////////////////////////////////
}//namespace Firebird
#endif // COMMON_REF_COUNTED_H
