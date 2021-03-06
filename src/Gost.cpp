#include <string.h>
#include <inttypes.h>
#include <array>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include "Gost.h"

namespace i2p
{
namespace crypto
{

// GOST R 34.10

	GOSTR3410Curve::GOSTR3410Curve (BIGNUM * a, BIGNUM * b, BIGNUM * p, BIGNUM * q, BIGNUM * x, BIGNUM * y)
	{
		m_KeyLen = BN_num_bytes (p);
		BN_CTX * ctx = BN_CTX_new ();
		m_Group = EC_GROUP_new_curve_GFp (p, a, b, ctx);
		EC_POINT * P = EC_POINT_new (m_Group);
		EC_POINT_set_affine_coordinates_GFp (m_Group, P, x, y, ctx);
		EC_GROUP_set_generator (m_Group, P, q, nullptr);
		EC_GROUP_set_curve_name (m_Group, NID_id_GostR3410_2001);
		EC_POINT_free(P);
		BN_CTX_free (ctx);
	}

	GOSTR3410Curve::~GOSTR3410Curve ()
	{
		EC_GROUP_free (m_Group);
	}				

	EC_POINT * GOSTR3410Curve::MulP (const BIGNUM * n) const
	{
		BN_CTX * ctx = BN_CTX_new ();
		auto p = EC_POINT_new (m_Group);
		EC_POINT_mul (m_Group, p, n, nullptr, nullptr, ctx);
		BN_CTX_free (ctx);
		return p;
	}

	bool GOSTR3410Curve::GetXY (const EC_POINT * p, BIGNUM * x, BIGNUM * y) const
	{
		return EC_POINT_get_affine_coordinates_GFp (m_Group, p, x, y, nullptr);
	}

	EC_POINT * GOSTR3410Curve::CreatePoint (const BIGNUM * x, const BIGNUM * y) const
	{
		EC_POINT * p = EC_POINT_new (m_Group);
		EC_POINT_set_affine_coordinates_GFp (m_Group, p, x, y, nullptr);
		return p;
	}

	void GOSTR3410Curve::Sign (const BIGNUM * priv, const BIGNUM * digest, BIGNUM * r, BIGNUM * s)
	{
		BN_CTX * ctx = BN_CTX_new ();
		BN_CTX_start (ctx);
		BIGNUM * q = BN_CTX_get (ctx);
		EC_GROUP_get_order(m_Group, q, ctx);
		BIGNUM * k = BN_CTX_get (ctx);
		BN_rand_range (k, q); // 0 < k < q
		EC_POINT * C = MulP (k); // C = k*P
		GetXY (C, r, nullptr); // r = Cx
		EC_POINT_free (C);
		BN_mod_mul (s, r, priv, q, ctx); // (r*priv)%q
		BIGNUM * tmp = BN_CTX_get (ctx);
		BN_mod_mul (tmp, k, digest, q, ctx); // (k*digest)%q
		BN_mod_add (s, s, tmp, q, ctx); // (r*priv+k*digest)%q
		BN_CTX_end (ctx);
		BN_CTX_free (ctx);
	}

	bool GOSTR3410Curve::Verify (const EC_POINT * pub, const BIGNUM * digest, const BIGNUM * r, const BIGNUM * s)
	{
		BN_CTX * ctx = BN_CTX_new ();
		BN_CTX_start (ctx);
		BIGNUM * q = BN_CTX_get (ctx);
		EC_GROUP_get_order(m_Group, q, ctx);
		BIGNUM * h = BN_CTX_get (ctx);
		BN_mod (h, digest, q, ctx); // h = digest % q
		BN_mod_inverse (h, h, q, ctx); // 1/h mod q
		BIGNUM * z1 = BN_CTX_get (ctx);
		BN_mod_mul (z1, s, h, q, ctx); // z1 = s/h
		BIGNUM * z2 = BN_CTX_get (ctx);				
		BN_sub (z2, q, r); // z2 = -r
		BN_mod_mul (z2, z2, h, q, ctx); // z2 = -r/h
		EC_POINT * C = EC_POINT_new (m_Group);
		EC_POINT_mul (m_Group, C, z1, pub, z2, ctx); // z1*P + z2*pub
		BIGNUM * x = BN_CTX_get (ctx);	
		GetXY  (C, x, nullptr); // Cx
		BN_mod (x, x, q, ctx); // Cx % q
		bool ret = !BN_cmp (x, r); // Cx = r ?
		EC_POINT_free (C);
		BN_CTX_end (ctx);
		BN_CTX_free (ctx);
		return ret;
	}	

	EC_POINT * GOSTR3410Curve::RecoverPublicKey (const BIGNUM * digest, const BIGNUM * r, const BIGNUM * s, bool isNegativeY) const 
	{
		// s*P = r*Q + h*C
		BN_CTX * ctx = BN_CTX_new ();
		BN_CTX_start (ctx);
		EC_POINT * C = EC_POINT_new (m_Group); // C = k*P = (rx, ry)
		EC_POINT * Q  = nullptr;
		if (EC_POINT_set_compressed_coordinates_GFp (m_Group, C, r, isNegativeY ? 1 : 0,  ctx))
		{	
			EC_POINT * S = EC_POINT_new (m_Group); // S = s*P
			EC_POINT_mul (m_Group, S, s, nullptr, nullptr, ctx);
			BIGNUM * q = BN_CTX_get (ctx);
			EC_GROUP_get_order(m_Group, q, ctx);
			BIGNUM * h = BN_CTX_get (ctx);
			BN_mod (h, digest, q, ctx); // h = digest % q
			BN_sub (h, q, h); // h = -h
			EC_POINT * H = EC_POINT_new (m_Group); 
			EC_POINT_mul (m_Group, H, nullptr, C, h, ctx); // -h*C
			EC_POINT_add (m_Group, C, S, H, ctx); // s*P - h*C
			EC_POINT_free (H);
			EC_POINT_free (S);
			BIGNUM * r1 = BN_CTX_get (ctx);
			BN_mod_inverse (r1, r, q, ctx);
			Q = EC_POINT_new (m_Group); 
			EC_POINT_mul (m_Group, Q, nullptr, C, r1, ctx); // (s*P - h*C)/r 
		}	
		EC_POINT_free (C);
		BN_CTX_end (ctx);
		BN_CTX_free (ctx);
		return Q;
	}	
	
	static GOSTR3410Curve * CreateGOSTR3410Curve (GOSTR3410ParamSet paramSet)
	{
		// a, b, p, q, x, y	
		static const char * params[eGOSTR3410NumParamSets][6] = 
		{
			{ 
				"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFD94",
				"A6",
				"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFD97",
				"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF6C611070995AD10045841B09B761B893",
				"1",
				"8D91E471E0989CDA27DF505A453F2B7635294F2DDF23E3B122ACC99C9E9F1E14"
			}, // CryptoPro A
			{
				"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFDC4",
				"E8C2505DEDFC86DDC1BD0B2B6667F1DA34B82574761CB0E879BD081CFD0B6265EE3CB090F30D27614CB4574010DA90DD862EF9D4EBEE4761503190785A71C760",
				"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFDC7",
				"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF27E69532F48D89116FF22B8D4E0560609B4B38ABFAD2B85DCACDB1411F10B275",
				"3",
				"7503CFE87A836AE3A61B8816E25450E6CE5E1C93ACF1ABC1778064FDCBEFA921DF1626BE4FD036E93D75E6A50E3A41E98028FE5FC235F5B889A589CB5215F2A4"		
			} // tc26-2012-paramSetA-512
		};	
		
		BIGNUM * a = nullptr, * b = nullptr, * p = nullptr, * q =nullptr, * x = nullptr, * y = nullptr;
		BN_hex2bn(&a, params[paramSet][0]);
		BN_hex2bn(&b, params[paramSet][1]);
		BN_hex2bn(&p, params[paramSet][2]);
		BN_hex2bn(&q, params[paramSet][3]);
		BN_hex2bn(&x, params[paramSet][4]);
		BN_hex2bn(&y, params[paramSet][5]);
		auto curve = new GOSTR3410Curve (a, b, p, q, x, y);
		BN_free (a); BN_free (b); BN_free (p); BN_free (q); BN_free (x); BN_free (y);
		return curve;
	}	

	static std::array<std::unique_ptr<GOSTR3410Curve>, eGOSTR3410NumParamSets> g_GOSTR3410Curves;
	std::unique_ptr<GOSTR3410Curve>& GetGOSTR3410Curve (GOSTR3410ParamSet paramSet)
	{
		if (!g_GOSTR3410Curves[paramSet])
		{
			auto c = CreateGOSTR3410Curve (paramSet);	
			if (!g_GOSTR3410Curves[paramSet]) // make sure it was not created already
				g_GOSTR3410Curves[paramSet].reset (c);
			else
				delete c;
		}	
		return g_GOSTR3410Curves[paramSet]; 
	}

// ГОСТ 34.11-2012

	static const uint8_t sbox_[256] =
	{
		0xFC, 0xEE, 0xDD, 0x11, 0xCF, 0x6E, 0x31, 0x16, 0xFB, 0xC4, 0xFA, 0xDA, 0x23, 0xC5, 0x04, 0x4D,
		0xE9, 0x77, 0xF0, 0xDB, 0x93, 0x2E, 0x99, 0xBA, 0x17, 0x36, 0xF1, 0xBB, 0x14, 0xCD, 0x5F, 0xC1,
		0xF9, 0x18, 0x65, 0x5A, 0xE2, 0x5C, 0xEF, 0x21, 0x81, 0x1C, 0x3C, 0x42, 0x8B, 0x01, 0x8E, 0x4F,
		0x05, 0x84, 0x02, 0xAE, 0xE3, 0x6A, 0x8F, 0xA0, 0x06, 0x0B, 0xED, 0x98, 0x7F, 0xD4, 0xD3, 0x1F,
		0xEB, 0x34, 0x2C, 0x51, 0xEA, 0xC8, 0x48, 0xAB, 0xF2, 0x2A, 0x68, 0xA2, 0xFD, 0x3A, 0xCE, 0xCC,
		0xB5, 0x70, 0x0E, 0x56, 0x08, 0x0C, 0x76, 0x12, 0xBF, 0x72, 0x13, 0x47, 0x9C, 0xB7, 0x5D, 0x87,
		0x15, 0xA1, 0x96, 0x29, 0x10, 0x7B, 0x9A, 0xC7, 0xF3, 0x91, 0x78, 0x6F, 0x9D, 0x9E, 0xB2, 0xB1,
		0x32, 0x75, 0x19, 0x3D, 0xFF, 0x35, 0x8A, 0x7E, 0x6D, 0x54, 0xC6, 0x80, 0xC3, 0xBD, 0x0D, 0x57,
		0xDF, 0xF5, 0x24, 0xA9, 0x3E, 0xA8, 0x43, 0xC9, 0xD7, 0x79, 0xD6, 0xF6, 0x7C, 0x22, 0xB9, 0x03,
		0xE0, 0x0F, 0xEC, 0xDE, 0x7A, 0x94, 0xB0, 0xBC, 0xDC, 0xE8, 0x28, 0x50, 0x4E, 0x33, 0x0A, 0x4A,
		0xA7, 0x97, 0x60, 0x73, 0x1E, 0x00, 0x62, 0x44, 0x1A, 0xB8, 0x38, 0x82, 0x64, 0x9F, 0x26, 0x41,
		0xAD, 0x45, 0x46, 0x92, 0x27, 0x5E, 0x55, 0x2F, 0x8C, 0xA3, 0xA5, 0x7D, 0x69, 0xD5, 0x95, 0x3B,
		0x07, 0x58, 0xB3, 0x40, 0x86, 0xAC, 0x1D, 0xF7, 0x30, 0x37, 0x6B, 0xE4, 0x88, 0xD9, 0xE7, 0x89,
		0xE1, 0x1B, 0x83, 0x49, 0x4C, 0x3F, 0xF8, 0xFE, 0x8D, 0x53, 0xAA, 0x90, 0xCA, 0xD8, 0x85, 0x61,
		0x20, 0x71, 0x67, 0xA4, 0x2D, 0x2B, 0x09, 0x5B, 0xCB, 0x9B, 0x25, 0xD0, 0xBE, 0xE5, 0x6C, 0x52,
		0x59, 0xA6, 0x74, 0xD2, 0xE6, 0xF4, 0xB4, 0xC0, 0xD1, 0x66, 0xAF, 0xC2, 0x39, 0x4B, 0x63, 0xB6
	};

	static const uint64_t A_[64] = 
	{
		0x8e20faa72ba0b470, 0x47107ddd9b505a38, 0xad08b0e0c3282d1c, 0xd8045870ef14980e,
		0x6c022c38f90a4c07, 0x3601161cf205268d, 0x1b8e0b0e798c13c8, 0x83478b07b2468764,
		0xa011d380818e8f40, 0x5086e740ce47c920, 0x2843fd2067adea10, 0x14aff010bdd87508,
		0x0ad97808d06cb404, 0x05e23c0468365a02, 0x8c711e02341b2d01, 0x46b60f011a83988e,
		0x90dab52a387ae76f, 0x486dd4151c3dfdb9, 0x24b86a840e90f0d2, 0x125c354207487869,
		0x092e94218d243cba, 0x8a174a9ec8121e5d, 0x4585254f64090fa0, 0xaccc9ca9328a8950,
		0x9d4df05d5f661451, 0xc0a878a0a1330aa6, 0x60543c50de970553, 0x302a1e286fc58ca7,
		0x18150f14b9ec46dd, 0x0c84890ad27623e0, 0x0642ca05693b9f70, 0x0321658cba93c138,
		0x86275df09ce8aaa8, 0x439da0784e745554, 0xafc0503c273aa42a, 0xd960281e9d1d5215,
		0xe230140fc0802984, 0x71180a8960409a42, 0xb60c05ca30204d21, 0x5b068c651810a89e,
		0x456c34887a3805b9, 0xac361a443d1c8cd2, 0x561b0d22900e4669, 0x2b838811480723ba,
		0x9bcf4486248d9f5d, 0xc3e9224312c8c1a0, 0xeffa11af0964ee50, 0xf97d86d98a327728,
		0xe4fa2054a80b329c, 0x727d102a548b194e, 0x39b008152acb8227, 0x9258048415eb419d,
		0x492c024284fbaec0, 0xaa16012142f35760, 0x550b8e9e21f7a530, 0xa48b474f9ef5dc18,
		0x70a6a56e2440598e, 0x3853dc371220a247, 0x1ca76e95091051ad, 0x0edd37c48a08a6d8,
		0x07e095624504536c, 0x8d70c431ac02a736, 0xc83862965601dd1b, 0x641c314b2b8ee083
	}; 

	static const uint8_t C_[12][64] = 
	{
		{
			0xb1,0x08,0x5b,0xda,0x1e,0xca,0xda,0xe9,0xeb,0xcb,0x2f,0x81,0xc0,0x65,0x7c,0x1f,
			0x2f,0x6a,0x76,0x43,0x2e,0x45,0xd0,0x16,0x71,0x4e,0xb8,0x8d,0x75,0x85,0xc4,0xfc,
			0x4b,0x7c,0xe0,0x91,0x92,0x67,0x69,0x01,0xa2,0x42,0x2a,0x08,0xa4,0x60,0xd3,0x15,
			0x05,0x76,0x74,0x36,0xcc,0x74,0x4d,0x23,0xdd,0x80,0x65,0x59,0xf2,0xa6,0x45,0x07
		},
		{
			0x6f,0xa3,0xb5,0x8a,0xa9,0x9d,0x2f,0x1a,0x4f,0xe3,0x9d,0x46,0x0f,0x70,0xb5,0xd7,
			0xf3,0xfe,0xea,0x72,0x0a,0x23,0x2b,0x98,0x61,0xd5,0x5e,0x0f,0x16,0xb5,0x01,0x31,
			0x9a,0xb5,0x17,0x6b,0x12,0xd6,0x99,0x58,0x5c,0xb5,0x61,0xc2,0xdb,0x0a,0xa7,0xca,
			0x55,0xdd,0xa2,0x1b,0xd7,0xcb,0xcd,0x56,0xe6,0x79,0x04,0x70,0x21,0xb1,0x9b,0xb7
		},
		{
			0xf5,0x74,0xdc,0xac,0x2b,0xce,0x2f,0xc7,0x0a,0x39,0xfc,0x28,0x6a,0x3d,0x84,0x35,
			0x06,0xf1,0x5e,0x5f,0x52,0x9c,0x1f,0x8b,0xf2,0xea,0x75,0x14,0xb1,0x29,0x7b,0x7b,
			0xd3,0xe2,0x0f,0xe4,0x90,0x35,0x9e,0xb1,0xc1,0xc9,0x3a,0x37,0x60,0x62,0xdb,0x09,
			0xc2,0xb6,0xf4,0x43,0x86,0x7a,0xdb,0x31,0x99,0x1e,0x96,0xf5,0x0a,0xba,0x0a,0xb2
		},
		{
			0xef,0x1f,0xdf,0xb3,0xe8,0x15,0x66,0xd2,0xf9,0x48,0xe1,0xa0,0x5d,0x71,0xe4,0xdd,
			0x48,0x8e,0x85,0x7e,0x33,0x5c,0x3c,0x7d,0x9d,0x72,0x1c,0xad,0x68,0x5e,0x35,0x3f,
			0xa9,0xd7,0x2c,0x82,0xed,0x03,0xd6,0x75,0xd8,0xb7,0x13,0x33,0x93,0x52,0x03,0xbe,
			0x34,0x53,0xea,0xa1,0x93,0xe8,0x37,0xf1,0x22,0x0c,0xbe,0xbc,0x84,0xe3,0xd1,0x2e
		},
		{
			0x4b,0xea,0x6b,0xac,0xad,0x47,0x47,0x99,0x9a,0x3f,0x41,0x0c,0x6c,0xa9,0x23,0x63,
			0x7f,0x15,0x1c,0x1f,0x16,0x86,0x10,0x4a,0x35,0x9e,0x35,0xd7,0x80,0x0f,0xff,0xbd,
			0xbf,0xcd,0x17,0x47,0x25,0x3a,0xf5,0xa3,0xdf,0xff,0x00,0xb7,0x23,0x27,0x1a,0x16,
			0x7a,0x56,0xa2,0x7e,0xa9,0xea,0x63,0xf5,0x60,0x17,0x58,0xfd,0x7c,0x6c,0xfe,0x57
		},
		{
			0xae,0x4f,0xae,0xae,0x1d,0x3a,0xd3,0xd9,0x6f,0xa4,0xc3,0x3b,0x7a,0x30,0x39,0xc0,
			0x2d,0x66,0xc4,0xf9,0x51,0x42,0xa4,0x6c,0x18,0x7f,0x9a,0xb4,0x9a,0xf0,0x8e,0xc6,
			0xcf,0xfa,0xa6,0xb7,0x1c,0x9a,0xb7,0xb4,0x0a,0xf2,0x1f,0x66,0xc2,0xbe,0xc6,0xb6,
			0xbf,0x71,0xc5,0x72,0x36,0x90,0x4f,0x35,0xfa,0x68,0x40,0x7a,0x46,0x64,0x7d,0x6e
		},
		{
			0xf4,0xc7,0x0e,0x16,0xee,0xaa,0xc5,0xec,0x51,0xac,0x86,0xfe,0xbf,0x24,0x09,0x54,
			0x39,0x9e,0xc6,0xc7,0xe6,0xbf,0x87,0xc9,0xd3,0x47,0x3e,0x33,0x19,0x7a,0x93,0xc9,
			0x09,0x92,0xab,0xc5,0x2d,0x82,0x2c,0x37,0x06,0x47,0x69,0x83,0x28,0x4a,0x05,0x04,
			0x35,0x17,0x45,0x4c,0xa2,0x3c,0x4a,0xf3,0x88,0x86,0x56,0x4d,0x3a,0x14,0xd4,0x93
		},
		{
			0x9b,0x1f,0x5b,0x42,0x4d,0x93,0xc9,0xa7,0x03,0xe7,0xaa,0x02,0x0c,0x6e,0x41,0x41,
			0x4e,0xb7,0xf8,0x71,0x9c,0x36,0xde,0x1e,0x89,0xb4,0x44,0x3b,0x4d,0xdb,0xc4,0x9a,
			0xf4,0x89,0x2b,0xcb,0x92,0x9b,0x06,0x90,0x69,0xd1,0x8d,0x2b,0xd1,0xa5,0xc4,0x2f,
			0x36,0xac,0xc2,0x35,0x59,0x51,0xa8,0xd9,0xa4,0x7f,0x0d,0xd4,0xbf,0x02,0xe7,0x1e
		},
		{
			0x37,0x8f,0x5a,0x54,0x16,0x31,0x22,0x9b,0x94,0x4c,0x9a,0xd8,0xec,0x16,0x5f,0xde,
			0x3a,0x7d,0x3a,0x1b,0x25,0x89,0x42,0x24,0x3c,0xd9,0x55,0xb7,0xe0,0x0d,0x09,0x84,
			0x80,0x0a,0x44,0x0b,0xdb,0xb2,0xce,0xb1,0x7b,0x2b,0x8a,0x9a,0xa6,0x07,0x9c,0x54,
			0x0e,0x38,0xdc,0x92,0xcb,0x1f,0x2a,0x60,0x72,0x61,0x44,0x51,0x83,0x23,0x5a,0xdb
		},
		{
			0xab,0xbe,0xde,0xa6,0x80,0x05,0x6f,0x52,0x38,0x2a,0xe5,0x48,0xb2,0xe4,0xf3,0xf3,
			0x89,0x41,0xe7,0x1c,0xff,0x8a,0x78,0xdb,0x1f,0xff,0xe1,0x8a,0x1b,0x33,0x61,0x03,
			0x9f,0xe7,0x67,0x02,0xaf,0x69,0x33,0x4b,0x7a,0x1e,0x6c,0x30,0x3b,0x76,0x52,0xf4,
			0x36,0x98,0xfa,0xd1,0x15,0x3b,0xb6,0xc3,0x74,0xb4,0xc7,0xfb,0x98,0x45,0x9c,0xed
		},
		{
			0x7b,0xcd,0x9e,0xd0,0xef,0xc8,0x89,0xfb,0x30,0x02,0xc6,0xcd,0x63,0x5a,0xfe,0x94,
			0xd8,0xfa,0x6b,0xbb,0xeb,0xab,0x07,0x61,0x20,0x01,0x80,0x21,0x14,0x84,0x66,0x79,
			0x8a,0x1d,0x71,0xef,0xea,0x48,0xb9,0xca,0xef,0xba,0xcd,0x1d,0x7d,0x47,0x6e,0x98,
			0xde,0xa2,0x59,0x4a,0xc0,0x6f,0xd8,0x5d,0x6b,0xca,0xa4,0xcd,0x81,0xf3,0x2d,0x1b
		},
		{
			0x37,0x8e,0xe7,0x67,0xf1,0x16,0x31,0xba,0xd2,0x13,0x80,0xb0,0x04,0x49,0xb1,0x7a,
			0xcd,0xa4,0x3c,0x32,0xbc,0xdf,0x1d,0x77,0xf8,0x20,0x12,0xd4,0x30,0x21,0x9f,0x9b,
			0x5d,0x80,0xef,0x9d,0x18,0x91,0xcc,0x86,0xe7,0x1d,0xa4,0xaa,0x88,0xe1,0x28,0x52,
			0xfa,0xf4,0x17,0xd5,0xd9,0xb2,0x1b,0x99,0x48,0xbc,0x92,0x4a,0xf1,0x1b,0xd7,0x20
		}
	};
	
	union GOST3411Block // 8 bytes aligned
	{
		uint8_t buf[64];
		uint64_t ll[8];

		GOST3411Block operator^(const GOST3411Block& other) const
		{
			GOST3411Block ret;
			for (int i = 0; i < 8; i++)
				ret.ll[i] = ll[i]^other.ll[i];
			return ret;
		}	

		GOST3411Block operator^(const uint8_t * other) const
		{
			GOST3411Block ret;
			for (int i = 0; i < 64; i++)
				ret.buf[i] = buf[i]^other[i];
			return ret;
		}	
		
		GOST3411Block operator+(const GOST3411Block& other) const
		{
			GOST3411Block ret;
			uint8_t carry = 0;
			for (int i = 63; i >= 0; i--)
			{
				uint16_t sum = buf[i] + other.buf[i] + carry;
				ret.buf[i] = sum & 0xFF;
				carry = sum >> 8;
			}	
			return ret;
		}

		void Add (uint32_t c)
		{
			for (int i = 63; i >= 0; i--)
			{
				c += buf[i];
				buf[i] = c;
				c >>= 8;			
			}
		}

		void SPL ()
		{
			uint8_t p[64];
			memcpy (p, buf, 64); // we need to copy it for P's transposition
			for (int i = 0; i < 8; i++)
			{
				uint64_t c = 0;
				for (int j = 0; j < 8; j++)
				{	
					uint8_t bit = 0x80;
					uint8_t byte = sbox_[p[j*8+i]]; // S - sbox_, P - transpose (i,j)
					for (int k = 0; k < 8; k++)
					{
						if (byte & bit) c ^= A_[j*8+k];
						bit >>= 1;
					}
				}	
#ifdef WIN32
				for (int k = 0; k < 8; k++)
					buf[k] = ((uint8_t *)&c)[7-k];			
#else
				ll[i] = htobe64 (c); // TODO:	
#endif
			}	
		}	

		GOST3411Block E (const GOST3411Block& m)
		{
			GOST3411Block k = *this;
			GOST3411Block res = k^m;
			for (int i = 0; i < 12; i++)
			{
				res.SPL ();
				k = k^C_[i];
				k.SPL ();
				res = k^res;
			}	
			return res;
		}	
	};
	
	static GOST3411Block gN (const GOST3411Block& N, const GOST3411Block& h, const GOST3411Block& m)
	{
		GOST3411Block res = N ^ h;
		res.SPL ();
		res = res.E (m);
		res = res^h;
		res = res^m;
		return res;	
	}	

	static void H (const uint8_t * iv, const uint8_t * buf, size_t len, uint8_t * digest)
	{
		// stage 1	
		GOST3411Block h, N, s, m;
		memcpy (h.buf, iv, 64);
		memset (N.buf, 0, 64);
		memset (s.buf, 0, 64);
		size_t l = len;
		// stage 2
		while (l >= 64)
		{
			memcpy (m.buf, buf + l - 64, 64); // TODO
			h= gN (N, h, m);
			N.Add (512);
			s = m + s;
			l -= 64;
		}
		// stage 3
		size_t padding = 64 - l;
		if (padding)
		{
			memset (m.buf, 0, padding - 1);
			m.buf[padding - 1] = 1;
		}
		memcpy (m.buf + padding, buf, l);

		h = gN (N, h, m);
		N.Add (l*8);	
		s = m + s;
	
		GOST3411Block N0;
		memset (N0.buf, 0, 64);
		h = gN (N0, h, N);
		h = gN (N0, h, s);
		
		memcpy (digest, h.buf, 64); 
	}

	void GOSTR3411_2012_256 (const uint8_t * buf, size_t len, uint8_t * digest)
	{
		uint8_t iv[64];
		memset (iv, 1, 64);
		uint8_t h[64];
		H (iv, buf, len, h);
		memcpy (digest, h, 32); // first half
	}

	void GOSTR3411_2012_512 (const uint8_t * buf, size_t len, uint8_t * digest)
	{
		uint8_t iv[64];
		memset (iv, 0, 64);
		H (iv, buf, len, digest);
	}

	// reverse order
	struct GOSTR3411_2012_CTX
	{
		GOST3411Block h, N, s, m;
		size_t len;
		bool is512;
	};
	
	GOSTR3411_2012_CTX * GOSTR3411_2012_CTX_new ()
	{
		return new GOSTR3411_2012_CTX;
	}

	void GOSTR3411_2012_CTX_free (GOSTR3411_2012_CTX * ctx)
	{
		delete ctx;
	}

	void GOSTR3411_2012_CTX_Init (GOSTR3411_2012_CTX * ctx, bool is512)
	{
		uint8_t iv[64];
		memset (iv, is512 ? 0 : 1, 64);
		memcpy (ctx->h.buf, iv, 64);
		memset (ctx->N.buf, 0, 64);
		memset (ctx->s.buf, 0, 64);
		ctx->len = 0;
		ctx->is512 = is512;
	}

	void GOSTR3411_2012_CTX_Update (const uint8_t * buf, size_t len, GOSTR3411_2012_CTX * ctx)
	{
		if (!len) return;
		if (ctx->len > 0) // something left from buffer
		{
			size_t l = 64 - ctx->len;
			if (len < l) l = len;
			for (size_t i = 0; i < l; i++)
				ctx->m.buf[ctx->len + i] = buf[l-i-1]; // invert	 
			ctx->len += l; len -= l; buf += l;

			ctx->h = gN (ctx->N, ctx->h, ctx->m);
			ctx->N.Add (512);
			ctx->s = ctx->m + ctx->s;
		}
		while (len >= 64)
		{
			for (size_t i = 0; i < 64; i++)
				ctx->m.buf[i] = buf[63-i]; // invert	
			len -= 64; buf += 64;
			ctx->h = gN (ctx->N, ctx->h, ctx->m);
			ctx->N.Add (512);
			ctx->s = ctx->m + ctx->s;
		}
		if (len > 0) // carry remaining
		{
			for (size_t i = 0; i < len; i++)
				ctx->m.buf[i] = buf[len-i-1]; // invert
		}
		ctx->len = len;
	}

	void GOSTR3411_2012_CTX_Finish (uint8_t * digest, GOSTR3411_2012_CTX * ctx)
	{
		GOST3411Block m;	
		size_t padding = 64 - ctx->len;
		if (padding)
		{
			memset (m.buf, 0, padding - 1);
			m.buf[padding - 1] = 1;
		}
		memcpy (m.buf + padding, ctx->m.buf, ctx->len);

		ctx->h = gN (ctx->N, ctx->h, m);
		ctx->N.Add (ctx->len*8);	
		ctx->s = m + ctx->s;
	
		GOST3411Block N0;
		memset (N0.buf, 0, 64);
		ctx->h = gN (N0, ctx->h, ctx->N);
		ctx->h = gN (N0, ctx->h, ctx->s);
		
		size_t sz = ctx->is512 ? 64 : 32;
		for (size_t i = 0; i < sz; i++)
			digest[i] = ctx->h.buf[sz - i - 1];
	}

}
}
