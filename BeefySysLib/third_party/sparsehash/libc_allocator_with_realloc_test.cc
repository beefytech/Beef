// Copyright (c) 2010, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---

#include <sparsehash/internal/sparseconfig.h>
#include <config.h>
#include <sparsehash/internal/libc_allocator_with_realloc.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <iostream>
#include "testutil.h"

using std::cerr;
using std::cout;
using std::string;
using std::basic_string;
using std::char_traits;
using std::vector;
using GOOGLE_NAMESPACE::libc_allocator_with_realloc;

#define arraysize(a)  ( sizeof(a) / sizeof(*(a)) )

namespace {

typedef libc_allocator_with_realloc<int> int_alloc;
typedef int_alloc::rebind<int*>::other intp_alloc;

// cstring allocates from libc_allocator_with_realloc.
typedef basic_string<char, char_traits<char>,
                     libc_allocator_with_realloc<char> > cstring;
typedef vector<cstring, libc_allocator_with_realloc<cstring> > cstring_vector;

TEST(LibcAllocatorWithReallocTest, Allocate) {
  int_alloc alloc;
  intp_alloc palloc;

  int** parray = palloc.allocate(1024);
  for (int i = 0; i < 16; ++i) {
    parray[i] = alloc.allocate(i * 1024 + 1);
  }
  for (int i = 0; i < 16; ++i) {
    alloc.deallocate(parray[i], i * 1024 + 1);
  }
  palloc.deallocate(parray, 1024);

  int* p = alloc.allocate(4096);
  p[0] = 1;
  p[1023] = 2;
  p[4095] = 3;
  p = alloc.reallocate(p, 8192);
  EXPECT_EQ(1, p[0]);
  EXPECT_EQ(2, p[1023]);
  EXPECT_EQ(3, p[4095]);
  p = alloc.reallocate(p, 1024);
  EXPECT_EQ(1, p[0]);
  EXPECT_EQ(2, p[1023]);
  alloc.deallocate(p, 1024);
}

TEST(LibcAllocatorWithReallocTest, TestSTL) {
  // Test strings copied from base/arena_unittest.cc
  static const char* test_strings[] = {
    "aback", "abaft", "abandon", "abandoned", "abandoning",
    "abandonment", "abandons", "abase", "abased", "abasement",
    "abasements", "abases", "abash", "abashed", "abashes", "abashing",
    "abasing", "abate", "abated", "abatement", "abatements", "abater",
    "abates", "abating", "abbe", "abbey", "abbeys", "abbot", "abbots",
    "abbreviate", "abbreviated", "abbreviates", "abbreviating",
    "abbreviation", "abbreviations", "abdomen", "abdomens", "abdominal",
    "abduct", "abducted", "abduction", "abductions", "abductor", "abductors",
    "abducts", "Abe", "abed", "Abel", "Abelian", "Abelson", "Aberdeen",
    "Abernathy", "aberrant", "aberration", "aberrations", "abet", "abets",
    "abetted", "abetter", "abetting", "abeyance", "abhor", "abhorred",
    "abhorrent", "abhorrer", "abhorring", "abhors", "abide", "abided",
    "abides", "abiding"};
  cstring_vector v;
  for (size_t i = 0; i < arraysize(test_strings); ++i) {
    v.push_back(test_strings[i]);
  }
  for (size_t i = arraysize(test_strings); i > 0; --i) {
    EXPECT_EQ(cstring(test_strings[i-1]), v.back());
    v.pop_back();
  }
}

}  // namespace

int main(int, char **) {
  // All the work is done in the static constructors.  If they don't
  // die, the tests have all passed.
  cout << "PASS\n";
  return 0;
}

