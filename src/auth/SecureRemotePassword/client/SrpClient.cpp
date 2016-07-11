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
 *  Copyright (c) 2011 Alex Peshkov <peshkoff at mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"

#include "../auth/SecureRemotePassword/client/SrpClient.h"
#include "../auth/SecureRemotePassword/srp.h"
#include "../common/classes/ImplementHelper.h"

using namespace Firebird;

namespace Auth {
////////////////////////////////////////////////////////////////////////////////
//class SrpClient

class SrpClient FB_FINAL : public StdPlugin<IClientImpl<SrpClient, CheckStatusWrapper> >
{
public:
	explicit SrpClient(IPluginConfig*)
		: client(nullptr)
        , data(getPool())
        , sessionKey(getPool())
	{ }

	~SrpClient()
	{
		delete this->client;
	}

	// IClient implementation
	virtual int authenticate(CheckStatusWrapper* status,
                             IClientBlock*       cb) override final;

    virtual int release() override final;

private:
	RemotePassword* client;
	string data;
	UCharBuffer sessionKey;
};//class SrpClient

////////////////////////////////////////////////////////////////////////////////
//class SrpClient

int SrpClient::authenticate(CheckStatusWrapper* const status,
                            IClientBlock*       const cb)
{
    fb_assert(status);
    fb_assert(cb);

	try
	{
		if (this->sessionKey.hasData())
		{
			// Why are we called when auth is completed?
			(Arg::Gds(isc_random) << "Auth sync failure - SRP's authenticate called more times than supported").raise();
		}

		if (!this->client)
		{
			HANDSHAKE_DEBUG(fprintf(stderr, "Cli: SRP phase1: login=%s password=%s\n",
				cb->getLogin(), cb->getPassword()));

			if (!(cb->getLogin() && cb->getPassword()))
			{
				return AUTH_CONTINUE;
			}

			this->client = FB_NEW RemotePassword;

			this->client->genClientKey(this->data);

			dumpIt("Clnt: clientPubKey", this->data);

			cb->putData(status, this->data.length(), this->data.begin());

			if (status->getState() & IStatus::STATE_ERRORS)
				return AUTH_FAILED;

			return AUTH_MORE_DATA;
		}//if !this->client

		HANDSHAKE_DEBUG(fprintf(stderr, "Cli: SRP phase2\n"));

		unsigned length;

        //saltAndKey contains the data received from server in p_acpd.p_acpt_data
        const unsigned char* saltAndKey = cb->getData(&length);

		if (!saltAndKey || length == 0)
		{
			Arg::Gds(isc_auth_data).raise();
		}

		const unsigned expectedLength =
			(RemotePassword::SRP_SALT_SIZE + RemotePassword::SRP_KEY_SIZE + 2) * 2;

		if (length > expectedLength)
		{
			(Arg::Gds(isc_auth_datalength) << Arg::Num(length) <<
				Arg::Num(expectedLength) << "data").raise();
		}

		unsigned charSize = *saltAndKey++;

		charSize += ((unsigned) *saltAndKey++) << 8;

		if (charSize > RemotePassword::SRP_SALT_SIZE * 2)
		{
			(Arg::Gds(isc_auth_datalength) << Arg::Num(charSize) <<
				Arg::Num(RemotePassword::SRP_SALT_SIZE * 2) << "salt").raise();
		}

		const string salt(saltAndKey, charSize);

		dumpIt("Clnt: salt", salt);

		saltAndKey += charSize;

		length -= (charSize + 2);

		charSize = *saltAndKey++;
		charSize += ((unsigned) *saltAndKey++) << 8;

		if (charSize != length - 2)
		{
			(Arg::Gds(isc_auth_datalength) << Arg::Num(charSize) <<
				Arg::Num(length - 2) << "key").raise();
		}

		const string key(saltAndKey, charSize);

		dumpIt("Clnt: key(srvPub)", key);

		dumpIt("Clnt: login", string(cb->getLogin()));
		dumpIt("Clnt: pass", string(cb->getPassword()));

		this->client->clientSessionKey(sessionKey, cb->getLogin(), salt.c_str(), cb->getPassword(), key.c_str());

		dumpIt("Clnt: sessionKey", sessionKey);

		const BigInteger cProof = this->client->clientProof(cb->getLogin(), salt.c_str(), sessionKey);

		cProof.getText(this->data);

		cb->putData(status, this->data.length(), this->data.c_str());

		if (status->getState() & IStatus::STATE_ERRORS)
		{
			return AUTH_FAILED;
		}

		// output the key
		ICryptKey* const cKey = cb->newKey(status);

		if (status->getState() & IStatus::STATE_ERRORS)
		{
			return AUTH_FAILED;
		}

		cKey->setSymmetric(status, "Symmetric", sessionKey.getCount(), sessionKey.begin());

		if (status->getState() & IStatus::STATE_ERRORS)
		{
			return AUTH_FAILED;
		}
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
		return AUTH_FAILED;
	}

	return AUTH_SUCCESS;
}//authenticate

//------------------------------------------------------------------------
int SrpClient::release()
{
	if (--refCounter == 0)
	{
		delete this;

		return 0;
	}//if

	return 1;
}//release

////////////////////////////////////////////////////////////////////////////////

namespace
{
	SimpleFactory<SrpClient> factory;
}

void registerSrpClient(IPluginManager* iPlugin)
{
	iPlugin->registerPluginFactory(IPluginManager::TYPE_AUTH_CLIENT, RemotePassword::plugName, &factory);
}

} // namespace Auth
