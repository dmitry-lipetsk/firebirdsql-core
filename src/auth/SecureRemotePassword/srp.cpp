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
//class RemotePassword

const char* const RemotePassword::plugName = "Srp";

//------------------------------------------------------------------------
const unsigned char RemotePassword::sm_primeStr[]
 ={0xE6,0x7D,0x2E,0x99,0x4B,0x2F,0x90,0x0C,0x3F,0x41,0xF0,0x8F,0x5B,0xB2,
   0x62,0x7E,0xD0,0xD4,0x9E,0xE1,0xFE,0x76,0x7A,0x52,0xEF,0xCD,0x56,0x5C,
   0xD6,0xE7,0x68,0x81,0x2C,0x3E,0x1E,0x9C,0xE8,0xF0,0xA8,0xBE,0xA6,0xCB,
   0x13,0xCD,0x29,0xDD,0xEB,0xF7,0xA9,0x6D,0x4A,0x93,0xB5,0x5D,0x48,0x8D,
   0xF0,0x99,0xA1,0x5C,0x89,0xDC,0xB0,0x64,0x07,0x38,0xEB,0x2C,0xBD,0xD9,
   0xA8,0xF7,0xBA,0xB5,0x61,0xAB,0x1B,0x0D,0xC1,0xC6,0xCD,0xAB,0xF3,0x03,
   0x26,0x4A,0x08,0xD1,0xBC,0xA9,0x32,0xD1,0xF1,0xEE,0x42,0x8B,0x61,0x9D,
   0x97,0x0F,0x34,0x2A,0xBA,0x9A,0x65,0x79,0x3B,0x8B,0x2F,0x04,0x1A,0xE5,
   0x36,0x43,0x50,0xC1,0x6F,0x73,0x5F,0x56,0xEC,0xBC,0xA8,0x7B,0xD5,0x7B,
   0x29,0xE7};

//------------------------------------------------------------------------
const unsigned char RemotePassword::sm_genStr[]
 ={0x02};

//------------------------------------------------------------------------
const unsigned char RemotePassword::sm_kStr[]
 ={0xDF,0xC2,0x12,0xB4,0xBD,0x69,0x67,0x48,0x55,0xCF,0xCE,0xB3,0x00,0x02,0xB5,0xC3,0x06,0xAC,0x60,0xB5};

//------------------------------------------------------------------------
RemotePassword::RemotePassword()
	: prime(sizeof(sm_primeStr),sm_primeStr)
    , generator(sizeof(sm_genStr),sm_genStr)
    , k(sizeof(sm_kStr),sm_kStr)
{
#if SRP_DEBUG > 1
	this->privateKey = BigInteger("60975527035CF2AD1989806F0407210BC81EDC04E2762A56AFD529DDDA2D4393");
#else
	this->privateKey.random(RemotePassword::SRP_KEY_SIZE);
#endif
	this->privateKey %= this->prime;
}//RemotePassword

//------------------------------------------------------------------------
BigInteger RemotePassword::getUserHash(const char* const account,
                                       const char* const salt,
                                       const char* const password)
{
    //[2016-07-08]
    fb_assert(account);
    fb_assert(salt);
    fb_assert(password);

	Firebird::Sha1 hash;

    Firebird::UCharBuffer tmp_bytes;

	//hash.reset();
	hash.process(account);
	hash.process(":");
	hash.process(password);
	UCharBuffer hash1;
	hash.getHash(hash1);

	hash.reset();
	hash.process(salt);
	hash.process(hash1);
	BigInteger rc;
	helper__getInt(tmp_bytes,hash,&rc);

	return rc;
}//getUserHash

//------------------------------------------------------------------------
BigInteger RemotePassword::computeVerifier(const string& account,
                                           const string& salt,
                                           const string& password)
{
	const BigInteger x(getUserHash(account.c_str(), salt.c_str(), password.c_str()));

	return this->generator.modPow(x, this->prime);
}//computeVerifier

//------------------------------------------------------------------------
void RemotePassword::genClientKey(string& pubkey)
{
	dumpIt("privateKey(C)", this->privateKey);

	this->clientPublicKey = this->generator.modPow(this->privateKey, this->prime);

	this->clientPublicKey.getText(pubkey);

	dumpIt("clientPublicKey", this->clientPublicKey);
}//genClientKey

//------------------------------------------------------------------------
void RemotePassword::genServerKey(string&                      pubkey,
                                  const Firebird::UCharBuffer& verifier)
{
	dumpIt("privateKey(S)", this->privateKey);

	const BigInteger gb(this->generator.modPow(this->privateKey, this->prime));	// g^b

	dumpIt("gb", gb);

	const BigInteger v(verifier);											// v

	const BigInteger kv = (this->k * v) % this->prime;

	dumpIt("kv", kv);

	this->serverPublicKey = (kv + gb) % this->prime;

	this->serverPublicKey.getText(pubkey);

	dumpIt("serverPublicKey", this->serverPublicKey);
}//genServerKey

//------------------------------------------------------------------------
void RemotePassword::computeScramble()
{
	Firebird::Sha1 hash;

    Firebird::UCharBuffer tmp_bytes;

	//hash.reset();

	dumpIt("computeScramble: clientPublicKey", this->clientPublicKey);

	helper__processStrippedInt(tmp_bytes,this->clientPublicKey,&hash);

	dumpIt("computeScramble: serverPublicKey", this->serverPublicKey);

	helper__processStrippedInt(tmp_bytes,this->serverPublicKey,&hash);

	helper__getInt(tmp_bytes,hash,&this->scramble);
}//computeScramble

//------------------------------------------------------------------------
void RemotePassword::clientSessionKey(UCharBuffer&       sessionKey,
                                      const char*  const account,
									  const char*  const salt,
                                      const char*  const password,
									  const char*  const serverPubKey)
{
	this->serverPublicKey = BigInteger(serverPubKey);

	this->computeScramble();

	dumpIt("scramble", scramble);

	dumpIt("password", password);

	const BigInteger x = getUserHash(account, salt, password);		// x

	dumpIt("x", x);

	const BigInteger gx = this->generator.modPow(x, this->prime);	// g^x

	const BigInteger kgx = (this->k * gx) % this->prime;			// kg^x

	dumpIt("kgx", kgx);

	const BigInteger diff = (this->serverPublicKey - kgx) % this->prime;	// B - kg^x

	const BigInteger ux = (scramble * x) % this->prime;			// ux

	const BigInteger aux = (this->privateKey + ux) % this->prime;	// A + ux

	dumpIt("clientPrivateKey", this->privateKey);

	dumpIt("aux", aux);

	const BigInteger sessionSecret = diff.modPow(aux, this->prime);// (B - kg^x) ^ (a + ux)

	dumpIt("sessionSecret", sessionSecret);

	Firebird::Sha1 hash;

    Firebird::UCharBuffer tmp_bytes;

	//hash.reset();
	helper__processStrippedInt(tmp_bytes,sessionSecret,&hash);
	hash.getHash(sessionKey);
}//clientSessionKey

//------------------------------------------------------------------------
void RemotePassword::serverSessionKey(UCharBuffer&       sessionKey,
                                      const char*  const clientPubKey,
									  const UCharBuffer& verifier)
{
	this->clientPublicKey = BigInteger(clientPubKey);

	this->computeScramble();

	dumpIt("scramble", scramble);

	const BigInteger v = BigInteger(verifier);

	const BigInteger vu = v.modPow(scramble, this->prime);					// v^u

	const BigInteger Avu = (this->clientPublicKey * vu) % this->prime;		// Av^u

	dumpIt("Avu", Avu);

	const BigInteger sessionSecret = Avu.modPow(this->privateKey, this->prime);	// (Av^u) ^ b

	dumpIt("serverPrivateKey", this->privateKey);

	dumpIt("sessionSecret", sessionSecret);

	Firebird::Sha1 hash;

    Firebird::UCharBuffer tmp_bytes;

	//hash.reset();
	helper__processStrippedInt(tmp_bytes,sessionSecret,&hash);
	hash.getHash(sessionKey);
}//serverSessionKey

//------------------------------------------------------------------------

// H(H(prime) ^ H(g), H(I), s, A, B, K)

BigInteger RemotePassword::clientProof(const char*  const account,
                                       const char*  const salt,
                                       const UCharBuffer& sessionKey)
{
	Firebird::Sha1 hash;

    Firebird::UCharBuffer tmp_bytes;

	//hash.reset();
	helper__processInt(tmp_bytes,this->prime,&hash);
	BigInteger n1;
	helper__getInt(tmp_bytes,hash,&n1);

	hash.reset();
	helper__processInt(tmp_bytes,this->generator,&hash);
	BigInteger n2;
	helper__getInt(tmp_bytes,hash,&n2);
	n1 = n1.modPow(n2, this->prime);

	hash.reset();
	hash.process(account);
	helper__getInt(tmp_bytes,hash,&n2);

	hash.reset();
	helper__processInt(tmp_bytes,n1,&hash);				// H(prime) ^ H(g)
	helper__processInt(tmp_bytes,n2,&hash);				// H(I)
	hash.process(salt);					// s
	helper__processInt(tmp_bytes,this->clientPublicKey,&hash);	// A
	helper__processInt(tmp_bytes,this->serverPublicKey,&hash);	// B
	hash.process(sessionKey);			// K

	BigInteger rc;
	helper__getInt(tmp_bytes,hash,&rc);
	return rc;
}//clientProof

//------------------------------------------------------------------------
void RemotePassword::helper__getInt(Firebird::UCharBuffer&      tmp_bytes,
							        Firebird::Sha1&             sha1,
							        Firebird::BigInteger* const hash)
{
	sha1.getHash(tmp_bytes);

	hash->assign(tmp_bytes.getCount(), tmp_bytes.begin());
}//helper__getInt

//------------------------------------------------------------------------
void RemotePassword::helper__processInt(Firebird::UCharBuffer&      tmp_bytes,
							            const Firebird::BigInteger& data,
								        Firebird::Sha1* const       sha1)
{
	data.getBytes(tmp_bytes);

	sha1->process(tmp_bytes);
}//helper__processInt

//------------------------------------------------------------------------
void RemotePassword::helper__processStrippedInt(Firebird::UCharBuffer&      tmp_bytes,
										        const Firebird::BigInteger& data,
										        Firebird::Sha1* const       sha1)
{
	data.getBytes(tmp_bytes);

	if (tmp_bytes.getCount())
	{
		const unsigned int n = (tmp_bytes[0] == 0) ? 1u : 0;

		sha1->process(tmp_bytes.getCount() - n, tmp_bytes.begin() + n);
	}
}//helper__processStrippedInt

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
