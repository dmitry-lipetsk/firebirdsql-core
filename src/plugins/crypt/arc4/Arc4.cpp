/*
 *	PROGRAM:		Firebird authentication.
 *	MODULE:			SrpClient.cpp
 *	DESCRIPTION:	SPR authentication plugin.
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
 *  The Original Code was created by Alex Peshkov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2012 Alex Peshkov <peshkoff at mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"

#include "./Arc4.h"
#include "../common/classes/ImplementHelper.h"

using namespace Firebird;

namespace{
////////////////////////////////////////////////////////////////////////////////
//class Cypher

class Cypher : public GlobalStorage
{
public:
	Cypher(unsigned int l, const unsigned char* key) throw()
		: s1(0), s2(0)
	{
		for (unsigned int n = 0; n < sizeof(this->state); ++n)
		{
			this->state[n] = n;
		}

		for (unsigned int k1 = 0, k2 = 0; k1 < sizeof(state); ++k1)
		{
			k2 = (k2 + key[k1 % l] + this->state[k1]) & 0xff;

			swap(this->state[k1], this->state[k2]);
		}
	}//Cypher

	void transform(unsigned int length, const void* from, void* to) throw()
	{
		unsigned char* t = static_cast<unsigned char*>(to);
		const unsigned char* f = static_cast<const unsigned char*>(from);

		while (length--)
		{
			this->s2 += this->state[++this->s1];

			swap(this->state[this->s1], this->state[this->s2]);

			unsigned char k = this->state[this->s1] + this->state[this->s2];

			k = this->state[k];

			*t++ = k ^ *f++;
		}
	}//transform

private:
	unsigned char state[256];
	unsigned char s1;
	unsigned char s2;

	static void swap(unsigned char& c1, unsigned char& c2) throw()
	{
		const unsigned char temp = c1;
		c1 = c2;
		c2 = temp;
	}
};//class Cypher

////////////////////////////////////////////////////////////////////////////////
} // anonymous namespace


namespace Crypt {

class Arc4 FB_FINAL : public StdPlugin<IWireCryptPluginImpl<Arc4, CheckStatusWrapper> >
{
public:
	explicit Arc4(IPluginConfig*)
		: en(NULL), de(NULL)
	{ }

	~Arc4()
	{
		delete en;
		delete de;
	}

	// ICryptPlugin implementation
	virtual const char* getKnownTypes(CheckStatusWrapper* status)override final;

    virtual void setKey(CheckStatusWrapper* status, ICryptKey* key)override final;
	
    virtual void encrypt(CheckStatusWrapper* status, unsigned int length, const void* from, void* to)override final;
	
    virtual void decrypt(CheckStatusWrapper* status, unsigned int length, const void* from, void* to)override final;
	
    virtual int release()override final;

private:
	Cypher* createCypher(unsigned int l, const void* key);
	Cypher* en;
	Cypher* de;
};

int Arc4::release()
{
	if (--refCounter == 0)
	{
		delete this;
		return 0;
	}
	return 1;
}

void Arc4::setKey(CheckStatusWrapper* status, ICryptKey* key)
{
	status->init();
	try
	{
    	unsigned int l;
		const void* k = key->getEncryptKey(&l);
		en = createCypher(l, k);

	    k = key->getDecryptKey(&l);
		de = createCypher(l, k);
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
	}
}

void Arc4::encrypt(CheckStatusWrapper* status, unsigned int length, const void* from, void* to)
{
	status->init();
	en->transform(length, from, to);
}

void Arc4::decrypt(CheckStatusWrapper* status, unsigned int length, const void* from, void* to)
{
	status->init();
	de->transform(length, from, to);
}

Cypher* Arc4::createCypher(unsigned int l, const void* key)
{
	return FB_NEW Cypher(l, static_cast<const unsigned char*>(key));
}

const char* Arc4::getKnownTypes(CheckStatusWrapper* status)
{
	status->init();
	return "Symmetric";
}


namespace
{
	SimpleFactory<Arc4> factory;
}

void registerArc4(IPluginManager* iPlugin)
{
	iPlugin->registerPluginFactory(IPluginManager::TYPE_WIRE_CRYPT, "Arc4", &factory);
}

} // namespace Crypt
