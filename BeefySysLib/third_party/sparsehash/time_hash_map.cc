// Copyright (c) 2005, Google Inc.
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
// Authors: Sanjay Ghemawat and Craig Silverstein

// Time various hash map implementations
//
// Below, times are per-call.  "Memory use" is "bytes in use by
// application" as reported by tcmalloc, compared before and after the
// function call.  This does not really report fragmentation, which is
// not bad for the sparse* routines but bad for the dense* ones.
//
// The tests generally yield best-case performance because the
// code uses sequential keys; on the other hand, "map_fetch_random" does
// lookups in a pseudorandom order.  Also, "stresshashfunction" is
// a stress test of sorts.  It uses keys from an arithmetic sequence, which,
// if combined with a quick-and-dirty hash function, will yield worse
// performance than the otherwise similar "map_predict/grow."
//
// Consider doing the following to get good numbers:
//
// 1. Run the tests on a machine with no X service. Make sure no other
//    processes are running.
// 2. Minimize compiled-code differences. Compare results from the same
//    binary, if possible, instead of comparing results from two different
//    binaries.
//
// See PERFORMANCE for the output of one example run.

#include <sparsehash/internal/sparseconfig.h>
#include <config.h>
#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif         // for uintptr_t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include <time.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif
#ifdef HAVE_SYS_UTSNAME_H
# include <sys/utsname.h>
#endif      // for uname()
}

// The functions that we call on each map, that differ for different types.
// By default each is a noop, but we redefine them for types that need them.

#include <map>
#include HASH_MAP_H
#include <algorithm>
#include <vector>
#include <sparsehash/type_traits.h>
#include <sparsehash/dense_hash_map>
#include <sparsehash/sparse_hash_map>

using std::map;
using std::swap;
using std::vector;
using GOOGLE_NAMESPACE::dense_hash_map;
using GOOGLE_NAMESPACE::sparse_hash_map;

static bool FLAGS_test_sparse_hash_map = true;
static bool FLAGS_test_dense_hash_map = true;
static bool FLAGS_test_hash_map = true;
static bool FLAGS_test_map = true;

static bool FLAGS_test_4_bytes = true;
static bool FLAGS_test_8_bytes = true;
static bool FLAGS_test_16_bytes = true;
static bool FLAGS_test_256_bytes = true;

#if defined(HAVE_UNORDERED_MAP)
using HASH_NAMESPACE::unordered_map;
#elif defined(HAVE_HASH_MAP) || defined(_MSC_VER)
using HASH_NAMESPACE::hash_map;
#endif

static const int kDefaultIters = 10000000;

// A version of each of the hashtable classes we test, that has been
// augumented to provide a common interface.  For instance, the
// sparse_hash_map and dense_hash_map versions set empty-key and
// deleted-key (we can do this because all our tests use int-like
// keys), so the users don't have to.  The hash_map version adds
// resize(), so users can just call resize() for all tests without
// worrying about whether the map-type supports it or not.

template<typename K, typename V, typename H>
class EasyUseSparseHashMap : public sparse_hash_map<K,V,H> {
 public:
  EasyUseSparseHashMap() {
    this->set_deleted_key(-1);
  }
};

template<typename K, typename V, typename H>
class EasyUseDenseHashMap : public dense_hash_map<K,V,H> {
 public:
  EasyUseDenseHashMap() {
    this->set_empty_key(-1);
    this->set_deleted_key(-2);
  }
};

// For pointers, we only set the empty key.
template<typename K, typename V, typename H>
class EasyUseSparseHashMap<K*, V, H> : public sparse_hash_map<K*,V,H> {
 public:
  EasyUseSparseHashMap() { }
};

template<typename K, typename V, typename H>
class EasyUseDenseHashMap<K*, V, H> : public dense_hash_map<K*,V,H> {
 public:
  EasyUseDenseHashMap() {
    this->set_empty_key((K*)(~0));
  }
};

#if defined(HAVE_UNORDERED_MAP)
template<typename K, typename V, typename H>
class EasyUseHashMap : public unordered_map<K,V,H> {
 public:
  // resize() is called rehash() in tr1
  void resize(size_t r) { this->rehash(r); }
};
#elif defined(_MSC_VER)
template<typename K, typename V, typename H>
class EasyUseHashMap : public hash_map<K,V,H> {
 public:
  void resize(size_t r) { }
};
#elif defined(HAVE_HASH_MAP)
template<typename K, typename V, typename H>
class EasyUseHashMap : public hash_map<K,V,H> {
 public:
  // Don't need to do anything: hash_map is already easy to use!
};
#endif

template<typename K, typename V>
class EasyUseMap : public map<K,V> {
 public:
  void resize(size_t) { }   // map<> doesn't support resize
};


// Returns the number of hashes that have been done since the last
// call to NumHashesSinceLastCall().  This is shared across all
// HashObject instances, which isn't super-OO, but avoids two issues:
// (1) making HashObject bigger than it ought to be (this is very
// important for our testing), and (2) having to pass around
// HashObject objects everywhere, which is annoying.
static int g_num_hashes;
static int g_num_copies;

int NumHashesSinceLastCall() {
  int retval = g_num_hashes;
  g_num_hashes = 0;
  return retval;
}
int NumCopiesSinceLastCall() {
  int retval = g_num_copies;
  g_num_copies = 0;
  return retval;
}

/*
 * These are the objects we hash.  Size is the size of the object
 * (must be > sizeof(int).  Hashsize is how many of these bytes we
 * use when hashing (must be > sizeof(int) and < Size).
 */
template<int Size, int Hashsize> class HashObject {
 public:
  typedef HashObject<Size, Hashsize> class_type;
  HashObject() {}
  HashObject(int i) : i_(i) {
    memset(buffer_, i & 255, sizeof(buffer_));   // a "random" char
  }
  HashObject(const HashObject& that) {
    operator=(that);
  }
  void operator=(const HashObject& that) {
    g_num_copies++;
    this->i_ = that.i_;
    memcpy(this->buffer_, that.buffer_, sizeof(this->buffer_));
  }

  size_t Hash() const {
    g_num_hashes++;
    int hashval = i_;
    for (size_t i = 0; i < Hashsize - sizeof(i_); ++i) {
      hashval += buffer_[i];
    }
    return SPARSEHASH_HASH<int>()(hashval);
  }

  bool operator==(const class_type& that) const { return this->i_ == that.i_; }
  bool operator< (const class_type& that) const { return this->i_ < that.i_; }
  bool operator<=(const class_type& that) const { return this->i_ <= that.i_; }

 private:
  int i_;        // the key used for hashing
  char buffer_[Size - sizeof(int)];
};

// A specialization for the case sizeof(buffer_) == 0
template<> class HashObject<sizeof(int), sizeof(int)> {
 public:
  typedef HashObject<sizeof(int), sizeof(int)> class_type;
  HashObject() {}
  HashObject(int i) : i_(i) {}
  HashObject(const HashObject& that) {
    operator=(that);
  }
  void operator=(const HashObject& that) {
    g_num_copies++;
    this->i_ = that.i_;
  }

  size_t Hash() const {
    g_num_hashes++;
    return SPARSEHASH_HASH<int>()(i_);
  }

  bool operator==(const class_type& that) const { return this->i_ == that.i_; }
  bool operator< (const class_type& that) const { return this->i_ < that.i_; }
  bool operator<=(const class_type& that) const { return this->i_ <= that.i_; }

 private:
  int i_;        // the key used for hashing
};

_START_GOOGLE_NAMESPACE_

// Let the hashtable implementations know it can use an optimized memcpy,
// because the compiler defines both the destructor and copy constructor.

template<int Size, int Hashsize>
struct has_trivial_copy< HashObject<Size, Hashsize> > : true_type { };

template<int Size, int Hashsize>
struct has_trivial_destructor< HashObject<Size, Hashsize> > : true_type { };

_END_GOOGLE_NAMESPACE_

class HashFn {
 public:
  template<int Size, int Hashsize>
  size_t operator()(const HashObject<Size,Hashsize>& obj) const {
    return obj.Hash();
  }
  // Do the identity hash for pointers.
  template<int Size, int Hashsize>
  size_t operator()(const HashObject<Size,Hashsize>* obj) const {
    return reinterpret_cast<uintptr_t>(obj);
  }

  // Less operator for MSVC's hash containers.
  template<int Size, int Hashsize>
  bool operator()(const HashObject<Size,Hashsize>& a,
                  const HashObject<Size,Hashsize>& b) const {
    return a < b;
  }
  template<int Size, int Hashsize>
  bool operator()(const HashObject<Size,Hashsize>* a,
                  const HashObject<Size,Hashsize>* b) const {
    return a < b;
  }
  // These two public members are required by msvc.  4 and 8 are defaults.
  static const size_t bucket_size = 4;
  static const size_t min_buckets = 8;
};

/*
 * Measure resource usage.
 */

class Rusage {
 public:
  /* Start collecting usage */
  Rusage() { Reset(); }

  /* Reset collection */
  void Reset();

  /* Show usage, in seconds */
  double UserTime();

 private:
#if defined HAVE_SYS_RESOURCE_H
  struct rusage start;
#elif defined HAVE_WINDOWS_H
  long long int start;
#else
  time_t start_time_t;
#endif
};

inline void Rusage::Reset() {
  g_num_copies = 0;
  g_num_hashes = 0;
#if defined HAVE_SYS_RESOURCE_H
  getrusage(RUSAGE_SELF, &start);
#elif defined HAVE_WINDOWS_H
  start = GetTickCount();
#else
  time(&start_time_t);
#endif
}

inline double Rusage::UserTime() {
#if defined HAVE_SYS_RESOURCE_H
  struct rusage u;

  getrusage(RUSAGE_SELF, &u);

  struct timeval result;
  result.tv_sec  = u.ru_utime.tv_sec  - start.ru_utime.tv_sec;
  result.tv_usec = u.ru_utime.tv_usec - start.ru_utime.tv_usec;

  return double(result.tv_sec) + double(result.tv_usec) / 1000000.0;
#elif defined HAVE_WINDOWS_H
  return double(GetTickCount() - start) / 1000.0;
#else
  time_t now;
  time(&now);
  return now - start_time_t;
#endif
}

static void print_uname() {
#ifdef HAVE_SYS_UTSNAME_H
  struct utsname u;
  if (uname(&u) == 0) {
    printf("%s %s %s %s %s\n",
           u.sysname, u.nodename, u.release, u.version, u.machine);
  }
#endif
}

// Generate stamp for this run
static void stamp_run(int iters) {
  time_t now = time(0);
  printf("======\n");
  fflush(stdout);
  print_uname();
  printf("Average over %d iterations\n", iters);
  fflush(stdout);
  // don't need asctime_r/gmtime_r: we're not threaded
  printf("Current time (GMT): %s", asctime(gmtime(&now)));
}

// This depends on the malloc implementation for exactly what it does
// -- and thus requires work after the fact to make sense of the
// numbers -- and also is likely thrown off by the memory management
// STL tries to do on its own.

#ifdef HAVE_GOOGLE_MALLOC_EXTENSION_H
#include <google/malloc_extension.h>

static size_t CurrentMemoryUsage() {
  size_t result;
  if (MallocExtension::instance()->GetNumericProperty(
          "generic.current_allocated_bytes",
          &result)) {
    return result;
  } else {
    return 0;
  }
}

#else  /* not HAVE_GOOGLE_MALLOC_EXTENSION_H */
static size_t CurrentMemoryUsage() { return 0; }

#endif

static void report(char const* title, double t,
                   int iters,
                   size_t start_memory, size_t end_memory) {
  // Construct heap growth report text if applicable
  char heap[100] = "";
  if (end_memory > start_memory) {
    snprintf(heap, sizeof(heap), "%7.1f MB",
             (end_memory - start_memory) / 1048576.0);
  }

  printf("%-20s %6.1f ns  (%8d hashes, %8d copies)%s\n",
         title, (t * 1000000000.0 / iters),
         NumHashesSinceLastCall(), NumCopiesSinceLastCall(),
         heap);
  fflush(stdout);
}

template<class MapType>
static void time_map_grow(int iters) {
  MapType set;
  Rusage t;

  const size_t start = CurrentMemoryUsage();
  t.Reset();
  for (int i = 0; i < iters; i++) {
    set[i] = i+1;
  }
  double ut = t.UserTime();
  const size_t finish = CurrentMemoryUsage();
  report("map_grow", ut, iters, start, finish);
}

template<class MapType>
static void time_map_grow_predicted(int iters) {
  MapType set;
  Rusage t;

  const size_t start = CurrentMemoryUsage();
  set.resize(iters);
  t.Reset();
  for (int i = 0; i < iters; i++) {
    set[i] = i+1;
  }
  double ut = t.UserTime();
  const size_t finish = CurrentMemoryUsage();
  report("map_predict/grow", ut, iters, start, finish);
}

template<class MapType>
static void time_map_replace(int iters) {
  MapType set;
  Rusage t;
  int i;

  for (i = 0; i < iters; i++) {
    set[i] = i+1;
  }

  t.Reset();
  for (i = 0; i < iters; i++) {
    set[i] = i+1;
  }
  double ut = t.UserTime();

  report("map_replace", ut, iters, 0, 0);
}

template<class MapType>
static void time_map_fetch(int iters, const vector<int>& indices,
                           char const* title) {
  MapType set;
  Rusage t;
  int r;
  int i;

  for (i = 0; i < iters; i++) {
    set[i] = i+1;
  }

  r = 1;
  t.Reset();
  for (i = 0; i < iters; i++) {
    r ^= static_cast<int>(set.find(indices[i]) != set.end());
  }
  double ut = t.UserTime();

  srand(r);   // keep compiler from optimizing away r (we never call rand())
  report(title, ut, iters, 0, 0);
}

template<class MapType>
static void time_map_fetch_sequential(int iters) {
  vector<int> v(iters);
  for (int i = 0; i < iters; i++) {
    v[i] = i;
  }
  time_map_fetch<MapType>(iters, v, "map_fetch_sequential");
}

// Apply a pseudorandom permutation to the given vector.
static void shuffle(vector<int>* v) {
  srand(9);
  for (int n = v->size(); n >= 2; n--) {
    swap((*v)[n - 1], (*v)[static_cast<unsigned>(rand()) % n]);
  }
}

template<class MapType>
static void time_map_fetch_random(int iters) {
  vector<int> v(iters);
  for (int i = 0; i < iters; i++) {
    v[i] = i;
  }
  shuffle(&v);
  time_map_fetch<MapType>(iters, v, "map_fetch_random");
}

template<class MapType>
static void time_map_fetch_empty(int iters) {
  MapType set;
  Rusage t;
  int r;
  int i;

  r = 1;
  t.Reset();
  for (i = 0; i < iters; i++) {
    r ^= static_cast<int>(set.find(i) != set.end());
  }
  double ut = t.UserTime();

  srand(r);   // keep compiler from optimizing away r (we never call rand())
  report("map_fetch_empty", ut, iters, 0, 0);
}

template<class MapType>
static void time_map_remove(int iters) {
  MapType set;
  Rusage t;
  int i;

  for (i = 0; i < iters; i++) {
    set[i] = i+1;
  }

  t.Reset();
  for (i = 0; i < iters; i++) {
    set.erase(i);
  }
  double ut = t.UserTime();

  report("map_remove", ut, iters, 0, 0);
}

template<class MapType>
static void time_map_toggle(int iters) {
  MapType set;
  Rusage t;
  int i;

  const size_t start = CurrentMemoryUsage();
  t.Reset();
  for (i = 0; i < iters; i++) {
    set[i] = i+1;
    set.erase(i);
  }

  double ut = t.UserTime();
  const size_t finish = CurrentMemoryUsage();

  report("map_toggle", ut, iters, start, finish);
}

template<class MapType>
static void time_map_iterate(int iters) {
  MapType set;
  Rusage t;
  int r;
  int i;

  for (i = 0; i < iters; i++) {
    set[i] = i+1;
  }

  r = 1;
  t.Reset();
  for (typename MapType::const_iterator it = set.begin(), it_end = set.end();
       it != it_end;
       ++it) {
    r ^= it->second;
  }

  double ut = t.UserTime();

  srand(r);   // keep compiler from optimizing away r (we never call rand())
  report("map_iterate", ut, iters, 0, 0);
}

template<class MapType>
static void stresshashfunction(int desired_insertions,
                               int map_size,
                               int stride) {
  Rusage t;
  int num_insertions = 0;
  // One measurement of user time (in seconds) is done for each iteration of
  // the outer loop.  The times are summed.
  double total_seconds = 0;
  const int k = desired_insertions / map_size;
  MapType set;
  for (int o = 0; o < k; o++) {
    set.clear();
    set.resize(map_size);
    t.Reset();
    const int maxint = (1ull << (sizeof(int) * 8 - 1)) - 1;
    // Use n arithmetic sequences.  Using just one may lead to overflow
    // if stride * map_size > maxint.  Compute n by requiring
    // stride * map_size/n < maxint, i.e., map_size/(maxint/stride) < n
    char* key;   // something we can do math on
    const int n = map_size / (maxint / stride) + 1;
    for (int i = 0; i < n; i++) {
      key = NULL;
      key += i;
      for (int j = 0; j < map_size/n; j++) {
        key += stride;
        set[reinterpret_cast<typename MapType::key_type>(key)]
            = ++num_insertions;
      }
    }
    total_seconds += t.UserTime();
  }
  printf("stresshashfunction map_size=%d stride=%d: %.1fns/insertion\n",
         map_size, stride, total_seconds * 1e9 / num_insertions);
}

template<class MapType>
static void stresshashfunction(int num_inserts) {
  static const int kMapSizes[] = {256, 1024};
  for (unsigned i = 0; i < sizeof(kMapSizes) / sizeof(kMapSizes[0]); i++) {
    const int map_size = kMapSizes[i];
    for (int stride = 1; stride <= map_size; stride *= map_size) {
      stresshashfunction<MapType>(num_inserts, map_size, stride);
    }
  }
}

template<class MapType, class StressMapType>
static void measure_map(const char* label, int obj_size, int iters,
                        bool stress_hash_function) {
  printf("\n%s (%d byte objects, %d iterations):\n", label, obj_size, iters);
  if (1) time_map_grow<MapType>(iters);
  if (1) time_map_grow_predicted<MapType>(iters);
  if (1) time_map_replace<MapType>(iters);
  if (1) time_map_fetch_random<MapType>(iters);
  if (1) time_map_fetch_sequential<MapType>(iters);
  if (1) time_map_fetch_empty<MapType>(iters);
  if (1) time_map_remove<MapType>(iters);
  if (1) time_map_toggle<MapType>(iters);
  if (1) time_map_iterate<MapType>(iters);
  // This last test is useful only if the map type uses hashing.
  // And it's slow, so use fewer iterations.
  if (stress_hash_function) {
    // Blank line in the output makes clear that what follows isn't part of the
    // table of results that we just printed.
    puts("");
    stresshashfunction<StressMapType>(iters / 4);
  }
}

template<class ObjType>
static void test_all_maps(int obj_size, int iters) {
  const bool stress_hash_function = obj_size <= 8;

  if (FLAGS_test_sparse_hash_map)
    measure_map< EasyUseSparseHashMap<ObjType, int, HashFn>,
                 EasyUseSparseHashMap<ObjType*, int, HashFn> >(
        "SPARSE_HASH_MAP", obj_size, iters, stress_hash_function);

  if (FLAGS_test_dense_hash_map)
    measure_map< EasyUseDenseHashMap<ObjType, int, HashFn>,
                 EasyUseDenseHashMap<ObjType*, int, HashFn> >(
        "DENSE_HASH_MAP", obj_size, iters, stress_hash_function);

  if (FLAGS_test_hash_map)
    measure_map< EasyUseHashMap<ObjType, int, HashFn>,
                 EasyUseHashMap<ObjType*, int, HashFn> >(
        "STANDARD HASH_MAP", obj_size, iters, stress_hash_function);

  if (FLAGS_test_map)
    measure_map< EasyUseMap<ObjType, int>,
                 EasyUseMap<ObjType*, int> >(
        "STANDARD MAP", obj_size, iters, false);
}

int main(int argc, char** argv) {

  int iters = kDefaultIters;
  if (argc > 1) {            // first arg is # of iterations
    iters = atoi(argv[1]);
  }

  stamp_run(iters);

#ifndef HAVE_SYS_RESOURCE_H
  printf("\n*** WARNING ***: sys/resources.h was not found, so all times\n"
         "                 reported are wall-clock time, not user time\n");
#endif

  // It would be nice to set these at run-time, but by setting them at
  // compile-time, we allow optimizations that make it as fast to use
  // a HashObject as it would be to use just a straight int/char
  // buffer.  To keep memory use similar, we normalize the number of
  // iterations based on size.
  if (FLAGS_test_4_bytes)  test_all_maps< HashObject<4,4> >(4, iters/1);
  if (FLAGS_test_8_bytes)  test_all_maps< HashObject<8,8> >(8, iters/2);
  if (FLAGS_test_16_bytes)  test_all_maps< HashObject<16,16> >(16, iters/4);
  if (FLAGS_test_256_bytes)  test_all_maps< HashObject<256,32> >(256, iters/32);

  return 0;
}
