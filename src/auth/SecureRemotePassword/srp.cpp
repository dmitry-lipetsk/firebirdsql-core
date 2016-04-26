#include "firebird.h"
#include "gen/iberror.h"

#include "../auth/SecureRemotePassword/srp.h"
#include "../common/classes/alloc.h"
#include "../common/classes/ImplementHelper.h"
#include "../common/classes/init.h"
#include "../common/classes/fb_string.h"
#include "../common/sha.h"

using namespace Firebird;

namespace Auth{
////////////////////////////////////////////////////////////////////////////////
//class RemoteGroup

class RemoteGroup
{
private:
    typedef RemoteGroup                     self_type;

    RemoteGroup(const self_type&);
    self_type& operator = (const self_type&);

public:
    BigInteger const prime;
    BigInteger const generator;
    BigInteger       k;

public:
	explicit RemoteGroup(Firebird::MemoryPool&);

public:
	static RemoteGroup* getGroup()
	{
		return &sm_instance();
	}

private:
    static const char* const sm_primeStr;

    static const char* const sm_genStr;

private:
	static InitInstance<RemoteGroup> sm_instance;
};//class RemoteGroup

////////////////////////////////////////////////////////////////////////////////
//class RemoteGroup

InitInstance<RemoteGroup> RemoteGroup::sm_instance;

//------------------------------------------------------------------------
const char* const RemoteGroup::sm_primeStr
  ="E67D2E994B2F900C3F41F08F5BB2627ED0D49EE1FE767A52EFCD565C"
   "D6E768812C3E1E9CE8F0A8BEA6CB13CD29DDEBF7A96D4A93B55D488D"
   "F099A15C89DCB0640738EB2CBDD9A8F7BAB561AB1B0DC1C6CDABF303"
   "264A08D1BCA932D1F1EE428B619D970F342ABA9A65793B8B2F041AE5"
   "364350C16F735F56ECBCA87BD57B29E7";

//------------------------------------------------------------------------
const char* const RemoteGroup::sm_genStr
 ="02";

//------------------------------------------------------------------------
RemoteGroup::RemoteGroup(Firebird::MemoryPool&)
 : prime(sm_primeStr)
 , generator(sm_genStr)
 , k()
{
	Auth::Sha1 hash;

	hash.processInt(this->prime);

	if (this->prime.length() > this->generator.length())
	{
		size_t const pad = (this->prime.length() - this->generator.length());

		char pb[1024];

        fb_assert(pad<=sizeof(pb));

		memset(pb, 0, pad);

		hash.process(pad, pb);
	}//if

	hash.processInt(this->generator);

	hash.getInt(k);
}//RemoteGroup

////////////////////////////////////////////////////////////////////////////////
//class RemotePassword

const char* const RemotePassword::plugName = "Srp";

//------------------------------------------------------------------------
RemotePassword::RemotePassword()
	: group(RemoteGroup::getGroup())
{
#if SRP_DEBUG > 1
	this->privateKey = BigInteger("60975527035CF2AD1989806F0407210BC81EDC04E2762A56AFD529DDDA2D4393");
#else
	this->privateKey.random(RemotePassword::SRP_KEY_SIZE);
#endif
	this->privateKey %= this->group->prime;
}//RemotePassword

//------------------------------------------------------------------------
BigInteger RemotePassword::getUserHash(const char* const account,
                                       const char* const salt,
                                       const char* const password)
{
	hash.reset();
	hash.process(account);
	hash.process(":");
	hash.process(password);
	UCharBuffer hash1;
	hash.getHash(hash1);

	hash.reset();
	hash.process(salt);
	hash.process(hash1);
	BigInteger rc;
	hash.getInt(rc);

	return rc;
}//getUserHash

//------------------------------------------------------------------------
BigInteger RemotePassword::computeVerifier(const string& account,
                                           const string& salt,
                                           const string& password)
{
	const BigInteger x(getUserHash(account.c_str(), salt.c_str(), password.c_str()));

	return group->generator.modPow(x, group->prime);
}//computeVerifier

//------------------------------------------------------------------------
void RemotePassword::genClientKey(string& pubkey)
{
	dumpIt("privateKey(C)", privateKey);
	clientPublicKey = group->generator.modPow(privateKey, group->prime);
	clientPublicKey.getText(pubkey);
	dumpIt("clientPublicKey", clientPublicKey);
}//genClientKey

//------------------------------------------------------------------------
void RemotePassword::genServerKey(string&                      pubkey,
                                  const Firebird::UCharBuffer& verifier)
{
	dumpIt("privateKey(S)", privateKey);

	BigInteger gb(group->generator.modPow(privateKey, group->prime));	// g^b

	dumpIt("gb", gb);

	BigInteger v(verifier);												// v

	BigInteger kv = (group->k * v) % group->prime;

	dumpIt("kv", kv);

	serverPublicKey = (kv + gb) % group->prime;

	serverPublicKey.getText(pubkey);

	dumpIt("serverPublicKey", serverPublicKey);
}//genServerKey

//------------------------------------------------------------------------
void RemotePassword::computeScramble()
{
	hash.reset();
	dumpIt("computeScramble: clientPublicKey", clientPublicKey);
	hash.processStrippedInt(clientPublicKey);
	dumpIt("computeScramble: serverPublicKey", serverPublicKey);
	hash.processStrippedInt(serverPublicKey);
	hash.getInt(scramble);
}//computeScramble

//------------------------------------------------------------------------
void RemotePassword::clientSessionKey(UCharBuffer&       sessionKey,
                                      const char*  const account,
									  const char*  const salt,
                                      const char*  const password,
									  const char*  const serverPubKey)
{
	serverPublicKey = BigInteger(serverPubKey);
	computeScramble();
	dumpIt("scramble", scramble);
	dumpIt("password", password);
	BigInteger x = getUserHash(account, salt, password);		// x
	dumpIt("x", x);
	BigInteger gx = group->generator.modPow(x, group->prime);	// g^x
	BigInteger kgx = (group->k * gx) % group->prime;			// kg^x
	dumpIt("kgx", kgx);
	BigInteger diff = (serverPublicKey - kgx) % group->prime;	// B - kg^x
	BigInteger ux = (scramble * x) % group->prime;				// ux
	BigInteger aux = (privateKey + ux) % group->prime;			// A + ux
	dumpIt("clientPrivateKey", privateKey);
	dumpIt("aux", aux);
	BigInteger sessionSecret = diff.modPow(aux, group->prime);	// (B - kg^x) ^ (a + ux)
	dumpIt("sessionSecret", sessionSecret);

	hash.reset();
	hash.processStrippedInt(sessionSecret);
	hash.getHash(sessionKey);
}//clientSessionKey

//------------------------------------------------------------------------
void RemotePassword::serverSessionKey(UCharBuffer&       sessionKey,
                                      const char*  const clientPubKey,
									  const UCharBuffer& verifier)
{
	clientPublicKey = BigInteger(clientPubKey);
	computeScramble();
	dumpIt("scramble", scramble);
	BigInteger v = BigInteger(verifier);
	BigInteger vu = v.modPow(scramble, group->prime);					// v^u
	BigInteger Avu = (clientPublicKey * vu) % group->prime;				// Av^u
	dumpIt("Avu", Avu);
	BigInteger sessionSecret = Avu.modPow(privateKey, group->prime);	// (Av^u) ^ b
	dumpIt("serverPrivateKey", privateKey);
	dumpIt("sessionSecret", sessionSecret);

	hash.reset();
	hash.processStrippedInt(sessionSecret);
	hash.getHash(sessionKey);
}//serverSessionKey

//------------------------------------------------------------------------

// H(H(prime) ^ H(g), H(I), s, A, B, K)

BigInteger RemotePassword::clientProof(const char*  const account,
                                       const char*  const salt,
                                       const UCharBuffer& sessionKey)
{
	hash.reset();
	hash.processInt(group->prime);
	BigInteger n1;
	hash.getInt(n1);

	hash.reset();
	hash.processInt(group->generator);
	BigInteger n2;
	hash.getInt(n2);
	n1 = n1.modPow(n2, group->prime);

	hash.reset();
	hash.process(account);
	hash.getInt(n2);

	hash.reset();
	hash.processInt(n1);				// H(prime) ^ H(g)
	hash.processInt(n2);				// H(I)
	hash.process(salt);					// s
	hash.processInt(clientPublicKey);	// A
	hash.processInt(serverPublicKey);	// B
	hash.process(sessionKey);			// K

	BigInteger rc;
	hash.getInt(rc);
	return rc;
}//clientProof

////////////////////////////////////////////////////////////////////////////////

#if SRP_DEBUG > 0
void dumpIt(const char* name, const Firebird::UCharBuffer& data)
{
	fprintf(stderr, "%s\n", name);
	for (size_t x=0; x<data.getCount(); ++x)
		fprintf(stderr, "%02x ", data[x]);
	fprintf(stderr, "\n");
}

//------------------------------------------------------------------------
void dumpIt(const char* name, const Firebird::string& str)
{
	fprintf(stderr, "%s: '%s'\n", name, str.c_str());
}

//------------------------------------------------------------------------
void dumpBin(const char* name, const Firebird::string& str)
{
	fprintf(stderr, "%s (%ld)\n", name, str.length());
	for (size_t x = 0; x < str.length(); ++x)
		fprintf(stderr, "%02x ", str[x]);
	fprintf(stderr, "\n");
}

//------------------------------------------------------------------------
void dumpIt(const char* name, const BigInteger& bi)
{
	string x;
	bi.getText(x);
	dumpIt(name, x);
}

#endif

//------------------------------------------------------------------------
void checkStatusVectorForMissingTable(const ISC_STATUS* v)
{
	while (v[0] == isc_arg_gds)
	{
		if (v[1] == isc_dsql_relation_err)
		{
			Arg::Gds(isc_missing_data_structures).raise();
		}

		do
		{
			v += 2;
		}
        while (v[0] != isc_arg_warning && v[0] != isc_arg_gds && v[0] != isc_arg_end);
	}//while
}//checkStatusVectorForMissingTable

////////////////////////////////////////////////////////////////////////////////
}//namespace Auth
