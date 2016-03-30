////////////////////////////////////////////////////////////////////////////////
// The contents of this file are subject to the Interbase Public
// License Version 1.0 (the "License"); you may not use this file
// except in compliance with the License. You may obtain a copy
// of the License at http://www.Inprise.com/IPL.html
//
// Software distributed under the License is distributed on an
// "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
// or implied. See the License for the specific language governing
// rights and limitations under the License.
//
// The Original Code was created by Inprise Corporation
// and its predecessors. Portions created by Inprise Corporation are
// Copyright (C) Inprise Corporation.
//
// All Rights Reserved.
// Contributor(s): ______________________________________.

#ifndef _fb__enc_crypt__helper_H_
#define _fb__enc_crypt__helper_H_

#include "../jrd/fb__enc_crypt.h"

namespace Firebird{
////////////////////////////////////////////////////////////////////////////////
//Configuration

//
// define "MUST_ALIGN" if your compiler cannot load/store
// long integers at arbitrary (e.g. odd) memory locations.
// (Either that or never pass unaligned addresses to des_cipher!)
//
#define MUST_ALIGN

#ifdef CHAR_BITS
# if CHAR_BITS != 8
#  error C_block structure assumes 8 bit characters
# endif
#endif

////////////////////////////////////////////////////////////////////////////////
// Convert twenty-four-bit long in host-order
// to six bits (and 2 low-order zeroes) per char little-endian format.

#define TO_SIX_BIT(rslt, src)                     \
 {                                                \
  C_block cvt;                                    \
  cvt.b[0] = (unsigned char)src; src >>= 6;       \
  cvt.b[1] = (unsigned char)src; src >>= 6;       \
  cvt.b[2] = (unsigned char)src; src >>= 6;       \
  cvt.b[3] = (unsigned char)src;                  \
  rslt = (cvt.b32.i0 & 0x3f3f3f3fL) << 2;         \
 }

////////////////////////////////////////////////////////////////////////////////
//union FB__ENC_crypt::C_block

union FB__ENC_crypt::C_block
{
 unsigned char b[8];

 struct
 {
  /* long is often faster than a 32-bit bit field */
  SLONG i0;
  SLONG i1;
 } b32;
};//union FB__ENC_crypt::C_block

////////////////////////////////////////////////////////////////////////////////
//struct FB__ENC_crypt::C_block2

struct FB__ENC_crypt::C_block2
{
 struct
 {
  /* long is often faster than a 32-bit bit field */
  SLONG i0;
  SLONG i1;
 } b32;
};//struct FB__ENC_crypt::C_block2

////////////////////////////////////////////////////////////////////////////////
//class FB__ENC_crypt::tag_data

class FB__ENC_crypt::tag_data
{
 public:
  static const char c_PASSWORD_EFMT1='#';

  /* "small data" */
  static const size_t c_LGCHUNKBITS           =2;
  static const size_t c_CHUNKBITS             =(1<<c_LGCHUNKBITS);
  static const size_t c_LEFT_SHIFT_CHUNKBITS  =(1<<c_CHUNKBITS);

 public:
  /* encryption constant */
  static const C_block constdatablock;

 public:
  static const unsigned char Rotates[];

 public:
  static const unsigned char itoa64[];

 public: //Tables that are initialized at run time
  static const unsigned char a64toi[128];   /* ascii-64 => 0..63 */

  /* Initial key schedule permutation */
  static const C_block2 PC1ROT[64 / c_CHUNKBITS][c_LEFT_SHIFT_CHUNKBITS];

  /* Subsequent key schedule rotation permutations */
  static const C_block2 PC2ROT[2][64 / c_CHUNKBITS][c_LEFT_SHIFT_CHUNKBITS];

  /* Initial permutation/expansion table */
  static const C_block2 IE3264[32 / c_CHUNKBITS][c_LEFT_SHIFT_CHUNKBITS];

  /* Table that combines the S, P, and E operations.  */
  static const SLONG SPE[2][8][64];

  /* compressed/interleaved => final permutation table */
  static const C_block2 CF6464[64 / c_CHUNKBITS][c_LEFT_SHIFT_CHUNKBITS];
};//class FB__ENC_crypt::tag_data

////////////////////////////////////////////////////////////////////////////////
//struct FB__ENC_crypt::tag_ctx

struct FB__ENC_crypt::tag_ctx
{
 public:
  static const size_t c_KS_SIZE=16;

  C_block KS[c_KS_SIZE];
};//struct tag_ctx

////////////////////////////////////////////////////////////////////////////////
}/*nms Firebird*/
#endif
