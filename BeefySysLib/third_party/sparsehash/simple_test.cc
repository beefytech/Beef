// Copyright (c) 2007, Google Inc.
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
//
// This tests mostly that we can #include the files correctly
// and have them work.  This unittest purposefully does not
// #include <config.h>; it's meant to emulate what a 'regular
// install' of sparsehash would be able to see.

#include <sparsehash/internal/sparseconfig.h>
#include <config.h>
#include <stdio.h>
#include <sparsehash/sparse_hash_set>
#include <sparsehash/sparse_hash_map>
#include <sparsehash/dense_hash_set>
#include <sparsehash/dense_hash_map>
#include <sparsehash/template_util.h>
#include <sparsehash/type_traits.h>

#define CHECK_IFF(cond, when) do {                                      \
  if (when) {                                                           \
    if (!(cond)) {                                                      \
      puts("ERROR: " #cond " failed when " #when " is true\n");         \
      exit(1);                                                          \
    }                                                                   \
  } else {                                                              \
    if (cond) {                                                         \
      puts("ERROR: " #cond " succeeded when " #when " is false\n");     \
      exit(1);                                                          \
    }                                                                   \
  }                                                                     \
} while (0)

int main(int argc, char**) {
  // Run with an argument to get verbose output
  const bool verbose = argc > 1;

  google::sparse_hash_set<int> sset;
  google::sparse_hash_map<int, int> smap;
  google::dense_hash_set<int> dset;
  google::dense_hash_map<int, int> dmap;
  dset.set_empty_key(-1);
  dmap.set_empty_key(-1);

  for (int i = 0; i < 100; i += 10) {   // go by tens
    sset.insert(i);
    smap[i] = i+1;
    dset.insert(i + 5);
    dmap[i+5] = i+6;
  }

  if (verbose) {
    for (google::sparse_hash_set<int>::const_iterator it = sset.begin();
         it != sset.end(); ++it)
      printf("sset: %d\n", *it);
    for (google::sparse_hash_map<int,int>::const_iterator it = smap.begin();
         it != smap.end(); ++it)
      printf("smap: %d -> %d\n", it->first, it->second);
    for (google::dense_hash_set<int>::const_iterator it = dset.begin();
         it != dset.end(); ++it)
      printf("dset: %d\n", *it);
    for (google::dense_hash_map<int,int>::const_iterator it = dmap.begin();
         it != dmap.end(); ++it)
      printf("dmap: %d -> %d\n", it->first, it->second);
  }

  for (int i = 0; i < 100; i++) {
    CHECK_IFF(sset.find(i) != sset.end(), (i % 10) == 0);
    CHECK_IFF(smap.find(i) != smap.end(), (i % 10) == 0);
    CHECK_IFF(smap.find(i) != smap.end() && smap.find(i)->second == i+1,
              (i % 10) == 0);
    CHECK_IFF(dset.find(i) != dset.end(), (i % 10) == 5);
    CHECK_IFF(dmap.find(i) != dmap.end(), (i % 10) == 5);
    CHECK_IFF(dmap.find(i) != dmap.end() && dmap.find(i)->second == i+1,
              (i % 10) == 5);
  }
  printf("PASS\n");
  return 0;
}
