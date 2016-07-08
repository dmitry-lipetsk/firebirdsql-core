#include "../common/BigInteger.h"
#include "../common/classes/alloc.h"
#include "../common/classes/fb_string.h"
#include "../common/sha.h"

#define SRP_DEBUG 0		// >0 - prints some debug info
						// >1 - uses consts instead randoms, NEVER use in PRODUCTION!

// for HANDSHAKE_DEBUG
#include "../remote/remot_proto.h"

namespace Auth{
////////////////////////////////////////////////////////////////////////////////

/*
 * Order of battle for SRP handshake:
 *
 * 													0.  At account creation, the server generates
 * 														a random salt and computes a password
 * 														verifier from the account name, password,
 * 														and salt.
*
 * 		1. Client generates random number
 * 		   as private key, computes public
 * 		   key.
 *
 * 		2. Client sends server the account
 * 		   name and its public key.
 * 													3.  Server receives account name, looks up
 * 														salt and password verifier.  Server
 * 														generates random number as private key.
 * 														Server computes public key from private
 * 														key, account name, verifier, and salt.
 *
 * 													4.  Server sends client public key and salt
 *
 * 		3. Client receives server public
 * 		   key and computes session key
 * 		   from server key, salt, account
 * 		   name, and password.
  * 												5.  Server computes session key from client
 * 														public key, client name, and verifier
 *
 * 		For full details, see http://www.ietf.org/rfc/rfc5054.txt
 *
 */

////////////////////////////////////////////////////////////////////////////////
//class RemotePassword

class RemotePassword : public Firebird::GlobalStorage
{
private:
    typedef RemotePassword                  self_type;

    RemotePassword(const self_type&);
    self_type& operator = (const self_type&);

public:
	static const char* const plugName;
	static const unsigned SRP_KEY_SIZE = 128;
	static const unsigned SRP_VERIFIER_SIZE = SRP_KEY_SIZE;
	static const unsigned SRP_SALT_SIZE = 32;

public:
	Firebird::BigInteger	clientPublicKey;
	Firebird::BigInteger	serverPublicKey;

public:
	RemotePassword();

	Firebird::BigInteger getUserHash(const char* account,
									 const char* salt,
									 const char* password);

	Firebird::BigInteger computeVerifier(const Firebird::string& account,
										 const Firebird::string& salt,
										 const Firebird::string& password);

	void genClientKey(Firebird::string& clientPubKey);

	void genServerKey(Firebird::string&            serverPubKey,
                      const Firebird::UCharBuffer& verifier);

	void computeScramble();

	void clientSessionKey(Firebird::UCharBuffer& sessionKey,
                          const char*            account,
                          const char*            salt,
                          const char*            password,
                          const char*            serverPubKey);

	void serverSessionKey(Firebird::UCharBuffer&       sessionKey,
						  const char*                  clientPubKey,
						  const Firebird::UCharBuffer& verifier);

	Firebird::BigInteger clientProof(const char*                  account,
									 const char*                  salt,
									 const Firebird::UCharBuffer& sessionKey);

private:
	static void helper__getInt(Firebird::UCharBuffer&      tmp_bytes,
							   Firebird::Sha1&             sha1,
							   Firebird::BigInteger* const hash);

	static void helper__processInt(Firebird::UCharBuffer&      tmp_bytes,
							       const Firebird::BigInteger& data,
								   Firebird::Sha1* const       sha1);

	static void helper__processStrippedInt(Firebird::UCharBuffer&      tmp_bytes,
										   const Firebird::BigInteger& data,
										   Firebird::Sha1* const       sha1);

private:
	Firebird::BigInteger     privateKey;
	Firebird::BigInteger     scramble;

    Firebird::BigInteger const prime;
    Firebird::BigInteger const generator;
    Firebird::BigInteger const k;

private:
    static const unsigned char sm_primeStr[];

    static const unsigned char sm_genStr[];

    static const unsigned char sm_kStr[];
};//class RemotePassword

////////////////////////////////////////////////////////////////////////////////

#if SRP_DEBUG > 0
void dumpIt(const char* name, const Firebird::BigInteger& bi);
void dumpIt(const char* name, const Firebird::UCharBuffer& data);
void dumpIt(const char* name, const Firebird::string& str);
void dumpBin(const char* name, const Firebird::string& str);
#else
void static inline dumpIt(const char* /*name*/, const Firebird::BigInteger& /*bi*/) { }
void static inline dumpIt(const char* /*name*/, const Firebird::UCharBuffer& /*data*/) { }
void static inline dumpIt(const char* /*name*/, const Firebird::string& /*str*/) { }
void static inline dumpBin(const char* /*name*/, const Firebird::string& /*str*/) { }
#endif

////////////////////////////////////////////////////////////////////////////////

void checkStatusVectorForMissingTable(const ISC_STATUS* v);

////////////////////////////////////////////////////////////////////////////////
} // namespace Auth
