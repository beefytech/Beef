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
//
// This tests densehashtable
// This tests dense_hash_set
// This tests dense_hash_map
// This tests sparsehashtable
// This tests sparse_hash_set
// This tests sparse_hash_map
//
// This test replaces hashtable_unittest.cc, which was becoming
// unreadable.  This file is opaque but hopefully not unreadable -- at
// least, not the tests!
//
// Note that since all these classes are templatized, it's important
// to call every public method on the class: not just to make sure
// they work, but to make sure they even compile.

#include <sparsehash/internal/sparseconfig.h>
#include <config.h>
#include <math.h>
#include <stddef.h>   // for size_t
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif   // for uintptr_t
#include <iostream>
#include <set>
#include <sstream>
#include <typeinfo>   // for class typeinfo (returned by typeid)
#include <vector>
#include <sparsehash/type_traits.h>
#include <sparsehash/sparsetable>
#include "hash_test_interface.h"
#include "testutil.h"
namespace testing = GOOGLE_NAMESPACE::testing;

using std::cout;
using std::pair;
using std::set;
using std::string;
using std::vector;
using GOOGLE_NAMESPACE::dense_hash_map;
using GOOGLE_NAMESPACE::dense_hash_set;
using GOOGLE_NAMESPACE::sparse_hash_map;
using GOOGLE_NAMESPACE::sparse_hash_set;
using GOOGLE_NAMESPACE::sparsetable;
using GOOGLE_NAMESPACE::HashtableInterface_SparseHashMap;
using GOOGLE_NAMESPACE::HashtableInterface_SparseHashSet;
using GOOGLE_NAMESPACE::HashtableInterface_SparseHashtable;
using GOOGLE_NAMESPACE::HashtableInterface_DenseHashMap;
using GOOGLE_NAMESPACE::HashtableInterface_DenseHashSet;
using GOOGLE_NAMESPACE::HashtableInterface_DenseHashtable;
namespace sparsehash_internal = GOOGLE_NAMESPACE::sparsehash_internal;

typedef unsigned char uint8;

#ifdef _MSC_VER
// Below, we purposefully test having a very small allocator size.
// This causes some "type conversion too small" errors when using this
// allocator with sparsetable buckets.  We're testing to make sure we
// handle that situation ok, so we don't need the compiler warnings.
#pragma warning(disable:4244)
#endif

namespace {

#ifndef _MSC_VER   // windows defines its own version
# ifdef __MINGW32__ // mingw has trouble writing to /tmp
static string TmpFile(const char* basename) {
  return string("./#") + basename;
}
# else
static string TmpFile(const char* basename) {
  string kTmpdir = "/tmp";
  return kTmpdir + "/" + basename;
}
# endif
#endif

// Used as a value in some of the hashtable tests.  It's just some
// arbitrary user-defined type with non-trivial memory management.
struct ValueType {
 public:
  ValueType() : s_(kDefault) { }
  ValueType(const char* init_s) : s_(kDefault) { set_s(init_s); }
  ~ValueType() { set_s(NULL); }
  ValueType(const ValueType& that) : s_(kDefault) { operator=(that); }
  void operator=(const ValueType& that) { set_s(that.s_); }
  bool operator==(const ValueType& that) const {
    return strcmp(this->s(), that.s()) == 0;
  }
  void set_s(const char* new_s) {
    if (s_ != kDefault)
      free(const_cast<char*>(s_));
    s_ = (new_s == NULL ? kDefault : reinterpret_cast<char*>(strdup(new_s)));
  }
  const char* s() const { return s_; }
 private:
  const char* s_;
  static const char* const kDefault;
};
const char* const ValueType::kDefault = "hi";

// This is used by the low-level sparse/dense_hashtable classes,
// which support the most general relationship between keys and
// values: the key is derived from the value through some arbitrary
// function.  (For classes like sparse_hash_map, the 'value' is a
// key/data pair, and the function to derive the key is
// FirstElementOfPair.)  KeyToValue is the inverse of this function,
// so GetKey(KeyToValue(key)) == key.  To keep the tests a bit
// simpler, we've chosen to make the key and value actually be the
// same type, which is why we need only one template argument for the
// types, rather than two (one for the key and one for the value).
template<class KeyAndValueT, class KeyToValue>
struct SetKey {
  void operator()(KeyAndValueT* value, const KeyAndValueT& new_key) const {
    *value = KeyToValue()(new_key);
  }
};

// A hash function that keeps track of how often it's called.  We use
// a simple djb-hash so we don't depend on how STL hashes.  We use
// this same method to do the key-comparison, so we can keep track
// of comparison-counts too.
struct Hasher {
  explicit Hasher(int i=0) : id_(i), num_hashes_(0), num_compares_(0) { }
  int id() const { return id_; }
  int num_hashes() const { return num_hashes_; }
  int num_compares() const { return num_compares_; }

  size_t operator()(int a) const {
    num_hashes_++;
    return static_cast<size_t>(a);
  }
  size_t operator()(const char* a) const {
    num_hashes_++;
    size_t hash = 0;
    for (size_t i = 0; a[i]; i++ )
      hash = 33 * hash + a[i];
    return hash;
  }
  size_t operator()(const string& a) const {
    num_hashes_++;
    size_t hash = 0;
    for (size_t i = 0; i < a.length(); i++ )
      hash = 33 * hash + a[i];
    return hash;
  }
  size_t operator()(const int* a) const {
    num_hashes_++;
    return static_cast<size_t>(reinterpret_cast<uintptr_t>(a));
  }
  bool operator()(int a, int b) const {
    num_compares_++;
    return a == b;
  }
  bool operator()(const string& a, const string& b) const {
    num_compares_++;
    return a == b;
  }
  bool operator()(const char* a, const char* b) const {
    num_compares_++;
    // The 'a == b' test is necessary, in case a and b are both NULL.
    return (a == b || (a && b && strcmp(a, b) == 0));
  }

 private:
  mutable int id_;
  mutable int num_hashes_;
  mutable int num_compares_;
};

// Allocator that allows controlling its size in various ways, to test
// allocator overflow.  Because we use this allocator in a vector, we
// need to define != and swap for gcc.
template<typename T,
         typename SizeT = size_t, SizeT MAX_SIZE = static_cast<SizeT>(~0)>
struct Alloc {
  typedef T value_type;
  typedef SizeT size_type;
  typedef ptrdiff_t difference_type;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;

  explicit Alloc(int i=0, int* count=NULL) : id_(i), count_(count) {}
  ~Alloc() {}
  pointer address(reference r) const  { return &r; }
  const_pointer address(const_reference r) const  { return &r; }
  pointer allocate(size_type n, const_pointer = 0) {
    if (count_)  ++(*count_);
    return static_cast<pointer>(malloc(n * sizeof(value_type)));
  }
  void deallocate(pointer p, size_type) {
    free(p);
  }
  pointer reallocate(pointer p, size_type n) {
    if (count_)  ++(*count_);
    return static_cast<pointer>(realloc(p, n * sizeof(value_type)));
  }
  size_type max_size() const  {
    return static_cast<size_type>(MAX_SIZE);
  }
  void construct(pointer p, const value_type& val) {
    new(p) value_type(val);
  }
  void destroy(pointer p) { p->~value_type(); }

  bool is_custom_alloc() const { return true; }

  template <class U>
  Alloc(const Alloc<U, SizeT, MAX_SIZE>& that)
      : id_(that.id_), count_(that.count_) {
  }

  template <class U>
  struct rebind {
    typedef Alloc<U, SizeT, MAX_SIZE> other;
  };

  bool operator==(const Alloc<T,SizeT,MAX_SIZE>& that) {
    return this->id_ == that.id_ && this->count_ == that.count_;
  }
  bool operator!=(const Alloc<T,SizeT,MAX_SIZE>& that) {
    return !this->operator==(that);
  }

  int id() const { return id_; }

  // I have to make these public so the constructor used for rebinding
  // can see them.  Normally, I'd just make them private and say:
  //   template<typename U, typename U_SizeT, U_SizeT U_MAX_SIZE> friend struct Alloc;
  // but MSVC 7.1 barfs on that.  So public it is.  But no peeking!
 public:
  int id_;
  int* count_;
};


// Below are a few fun routines that convert a value into a key, used
// for dense_hashtable and sparse_hashtable.  It's our responsibility
// to make sure, when we insert values into these objects, that the
// values match the keys we insert them under.  To allow us to use
// these routines for SetKey as well, we require all these functions
// be their own inverse: f(f(x)) == x.
template<class Value>
struct Negation {
  typedef Value result_type;
  Value operator()(Value& v) { return -v; }
  const Value operator()(const Value& v) const { return -v; }
};

struct Capital {
  typedef string result_type;
  string operator()(string& s) {
    return string(1, s[0] ^ 32) + s.substr(1);
  }
  const string operator()(const string& s) const {
    return string(1, s[0] ^ 32) + s.substr(1);
  }
};

struct Identity {   // lame, I know, but an important case to test.
  typedef const char* result_type;
  const char* operator()(const char* s) const {
    return s;
  }
};

// This is just to avoid memory leaks -- it's a global pointer to
// all the memory allocated by UniqueObjectHelper.  We'll use it
// to semi-test sparsetable as well. :-)
sparsetable<char*> g_unique_charstar_objects(16);

// This is an object-generator: pass in an index, and it will return a
// unique object of type ItemType.  We provide specializations for the
// types we actually support.
template <typename ItemType> ItemType UniqueObjectHelper(int index);
template<> int UniqueObjectHelper(int index) {
  return index;
}
template<> string UniqueObjectHelper(int index) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%d", index);
  return buffer;
}
template<> char* UniqueObjectHelper(int index) {
  // First grow the table if need be.
  sparsetable<char*>::size_type table_size = g_unique_charstar_objects.size();
  while (index >= static_cast<int>(table_size)) {
    assert(table_size * 2 > table_size);  // avoid overflow problems
    table_size *= 2;
  }
  if (table_size > g_unique_charstar_objects.size())
    g_unique_charstar_objects.resize(table_size);

  if (!g_unique_charstar_objects.test(index)) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d", index);
    g_unique_charstar_objects[index] = strdup(buffer);
  }
  return g_unique_charstar_objects.get(index);
}
template<> const char* UniqueObjectHelper(int index) {
  return UniqueObjectHelper<char*>(index);
}
template<> ValueType UniqueObjectHelper(int index) {
  return ValueType(UniqueObjectHelper<string>(index).c_str());
}
template<> pair<const int, int> UniqueObjectHelper(int index) {
  return pair<const int,int>(index, index + 1);
}
template<> pair<const string, string> UniqueObjectHelper(int index) {
  return pair<const string,string>(
      UniqueObjectHelper<string>(index), UniqueObjectHelper<string>(index + 1));
}
template<> pair<const char* const,ValueType> UniqueObjectHelper(int index) {
  return pair<const char* const,ValueType>(
      UniqueObjectHelper<char*>(index), UniqueObjectHelper<ValueType>(index+1));
}

class ValueSerializer {
 public:
  bool operator()(FILE* fp, const int& value) {
    return fwrite(&value, sizeof(value), 1, fp) == 1;
  }
  bool operator()(FILE* fp, int* value) {
    return fread(value, sizeof(*value), 1, fp) == 1;
  }
  bool operator()(FILE* fp, const string& value) {
    const int size = value.size();
    return (*this)(fp, size) && fwrite(value.c_str(), size, 1, fp) == 1;
  }
  bool operator()(FILE* fp, string* value) {
    int size;
    if (!(*this)(fp, &size)) return false;
    char* buf = new char[size];
    if (fread(buf, size, 1, fp) != 1) {
      delete[] buf;
      return false;
    }
    new(value) string(buf, size);
    delete[] buf;
    return true;
  }
  template <typename OUTPUT>
  bool operator()(OUTPUT* fp, const ValueType& v) {
    return (*this)(fp, string(v.s()));
  }
  template <typename INPUT>
  bool operator()(INPUT* fp, ValueType* v) {
    string data;
    if (!(*this)(fp, &data)) return false;
    new(v) ValueType(data.c_str());
    return true;
  }
  template <typename OUTPUT>
  bool operator()(OUTPUT* fp, const char* const& value) {
    // Just store the index.
    return (*this)(fp, atoi(value));
  }
  template <typename INPUT>
  bool operator()(INPUT* fp, const char** value) {
    // Look up via index.
    int index;
    if (!(*this)(fp, &index)) return false;
    *value = UniqueObjectHelper<char*>(index);
    return true;
  }
  template <typename OUTPUT, typename First, typename Second>
  bool operator()(OUTPUT* fp, std::pair<const First, Second>* value) {
    return (*this)(fp, const_cast<First*>(&value->first))
        && (*this)(fp, &value->second);
  }
  template <typename INPUT, typename First, typename Second>
  bool operator()(INPUT* fp, const std::pair<const First, Second>& value) {
    return (*this)(fp, value.first) && (*this)(fp, value.second);
  }
};

template <typename HashtableType>
class HashtableTest : public ::testing::Test {
 public:
  HashtableTest() : ht_() { }
  // Give syntactically-prettier access to UniqueObjectHelper.
  typename HashtableType::value_type UniqueObject(int index) {
    return UniqueObjectHelper<typename HashtableType::value_type>(index);
  }
  typename HashtableType::key_type UniqueKey(int index) {
    return this->ht_.get_key(this->UniqueObject(index));
  }
 protected:
  HashtableType ht_;
};

}

// These are used to specify the empty key and deleted key in some
// contexts.  They can't be in the unnamed namespace, or static,
// because the template code requires external linkage.
extern const string kEmptyString("--empty string--");
extern const string kDeletedString("--deleted string--");
extern const int kEmptyInt = 0;
extern const int kDeletedInt = -1234676543;  // an unlikely-to-pick int
extern const char* const kEmptyCharStar = "--empty char*--";
extern const char* const kDeletedCharStar = "--deleted char*--";

namespace {

#define INT_HASHTABLES                                                  \
  HashtableInterface_SparseHashMap<int, int, Hasher, Hasher,            \
                                   Alloc<int> >,                        \
  HashtableInterface_SparseHashSet<int, Hasher, Hasher,                 \
                                   Alloc<int> >,                        \
  /* This is a table where the key associated with a value is -value */ \
  HashtableInterface_SparseHashtable<int, int, Hasher, Negation<int>,   \
                                     SetKey<int, Negation<int> >,       \
                                     Hasher, Alloc<int> >,              \
  HashtableInterface_DenseHashMap<int, int, kEmptyInt, Hasher, Hasher,  \
                                  Alloc<int> >,                         \
  HashtableInterface_DenseHashSet<int, kEmptyInt, Hasher, Hasher,       \
                                  Alloc<int> >,                         \
  HashtableInterface_DenseHashtable<int, int, kEmptyInt,                \
                                    Hasher, Negation<int>,              \
                                    SetKey<int, Negation<int> >,        \
                                    Hasher, Alloc<int> >

#define STRING_HASHTABLES                                               \
  HashtableInterface_SparseHashMap<string, string, Hasher, Hasher,      \
                                   Alloc<string> >,                     \
  HashtableInterface_SparseHashSet<string, Hasher, Hasher,              \
                                   Alloc<string> >,                     \
  /* This is a table where the key associated with a value is Cap(value) */ \
  HashtableInterface_SparseHashtable<string, string, Hasher, Capital,   \
                                     SetKey<string, Capital>,           \
                                     Hasher, Alloc<string> >,           \
  HashtableInterface_DenseHashMap<string, string, kEmptyString,         \
                                  Hasher, Hasher, Alloc<string> >,      \
  HashtableInterface_DenseHashSet<string, kEmptyString, Hasher, Hasher, \
                                  Alloc<string> >,                      \
  HashtableInterface_DenseHashtable<string, string, kEmptyString,       \
                                    Hasher, Capital,                    \
                                    SetKey<string, Capital>,            \
                                    Hasher, Alloc<string> >

// I'd like to use ValueType keys for SparseHashtable<> and
// DenseHashtable<> but I can't due to memory-management woes (nobody
// really owns the char* involved).  So instead I do something simpler.
#define CHARSTAR_HASHTABLES                                             \
  HashtableInterface_SparseHashMap<const char*, ValueType,              \
                                   Hasher, Hasher, Alloc<const char*> >, \
  HashtableInterface_SparseHashSet<const char*, Hasher, Hasher,         \
                                   Alloc<const char*> >,                \
  /* This is a table where each value is its own key. */                \
  HashtableInterface_SparseHashtable<const char*, const char*,          \
                                     Hasher, Identity,                  \
                                     SetKey<const char*, Identity>,     \
                                     Hasher, Alloc<const char*> >,      \
  HashtableInterface_DenseHashMap<const char*, ValueType, kEmptyCharStar, \
                                  Hasher, Hasher, Alloc<const char*> >, \
  HashtableInterface_DenseHashSet<const char*, kEmptyCharStar,          \
                                  Hasher, Hasher, Alloc<const char*> >, \
  HashtableInterface_DenseHashtable<const char*, const char*, kEmptyCharStar, \
                                    Hasher, Identity,                   \
                                    SetKey<const char*, Identity>,      \
                                    Hasher, Alloc<ValueType> >

// This is the list of types we run each test against.
// We need to define the same class 4 times due to limitations in the
// testing framework.  Basically, we associate each class below with
// the set of types we want to run tests on it with.
template <typename HashtableType> class HashtableIntTest
    : public HashtableTest<HashtableType> { };
template <typename HashtableType> class HashtableStringTest
    : public HashtableTest<HashtableType> { };
template <typename HashtableType> class HashtableCharStarTest
    : public HashtableTest<HashtableType> { };
template <typename HashtableType> class HashtableAllTest
    : public HashtableTest<HashtableType> { };

typedef testing::TypeList6<INT_HASHTABLES> IntHashtables;
typedef testing::TypeList6<STRING_HASHTABLES> StringHashtables;
typedef testing::TypeList6<CHARSTAR_HASHTABLES> CharStarHashtables;
typedef testing::TypeList18<INT_HASHTABLES, STRING_HASHTABLES,
                            CHARSTAR_HASHTABLES> AllHashtables;

TYPED_TEST_CASE_6(HashtableIntTest, IntHashtables);
TYPED_TEST_CASE_6(HashtableStringTest, StringHashtables);
TYPED_TEST_CASE_6(HashtableCharStarTest, CharStarHashtables);
TYPED_TEST_CASE_18(HashtableAllTest, AllHashtables);

// ------------------------------------------------------------------------
// First, some testing of the underlying infrastructure.

TEST(HashtableCommonTest, HashMunging) {
  const Hasher hasher;

  // We don't munge the hash value on non-pointer template types.
  {
    const sparsehash_internal::sh_hashtable_settings<int, Hasher, size_t, 1>
        settings(hasher, 0.0, 0.0);
    const int v = 1000;
    EXPECT_EQ(hasher(v), settings.hash(v));
  }

  {
    // We do munge the hash value on pointer template types.
    const sparsehash_internal::sh_hashtable_settings<int*, Hasher, size_t, 1>
        settings(hasher, 0.0, 0.0);
    int* v = NULL;
    v += 0x10000;    // get a non-trivial pointer value
    EXPECT_NE(hasher(v), settings.hash(v));
  }
  {
    const sparsehash_internal::sh_hashtable_settings<const int*, Hasher,
                                                     size_t, 1>
        settings(hasher, 0.0, 0.0);
    const int* v = NULL;
    v += 0x10000;    // get a non-trivial pointer value
    EXPECT_NE(hasher(v), settings.hash(v));
  }
}

// ------------------------------------------------------------------------
// If the first arg to TYPED_TEST is HashtableIntTest, it will run
// this test on all the hashtable types, with key=int and value=int.
// Likewise, HashtableStringTest will have string key/values, and
// HashtableCharStarTest will have char* keys and -- just to mix it up
// a little -- ValueType values.  HashtableAllTest will run all three
// key/value types on all 6 hashtables types, for 18 test-runs total
// per test.
//
// In addition, TYPED_TEST makes available the magic keyword
// TypeParam, which is the type being used for the current test.

// This first set of tests just tests the public API, going through
// the public typedefs and methods in turn.  It goes approximately
// in the definition-order in sparse_hash_map.h.

TYPED_TEST(HashtableIntTest, Typedefs) {
  // Make sure all the standard STL-y typedefs are defined.  The exact
  // key/value types don't matter here, so we only bother testing on
  // the int tables.  This is just a compile-time "test"; nothing here
  // can fail at runtime.
  this->ht_.set_deleted_key(-2);  // just so deleted_key succeeds
  typename TypeParam::key_type kt;
  typename TypeParam::value_type vt;
  typename TypeParam::hasher h;
  typename TypeParam::key_equal ke;
  typename TypeParam::allocator_type at;

  typename TypeParam::size_type st;
  typename TypeParam::difference_type dt;
  typename TypeParam::pointer p;
  typename TypeParam::const_pointer cp;
  // I can't declare variables of reference-type, since I have nothing
  // to point them to, so I just make sure that these types exist.
  __attribute__((unused)) typedef typename TypeParam::reference r;
  __attribute__((unused)) typedef typename TypeParam::const_reference cf;

  typename TypeParam::iterator i;
  typename TypeParam::const_iterator ci;
  typename TypeParam::local_iterator li;
  typename TypeParam::const_local_iterator cli;

  // Now make sure the variables are used, so the compiler doesn't
  // complain.  Where possible, I "use" the variable by calling the
  // method that's supposed to return the unique instance of the
  // relevant type (eg. get_allocator()).  Otherwise, I try to call a
  // different, arbitrary function that returns the type.  Sometimes
  // the type isn't used at all, and there's no good way to use the
  // variable.
  kt = this->ht_.deleted_key();
  (void)vt;   // value_type may not be copyable.  Easiest not to try.
  h = this->ht_.hash_funct();
  ke = this->ht_.key_eq();
  at = this->ht_.get_allocator();
  st = this->ht_.size();
  (void)dt;
  (void)p;
  (void)cp;
  i = this->ht_.begin();
  ci = this->ht_.begin();
  li = this->ht_.begin(0);
  cli = this->ht_.begin(0);
}

TYPED_TEST(HashtableAllTest, NormalIterators) {
  EXPECT_TRUE(this->ht_.begin() == this->ht_.end());
  this->ht_.insert(this->UniqueObject(1));
  {
    typename TypeParam::iterator it = this->ht_.begin();
    EXPECT_TRUE(it != this->ht_.end());
    ++it;
    EXPECT_TRUE(it == this->ht_.end());
  }
}

TEST(HashtableTest, ModifyViaIterator) {
  // This only works for hash-maps, since only they have non-const values.
  {
    sparse_hash_map<int, int> ht;
    ht[1] = 2;
    sparse_hash_map<int, int>::iterator it = ht.find(1);
    EXPECT_TRUE(it != ht.end());
    EXPECT_EQ(1, it->first);
    EXPECT_EQ(2, it->second);
    it->second = 5;
    it = ht.find(1);
    EXPECT_TRUE(it != ht.end());
    EXPECT_EQ(5, it->second);
  }
  {
    dense_hash_map<int, int> ht;
    ht.set_empty_key(0);
    ht[1] = 2;
    dense_hash_map<int, int>::iterator it = ht.find(1);
    EXPECT_TRUE(it != ht.end());
    EXPECT_EQ(1, it->first);
    EXPECT_EQ(2, it->second);
    it->second = 5;
    it = ht.find(1);
    EXPECT_TRUE(it != ht.end());
    EXPECT_EQ(5, it->second);
  }
}

TYPED_TEST(HashtableAllTest, ConstIterators) {
  this->ht_.insert(this->UniqueObject(1));
  typename TypeParam::const_iterator it = this->ht_.begin();
  EXPECT_TRUE(it != this->ht_.end());
  ++it;
  EXPECT_TRUE(it == this->ht_.end());
}

TYPED_TEST(HashtableAllTest, LocalIterators) {
  // Now, tr1 begin/end (the local iterator that takes a bucket-number).
  // ht::bucket() returns the bucket that this key would be inserted in.
  this->ht_.insert(this->UniqueObject(1));
  const typename TypeParam::size_type bucknum =
      this->ht_.bucket(this->UniqueKey(1));
  typename TypeParam::local_iterator b = this->ht_.begin(bucknum);
  typename TypeParam::local_iterator e = this->ht_.end(bucknum);
  EXPECT_TRUE(b != e);
  b++;
  EXPECT_TRUE(b == e);

  // Check an empty bucket.  We can just xor the bottom bit and be sure
  // of getting a legal bucket, since #buckets is always a power of 2.
  EXPECT_TRUE(this->ht_.begin(bucknum ^ 1) == this->ht_.end(bucknum ^ 1));
  // Another test, this time making sure we're using the right types.
  typename TypeParam::local_iterator b2 = this->ht_.begin(bucknum ^ 1);
  typename TypeParam::local_iterator e2 = this->ht_.end(bucknum ^ 1);
  EXPECT_TRUE(b2 == e2);
}

TYPED_TEST(HashtableAllTest, ConstLocalIterators) {
  this->ht_.insert(this->UniqueObject(1));
  const typename TypeParam::size_type bucknum =
      this->ht_.bucket(this->UniqueKey(1));
  typename TypeParam::const_local_iterator b = this->ht_.begin(bucknum);
  typename TypeParam::const_local_iterator e = this->ht_.end(bucknum);
  EXPECT_TRUE(b != e);
  b++;
  EXPECT_TRUE(b == e);
  typename TypeParam::const_local_iterator b2 = this->ht_.begin(bucknum ^ 1);
  typename TypeParam::const_local_iterator e2 = this->ht_.end(bucknum ^ 1);
  EXPECT_TRUE(b2 == e2);
}

TYPED_TEST(HashtableAllTest, Iterating) {
  // Test a bit more iterating than just one ++.
  this->ht_.insert(this->UniqueObject(1));
  this->ht_.insert(this->UniqueObject(11));
  this->ht_.insert(this->UniqueObject(111));
  this->ht_.insert(this->UniqueObject(1111));
  this->ht_.insert(this->UniqueObject(11111));
  this->ht_.insert(this->UniqueObject(111111));
  this->ht_.insert(this->UniqueObject(1111111));
  this->ht_.insert(this->UniqueObject(11111111));
  this->ht_.insert(this->UniqueObject(111111111));
  typename TypeParam::iterator it = this->ht_.begin();
  for (int i = 1; i <= 9; i++) {   // start at 1 so i is never 0
    // && here makes it easier to tell what loop iteration the test failed on.
    EXPECT_TRUE(i && (it++ != this->ht_.end()));
  }
  EXPECT_TRUE(it == this->ht_.end());
}

TYPED_TEST(HashtableIntTest, Constructors) {
  // The key/value types don't matter here, so I just test on one set
  // of tables, the ones with int keys, which can easily handle the
  // placement-news we have to do below.
  Hasher hasher(1);   // 1 is a unique id
  int alloc_count = 0;
  Alloc<typename TypeParam::key_type> alloc(2, &alloc_count);

  TypeParam ht_noarg;
  TypeParam ht_onearg(100);
  TypeParam ht_twoarg(100, hasher);
  TypeParam ht_threearg(100, hasher, hasher);  // hasher serves as key_equal too
  TypeParam ht_fourarg(100, hasher, hasher, alloc);

  // The allocator should have been called at most once, for the last ht.
  EXPECT_LE(1, alloc_count);
  int old_alloc_count = alloc_count;

  const typename TypeParam::value_type input[] = {
    this->UniqueObject(1),
    this->UniqueObject(2),
    this->UniqueObject(4),
    this->UniqueObject(8)
  };
  const int num_inputs = sizeof(input) / sizeof(input[0]);
  const typename TypeParam::value_type *begin = &input[0];
  const typename TypeParam::value_type *end = begin + num_inputs;
  TypeParam ht_iter_noarg(begin, end);
  TypeParam ht_iter_onearg(begin, end, 100);
  TypeParam ht_iter_twoarg(begin, end, 100, hasher);
  TypeParam ht_iter_threearg(begin, end, 100, hasher, hasher);
  TypeParam ht_iter_fourarg(begin, end, 100, hasher, hasher, alloc);
  // Now the allocator should have been called more.
  EXPECT_GT(alloc_count, old_alloc_count);
  old_alloc_count = alloc_count;

  // Let's do a lot more inserting and make sure the alloc-count goes up
  for (int i = 2; i < 2000; i++)
    ht_fourarg.insert(this->UniqueObject(i));
  EXPECT_GT(alloc_count, old_alloc_count);

  EXPECT_LT(ht_noarg.bucket_count(), 100u);
  EXPECT_GE(ht_onearg.bucket_count(), 100u);
  EXPECT_GE(ht_twoarg.bucket_count(), 100u);
  EXPECT_GE(ht_threearg.bucket_count(), 100u);
  EXPECT_GE(ht_fourarg.bucket_count(), 100u);
  EXPECT_GE(ht_iter_onearg.bucket_count(), 100u);

  // When we pass in a hasher -- it can serve both as the hash-function
  // and the key-equal function -- its id should be 1.  Where we don't
  // pass it in and use the default Hasher object, the id should be 0.
  EXPECT_EQ(0, ht_noarg.hash_funct().id());
  EXPECT_EQ(0, ht_noarg.key_eq().id());
  EXPECT_EQ(0, ht_onearg.hash_funct().id());
  EXPECT_EQ(0, ht_onearg.key_eq().id());
  EXPECT_EQ(1, ht_twoarg.hash_funct().id());
  EXPECT_EQ(0, ht_twoarg.key_eq().id());
  EXPECT_EQ(1, ht_threearg.hash_funct().id());
  EXPECT_EQ(1, ht_threearg.key_eq().id());

  EXPECT_EQ(0, ht_iter_noarg.hash_funct().id());
  EXPECT_EQ(0, ht_iter_noarg.key_eq().id());
  EXPECT_EQ(0, ht_iter_onearg.hash_funct().id());
  EXPECT_EQ(0, ht_iter_onearg.key_eq().id());
  EXPECT_EQ(1, ht_iter_twoarg.hash_funct().id());
  EXPECT_EQ(0, ht_iter_twoarg.key_eq().id());
  EXPECT_EQ(1, ht_iter_threearg.hash_funct().id());
  EXPECT_EQ(1, ht_iter_threearg.key_eq().id());

  // Likewise for the allocator
  EXPECT_EQ(0, ht_threearg.get_allocator().id());
  EXPECT_EQ(0, ht_iter_threearg.get_allocator().id());
  EXPECT_EQ(2, ht_fourarg.get_allocator().id());
  EXPECT_EQ(2, ht_iter_fourarg.get_allocator().id());
}

TYPED_TEST(HashtableAllTest, OperatorEquals) {
  {
    TypeParam ht1, ht2;
    ht1.set_deleted_key(this->UniqueKey(1));
    ht2.set_deleted_key(this->UniqueKey(2));

    ht1.insert(this->UniqueObject(10));
    ht2.insert(this->UniqueObject(20));
    EXPECT_FALSE(ht1 == ht2);
    ht1 = ht2;
    EXPECT_TRUE(ht1 == ht2);
  }
  {
    TypeParam ht1, ht2;
    ht1.insert(this->UniqueObject(30));
    ht1 = ht2;
    EXPECT_EQ(0u, ht1.size());
  }
  {
    TypeParam ht1, ht2;
    ht1.set_deleted_key(this->UniqueKey(1));
    ht2.insert(this->UniqueObject(1));        // has same key as ht1.delkey
    ht1 = ht2;     // should reset deleted-key to 'unset'
    EXPECT_EQ(1u, ht1.size());
    EXPECT_EQ(1u, ht1.count(this->UniqueKey(1)));
  }
}

TYPED_TEST(HashtableAllTest, Clear) {
  for (int i = 1; i < 200; i++) {
    this->ht_.insert(this->UniqueObject(i));
  }
  this->ht_.clear();
  EXPECT_EQ(0u, this->ht_.size());
  // TODO(csilvers): do we want to enforce that the hashtable has or
  // has not shrunk?  It does for dense_* but not sparse_*.
}

TYPED_TEST(HashtableAllTest, ClearNoResize) {
  if (!this->ht_.supports_clear_no_resize())
    return;
  typename TypeParam::size_type empty_bucket_count = this->ht_.bucket_count();
  int last_element = 1;
  while (this->ht_.bucket_count() == empty_bucket_count) {
    this->ht_.insert(this->UniqueObject(last_element));
    ++last_element;
  }
  typename TypeParam::size_type last_bucket_count = this->ht_.bucket_count();
  this->ht_.clear_no_resize();
  EXPECT_EQ(last_bucket_count, this->ht_.bucket_count());
  EXPECT_TRUE(this->ht_.empty());

  // When inserting the same number of elements again, no resize
  // should be necessary.
  for (int i = 1; i < last_element; ++i) {
    this->ht_.insert(this->UniqueObject(last_element + i));
    EXPECT_EQ(last_bucket_count, this->ht_.bucket_count());
  }
}

TYPED_TEST(HashtableAllTest, Swap) {
  // Let's make a second hashtable with its own hasher, key_equal, etc.
  Hasher hasher(1);   // 1 is a unique id
  TypeParam other_ht(200, hasher, hasher);

  this->ht_.set_deleted_key(this->UniqueKey(1));
  other_ht.set_deleted_key(this->UniqueKey(2));

  for (int i = 3; i < 2000; i++) {
    this->ht_.insert(this->UniqueObject(i));
  }
  this->ht_.erase(this->UniqueKey(1000));
  other_ht.insert(this->UniqueObject(2001));
  typename TypeParam::size_type expected_buckets = other_ht.bucket_count();

  this->ht_.swap(other_ht);

  EXPECT_EQ(this->UniqueKey(2), this->ht_.deleted_key());
  EXPECT_EQ(this->UniqueKey(1), other_ht.deleted_key());

  EXPECT_EQ(1, this->ht_.hash_funct().id());
  EXPECT_EQ(0, other_ht.hash_funct().id());

  EXPECT_EQ(1, this->ht_.key_eq().id());
  EXPECT_EQ(0, other_ht.key_eq().id());

  EXPECT_EQ(expected_buckets, this->ht_.bucket_count());
  EXPECT_GT(other_ht.bucket_count(), 200u);

  EXPECT_EQ(1u, this->ht_.size());
  EXPECT_EQ(1996u, other_ht.size());    // because we erased 1000

  EXPECT_EQ(0u, this->ht_.count(this->UniqueKey(111)));
  EXPECT_EQ(1u, other_ht.count(this->UniqueKey(111)));
  EXPECT_EQ(1u, this->ht_.count(this->UniqueKey(2001)));
  EXPECT_EQ(0u, other_ht.count(this->UniqueKey(2001)));
  EXPECT_EQ(0u, this->ht_.count(this->UniqueKey(1000)));
  EXPECT_EQ(0u, other_ht.count(this->UniqueKey(1000)));

  // We purposefully don't swap allocs -- they're not necessarily swappable.

  // Now swap back, using the free-function swap
  // NOTE: MSVC seems to have trouble with this free swap, not quite
  // sure why.  I've given up trying to fix it though.
#ifdef _MSC_VER
  other_ht.swap(this->ht_);
#else
  std::swap(this->ht_, other_ht);
#endif

  EXPECT_EQ(this->UniqueKey(1), this->ht_.deleted_key());
  EXPECT_EQ(this->UniqueKey(2), other_ht.deleted_key());
  EXPECT_EQ(0, this->ht_.hash_funct().id());
  EXPECT_EQ(1, other_ht.hash_funct().id());
  EXPECT_EQ(1996u, this->ht_.size());
  EXPECT_EQ(1u, other_ht.size());
  EXPECT_EQ(1u, this->ht_.count(this->UniqueKey(111)));
  EXPECT_EQ(0u, other_ht.count(this->UniqueKey(111)));

  // A user reported a crash with this code using swap to clear.
  // We've since fixed the bug; this prevents a regression.
  TypeParam swap_to_clear_ht;
  swap_to_clear_ht.set_deleted_key(this->UniqueKey(1));
  for (int i = 2; i < 10000; ++i) {
    swap_to_clear_ht.insert(this->UniqueObject(i));
  }
  TypeParam empty_ht;
  empty_ht.swap(swap_to_clear_ht);
  swap_to_clear_ht.set_deleted_key(this->UniqueKey(1));
  for (int i = 2; i < 10000; ++i) {
    swap_to_clear_ht.insert(this->UniqueObject(i));
  }
}

TYPED_TEST(HashtableAllTest, Size) {
  EXPECT_EQ(0u, this->ht_.size());
  for (int i = 1; i < 1000; i++) {    // go through some resizes
    this->ht_.insert(this->UniqueObject(i));
    EXPECT_EQ(static_cast<typename TypeParam::size_type>(i), this->ht_.size());
  }
  this->ht_.clear();
  EXPECT_EQ(0u, this->ht_.size());

  this->ht_.set_deleted_key(this->UniqueKey(1));
  EXPECT_EQ(0u, this->ht_.size());     // deleted key doesn't count
  for (int i = 2; i < 1000; i++) {    // go through some resizes
    this->ht_.insert(this->UniqueObject(i));
    this->ht_.erase(this->UniqueKey(i));
    EXPECT_EQ(0u, this->ht_.size());
  }
}

TEST(HashtableTest, MaxSizeAndMaxBucketCount) {
  // The max size depends on the allocator.  So we can't use the
  // built-in allocator type; instead, we make our own types.
  sparse_hash_set<int, Hasher, Hasher, Alloc<int> > ht_default;
  sparse_hash_set<int, Hasher, Hasher, Alloc<int, unsigned char> > ht_char;
  sparse_hash_set<int, Hasher, Hasher, Alloc<int, unsigned char, 104> > ht_104;

  EXPECT_GE(ht_default.max_size(), 256u);
  EXPECT_EQ(255u, ht_char.max_size());
  EXPECT_EQ(104u, ht_104.max_size());

  // In our implementations, MaxBucketCount == MaxSize.
  EXPECT_EQ(ht_default.max_size(), ht_default.max_bucket_count());
  EXPECT_EQ(ht_char.max_size(), ht_char.max_bucket_count());
  EXPECT_EQ(ht_104.max_size(), ht_104.max_bucket_count());
}

TYPED_TEST(HashtableAllTest, Empty) {
  EXPECT_TRUE(this->ht_.empty());

  this->ht_.insert(this->UniqueObject(1));
  EXPECT_FALSE(this->ht_.empty());

  this->ht_.clear();
  EXPECT_TRUE(this->ht_.empty());

  TypeParam empty_ht;
  this->ht_.insert(this->UniqueObject(1));
  this->ht_.swap(empty_ht);
  EXPECT_TRUE(this->ht_.empty());
}

TYPED_TEST(HashtableAllTest, BucketCount) {
  TypeParam ht(100);
  // constructor arg is number of *items* to be inserted, not the
  // number of buckets, so we expect more buckets.
  EXPECT_GT(ht.bucket_count(), 100u);
  for (int i = 1; i < 200; i++) {
    ht.insert(this->UniqueObject(i));
  }
  EXPECT_GT(ht.bucket_count(), 200u);
}

TYPED_TEST(HashtableAllTest, BucketAndBucketSize) {
  const typename TypeParam::size_type expected_bucknum = this->ht_.bucket(
      this->UniqueKey(1));
  EXPECT_EQ(0u, this->ht_.bucket_size(expected_bucknum));

  this->ht_.insert(this->UniqueObject(1));
  EXPECT_EQ(expected_bucknum, this->ht_.bucket(this->UniqueKey(1)));
  EXPECT_EQ(1u, this->ht_.bucket_size(expected_bucknum));

  // Check that a bucket we didn't insert into, has a 0 size.  Since
  // we have an even number of buckets, bucknum^1 is guaranteed in range.
  EXPECT_EQ(0u, this->ht_.bucket_size(expected_bucknum ^ 1));
}

TYPED_TEST(HashtableAllTest, LoadFactor) {
  const typename TypeParam::size_type kSize = 16536;
  // Check growing past various thresholds and then shrinking below
  // them.
  for (float grow_threshold = 0.2f;
       grow_threshold <= 0.8f;
       grow_threshold += 0.2f) {
    TypeParam ht;
    ht.set_deleted_key(this->UniqueKey(1));
    ht.max_load_factor(grow_threshold);
    ht.min_load_factor(0.0);
    EXPECT_EQ(grow_threshold, ht.max_load_factor());
    EXPECT_EQ(0.0, ht.min_load_factor());

    ht.resize(kSize);
    size_t bucket_count = ht.bucket_count();
    // Erase and insert an element to set consider_shrink = true,
    // which should not cause a shrink because the threshold is 0.0.
    ht.insert(this->UniqueObject(2));
    ht.erase(this->UniqueKey(2));
    for (int i = 2;; ++i) {
      ht.insert(this->UniqueObject(i));
      if (static_cast<float>(ht.size())/bucket_count < grow_threshold) {
        EXPECT_EQ(bucket_count, ht.bucket_count());
      } else {
        EXPECT_GT(ht.bucket_count(), bucket_count);
        break;
      }
    }
    // Now set a shrink threshold 1% below the current size and remove
    // items until the size falls below that.
    const float shrink_threshold = static_cast<float>(ht.size()) /
        ht.bucket_count() - 0.01f;

    // This time around, check the old set_resizing_parameters interface.
    ht.set_resizing_parameters(shrink_threshold, 1.0);
    EXPECT_EQ(1.0, ht.max_load_factor());
    EXPECT_EQ(shrink_threshold, ht.min_load_factor());

    bucket_count = ht.bucket_count();
    for (int i = 2;; ++i) {
      ht.erase(this->UniqueKey(i));
      // A resize is only triggered by an insert, so add and remove a
      // value every iteration to trigger the shrink as soon as the
      // threshold is passed.
      ht.erase(this->UniqueKey(i+1));
      ht.insert(this->UniqueObject(i+1));
      if (static_cast<float>(ht.size())/bucket_count > shrink_threshold) {
        EXPECT_EQ(bucket_count, ht.bucket_count());
      } else {
        EXPECT_LT(ht.bucket_count(), bucket_count);
        break;
      }
    }
  }
}

TYPED_TEST(HashtableAllTest, ResizeAndRehash) {
  // resize() and rehash() are synonyms.  rehash() is the tr1 name.
  TypeParam ht(10000);
  ht.max_load_factor(0.8f);    // for consistency's sake

  for (int i = 1; i < 100; ++i)
    ht.insert(this->UniqueObject(i));
  ht.resize(0);
  // Now ht should be as small as possible.
  EXPECT_LT(ht.bucket_count(), 300u);

  ht.rehash(9000);    // use the 'rehash' version of the name.
  // Bucket count should be next power of 2, after considering max_load_factor.
  EXPECT_EQ(16384u, ht.bucket_count());
  for (int i = 101; i < 200; ++i)
    ht.insert(this->UniqueObject(i));
  // Adding a few hundred buckets shouldn't have caused a resize yet.
  EXPECT_EQ(ht.bucket_count(), 16384u);
}

TYPED_TEST(HashtableAllTest, FindAndCountAndEqualRange) {
  pair<typename TypeParam::iterator, typename TypeParam::iterator> eq_pair;
  pair<typename TypeParam::const_iterator,
       typename TypeParam::const_iterator> const_eq_pair;

  EXPECT_TRUE(this->ht_.empty());
  EXPECT_TRUE(this->ht_.find(this->UniqueKey(1)) == this->ht_.end());
  EXPECT_EQ(0u, this->ht_.count(this->UniqueKey(1)));
  eq_pair = this->ht_.equal_range(this->UniqueKey(1));
  EXPECT_TRUE(eq_pair.first == eq_pair.second);

  this->ht_.insert(this->UniqueObject(1));
  EXPECT_FALSE(this->ht_.empty());
  this->ht_.insert(this->UniqueObject(11));
  this->ht_.insert(this->UniqueObject(111));
  this->ht_.insert(this->UniqueObject(1111));
  this->ht_.insert(this->UniqueObject(11111));
  this->ht_.insert(this->UniqueObject(111111));
  this->ht_.insert(this->UniqueObject(1111111));
  this->ht_.insert(this->UniqueObject(11111111));
  this->ht_.insert(this->UniqueObject(111111111));
  EXPECT_EQ(9u, this->ht_.size());
  typename TypeParam::const_iterator it = this->ht_.find(this->UniqueKey(1));
  EXPECT_EQ(it.key(), this->UniqueKey(1));

  // Allow testing the const version of the methods as well.
  const TypeParam ht = this->ht_;

  // Some successful lookups (via find, count, and equal_range).
  EXPECT_TRUE(this->ht_.find(this->UniqueKey(1)) != this->ht_.end());
  EXPECT_EQ(1u, this->ht_.count(this->UniqueKey(1)));
  eq_pair = this->ht_.equal_range(this->UniqueKey(1));
  EXPECT_TRUE(eq_pair.first != eq_pair.second);
  EXPECT_EQ(eq_pair.first.key(), this->UniqueKey(1));
  ++eq_pair.first;
  EXPECT_TRUE(eq_pair.first == eq_pair.second);

  EXPECT_TRUE(ht.find(this->UniqueKey(1)) != ht.end());
  EXPECT_EQ(1u, ht.count(this->UniqueKey(1)));
  const_eq_pair = ht.equal_range(this->UniqueKey(1));
  EXPECT_TRUE(const_eq_pair.first != const_eq_pair.second);
  EXPECT_EQ(const_eq_pair.first.key(), this->UniqueKey(1));
  ++const_eq_pair.first;
  EXPECT_TRUE(const_eq_pair.first == const_eq_pair.second);

  EXPECT_TRUE(this->ht_.find(this->UniqueKey(11111)) != this->ht_.end());
  EXPECT_EQ(1u, this->ht_.count(this->UniqueKey(11111)));
  eq_pair = this->ht_.equal_range(this->UniqueKey(11111));
  EXPECT_TRUE(eq_pair.first != eq_pair.second);
  EXPECT_EQ(eq_pair.first.key(), this->UniqueKey(11111));
  ++eq_pair.first;
  EXPECT_TRUE(eq_pair.first == eq_pair.second);

  EXPECT_TRUE(ht.find(this->UniqueKey(11111)) != ht.end());
  EXPECT_EQ(1u, ht.count(this->UniqueKey(11111)));
  const_eq_pair = ht.equal_range(this->UniqueKey(11111));
  EXPECT_TRUE(const_eq_pair.first != const_eq_pair.second);
  EXPECT_EQ(const_eq_pair.first.key(), this->UniqueKey(11111));
  ++const_eq_pair.first;
  EXPECT_TRUE(const_eq_pair.first == const_eq_pair.second);

  // Some unsuccessful lookups (via find, count, and equal_range).
  EXPECT_TRUE(this->ht_.find(this->UniqueKey(11112)) == this->ht_.end());
  EXPECT_EQ(0u, this->ht_.count(this->UniqueKey(11112)));
  eq_pair = this->ht_.equal_range(this->UniqueKey(11112));
  EXPECT_TRUE(eq_pair.first == eq_pair.second);

  EXPECT_TRUE(ht.find(this->UniqueKey(11112)) == ht.end());
  EXPECT_EQ(0u, ht.count(this->UniqueKey(11112)));
  const_eq_pair = ht.equal_range(this->UniqueKey(11112));
  EXPECT_TRUE(const_eq_pair.first == const_eq_pair.second);

  EXPECT_TRUE(this->ht_.find(this->UniqueKey(11110)) == this->ht_.end());
  EXPECT_EQ(0u, this->ht_.count(this->UniqueKey(11110)));
  eq_pair = this->ht_.equal_range(this->UniqueKey(11110));
  EXPECT_TRUE(eq_pair.first == eq_pair.second);

  EXPECT_TRUE(ht.find(this->UniqueKey(11110)) == ht.end());
  EXPECT_EQ(0u, ht.count(this->UniqueKey(11110)));
  const_eq_pair = ht.equal_range(this->UniqueKey(11110));
  EXPECT_TRUE(const_eq_pair.first == const_eq_pair.second);
}

TYPED_TEST(HashtableAllTest, BracketInsert) {
  // tests operator[], for those types that support it.
  if (!this->ht_.supports_brackets())
    return;

  // bracket_equal is equivalent to ht_[a] == b.  It should insert a if
  // it doesn't already exist.
  EXPECT_TRUE(this->ht_.bracket_equal(this->UniqueKey(1),
                                      this->ht_.default_data()));
  EXPECT_TRUE(this->ht_.find(this->UniqueKey(1)) != this->ht_.end());

  // bracket_assign is equivalent to ht_[a] = b.
  this->ht_.bracket_assign(this->UniqueKey(2),
                           this->ht_.get_data(this->UniqueObject(4)));
  EXPECT_TRUE(this->ht_.find(this->UniqueKey(2)) != this->ht_.end());
  EXPECT_TRUE(this->ht_.bracket_equal(
      this->UniqueKey(2), this->ht_.get_data(this->UniqueObject(4))));

  this->ht_.bracket_assign(
      this->UniqueKey(2), this->ht_.get_data(this->UniqueObject(6)));
  EXPECT_TRUE(this->ht_.bracket_equal(
      this->UniqueKey(2), this->ht_.get_data(this->UniqueObject(6))));
  // bracket_equal shouldn't have modified the value.
  EXPECT_TRUE(this->ht_.bracket_equal(
      this->UniqueKey(2), this->ht_.get_data(this->UniqueObject(6))));

  // Verify that an operator[] that doesn't cause a resize, also
  // doesn't require an extra rehash.
  TypeParam ht(100);
  EXPECT_EQ(0, ht.hash_funct().num_hashes());
  ht.bracket_assign(this->UniqueKey(2), ht.get_data(this->UniqueObject(2)));
  EXPECT_EQ(1, ht.hash_funct().num_hashes());

  // And overwriting, likewise, should only cause one extra hash.
  ht.bracket_assign(this->UniqueKey(2), ht.get_data(this->UniqueObject(2)));
  EXPECT_EQ(2, ht.hash_funct().num_hashes());
}


TYPED_TEST(HashtableAllTest, InsertValue) {
  // First, try some straightforward insertions.
  EXPECT_TRUE(this->ht_.empty());
  this->ht_.insert(this->UniqueObject(1));
  EXPECT_FALSE(this->ht_.empty());
  this->ht_.insert(this->UniqueObject(11));
  this->ht_.insert(this->UniqueObject(111));
  this->ht_.insert(this->UniqueObject(1111));
  this->ht_.insert(this->UniqueObject(11111));
  this->ht_.insert(this->UniqueObject(111111));
  this->ht_.insert(this->UniqueObject(1111111));
  this->ht_.insert(this->UniqueObject(11111111));
  this->ht_.insert(this->UniqueObject(111111111));
  EXPECT_EQ(9u, this->ht_.size());
  EXPECT_EQ(1u, this->ht_.count(this->UniqueKey(1)));
  EXPECT_EQ(1u, this->ht_.count(this->UniqueKey(1111)));

  // Check the return type.
  pair<typename TypeParam::iterator, bool> insert_it;
  insert_it = this->ht_.insert(this->UniqueObject(1));
  EXPECT_EQ(false, insert_it.second);   // false: already present
  EXPECT_TRUE(*insert_it.first == this->UniqueObject(1));

  insert_it = this->ht_.insert(this->UniqueObject(2));
  EXPECT_EQ(true, insert_it.second);   // true: not already present
  EXPECT_TRUE(*insert_it.first == this->UniqueObject(2));
}

TYPED_TEST(HashtableIntTest, InsertRange) {
  // We just test the ints here, to make the placement-new easier.
  TypeParam ht_source;
  ht_source.insert(this->UniqueObject(10));
  ht_source.insert(this->UniqueObject(100));
  ht_source.insert(this->UniqueObject(1000));
  ht_source.insert(this->UniqueObject(10000));
  ht_source.insert(this->UniqueObject(100000));
  ht_source.insert(this->UniqueObject(1000000));

  const typename TypeParam::value_type input[] = {
    // This is a copy of the first element in ht_source.
    *ht_source.begin(),
    this->UniqueObject(2),
    this->UniqueObject(4),
    this->UniqueObject(8)
  };

  set<typename TypeParam::value_type> set_input;
  set_input.insert(this->UniqueObject(1111111));
  set_input.insert(this->UniqueObject(111111));
  set_input.insert(this->UniqueObject(11111));
  set_input.insert(this->UniqueObject(1111));
  set_input.insert(this->UniqueObject(111));
  set_input.insert(this->UniqueObject(11));

  // Insert from ht_source, an iterator of the same type as us.
  typename TypeParam::const_iterator begin = ht_source.begin();
  typename TypeParam::const_iterator end = begin;
  std::advance(end, 3);
  this->ht_.insert(begin, end);   // insert 3 elements from ht_source
  EXPECT_EQ(3u, this->ht_.size());
  EXPECT_TRUE(*this->ht_.begin() == this->UniqueObject(10) ||
              *this->ht_.begin() == this->UniqueObject(100) ||
              *this->ht_.begin() == this->UniqueObject(1000) ||
              *this->ht_.begin() == this->UniqueObject(10000) ||
              *this->ht_.begin() == this->UniqueObject(100000) ||
              *this->ht_.begin() == this->UniqueObject(1000000));

  // And insert from set_input, a separate, non-random-access iterator.
  typename set<typename TypeParam::value_type>::const_iterator set_begin;
  typename set<typename TypeParam::value_type>::const_iterator set_end;
  set_begin = set_input.begin();
  set_end = set_begin;
  std::advance(set_end, 3);
  this->ht_.insert(set_begin, set_end);
  EXPECT_EQ(6u, this->ht_.size());

  // Insert from input as well, a separate, random-access iterator.
  // The first element of input overlaps with an existing element
  // of ht_, so this should only up the size by 2.
  this->ht_.insert(&input[0], &input[3]);
  EXPECT_EQ(8u, this->ht_.size());
}

TEST(HashtableTest, InsertValueToMap) {
  // For the maps in particular, ensure that inserting doesn't change
  // the value.
  sparse_hash_map<int, int> shm;
  pair<sparse_hash_map<int,int>::iterator, bool> shm_it;
  shm[1] = 2;   // test a different method of inserting
  shm_it = shm.insert(pair<int, int>(1, 3));
  EXPECT_EQ(false, shm_it.second);
  EXPECT_EQ(1, shm_it.first->first);
  EXPECT_EQ(2, shm_it.first->second);
  shm_it.first->second = 20;
  EXPECT_EQ(20, shm[1]);

  shm_it = shm.insert(pair<int, int>(2, 4));
  EXPECT_EQ(true, shm_it.second);
  EXPECT_EQ(2, shm_it.first->first);
  EXPECT_EQ(4, shm_it.first->second);
  EXPECT_EQ(4, shm[2]);

  // Do it all again, with dense_hash_map.
  dense_hash_map<int, int> dhm;
  dhm.set_empty_key(0);
  pair<dense_hash_map<int,int>::iterator, bool> dhm_it;
  dhm[1] = 2;   // test a different method of inserting
  dhm_it = dhm.insert(pair<const int, int>(1, 3));
  EXPECT_EQ(false, dhm_it.second);
  EXPECT_EQ(1, dhm_it.first->first);
  EXPECT_EQ(2, dhm_it.first->second);
  dhm_it.first->second = 20;
  EXPECT_EQ(20, dhm[1]);

  dhm_it = dhm.insert(pair<const int, int>(2, 4));
  EXPECT_EQ(true, dhm_it.second);
  EXPECT_EQ(2, dhm_it.first->first);
  EXPECT_EQ(4, dhm_it.first->second);
  EXPECT_EQ(4, dhm[2]);
}

TYPED_TEST(HashtableStringTest, EmptyKey) {
  // Only run the string tests, to make it easier to know what the
  // empty key should be.
  if (!this->ht_.supports_empty_key())
    return;
  EXPECT_EQ(kEmptyString, this->ht_.empty_key());
}

TYPED_TEST(HashtableAllTest, DeletedKey) {
  if (!this->ht_.supports_deleted_key())
    return;
  this->ht_.insert(this->UniqueObject(10));
  this->ht_.insert(this->UniqueObject(20));
  this->ht_.set_deleted_key(this->UniqueKey(1));
  EXPECT_EQ(this->ht_.deleted_key(), this->UniqueKey(1));
  EXPECT_EQ(2u, this->ht_.size());
  this->ht_.erase(this->UniqueKey(20));
  EXPECT_EQ(1u, this->ht_.size());

  // Changing the deleted key is fine.
  this->ht_.set_deleted_key(this->UniqueKey(2));
  EXPECT_EQ(this->ht_.deleted_key(), this->UniqueKey(2));
  EXPECT_EQ(1u, this->ht_.size());
}

TYPED_TEST(HashtableAllTest, Erase) {
  this->ht_.set_deleted_key(this->UniqueKey(1));
  EXPECT_EQ(0u, this->ht_.erase(this->UniqueKey(20)));
  this->ht_.insert(this->UniqueObject(10));
  this->ht_.insert(this->UniqueObject(20));
  EXPECT_EQ(1u, this->ht_.erase(this->UniqueKey(20)));
  EXPECT_EQ(1u, this->ht_.size());
  EXPECT_EQ(0u, this->ht_.erase(this->UniqueKey(20)));
  EXPECT_EQ(1u, this->ht_.size());
  EXPECT_EQ(0u, this->ht_.erase(this->UniqueKey(19)));
  EXPECT_EQ(1u, this->ht_.size());

  typename TypeParam::iterator it = this->ht_.find(this->UniqueKey(10));
  EXPECT_TRUE(it != this->ht_.end());
  this->ht_.erase(it);
  EXPECT_EQ(0u, this->ht_.size());

  for (int i = 10; i < 100; i++)
    this->ht_.insert(this->UniqueObject(i));
  EXPECT_EQ(90u, this->ht_.size());
  this->ht_.erase(this->ht_.begin(), this->ht_.end());
  EXPECT_EQ(0u, this->ht_.size());
}

TYPED_TEST(HashtableAllTest, EraseDoesNotResize) {
  this->ht_.set_deleted_key(this->UniqueKey(1));
  for (int i = 10; i < 2000; i++) {
    this->ht_.insert(this->UniqueObject(i));
  }
  const typename TypeParam::size_type old_count = this->ht_.bucket_count();
  for (int i = 10; i < 1000; i++) {                 // erase half one at a time
    EXPECT_EQ(1u, this->ht_.erase(this->UniqueKey(i)));
  }
  this->ht_.erase(this->ht_.begin(), this->ht_.end());  // and the rest at once
  EXPECT_EQ(0u, this->ht_.size());
  EXPECT_EQ(old_count, this->ht_.bucket_count());
}

TYPED_TEST(HashtableAllTest, Equals) {
  // The real test here is whether two hashtables are equal if they
  // have the same items but in a different order.
  TypeParam ht1;
  TypeParam ht2;

  EXPECT_TRUE(ht1 == ht1);
  EXPECT_FALSE(ht1 != ht1);
  EXPECT_TRUE(ht1 == ht2);
  EXPECT_FALSE(ht1 != ht2);
  ht1.set_deleted_key(this->UniqueKey(1));
  // Only the contents affect equality, not things like deleted-key.
  EXPECT_TRUE(ht1 == ht2);
  EXPECT_FALSE(ht1 != ht2);
  ht1.resize(2000);
  EXPECT_TRUE(ht1 == ht2);

  // The choice of allocator/etc doesn't matter either.
  Hasher hasher(1);
  Alloc<typename TypeParam::key_type> alloc(2, NULL);
  TypeParam ht3(5, hasher, hasher, alloc);
  EXPECT_TRUE(ht1 == ht3);
  EXPECT_FALSE(ht1 != ht3);

  ht1.insert(this->UniqueObject(2));
  EXPECT_TRUE(ht1 != ht2);
  EXPECT_FALSE(ht1 == ht2);   // this should hold as well!

  ht2.insert(this->UniqueObject(2));
  EXPECT_TRUE(ht1 == ht2);

  for (int i = 3; i <= 2000; i++) {
    ht1.insert(this->UniqueObject(i));
  }
  for (int i = 2000; i >= 3; i--) {
    ht2.insert(this->UniqueObject(i));
  }
  EXPECT_TRUE(ht1 == ht2);
}

TEST(HashtableTest, IntIO) {
  // Since the set case is just a special (easier) case than the map case, I
  // just test on sparse_hash_map.  This handles the easy case where we can
  // use the standard reader and writer.
  sparse_hash_map<int, int> ht_out;
  ht_out.set_deleted_key(0);
  for (int i = 1; i < 1000; i++) {
    ht_out[i] = i * i;
  }
  ht_out.erase(563);   // just to test having some erased keys when we write.
  ht_out.erase(22);

  string file(TmpFile("intio"));
  FILE* fp = fopen(file.c_str(), "wb");
  EXPECT_TRUE(fp != NULL);
  EXPECT_TRUE(ht_out.write_metadata(fp));
  EXPECT_TRUE(ht_out.write_nopointer_data(fp));
  fclose(fp);

  sparse_hash_map<int, int> ht_in;
  fp = fopen(file.c_str(), "rb");
  EXPECT_TRUE(fp != NULL);
  EXPECT_TRUE(ht_in.read_metadata(fp));
  EXPECT_TRUE(ht_in.read_nopointer_data(fp));
  fclose(fp);

  EXPECT_EQ(1, ht_in[1]);
  EXPECT_EQ(998001, ht_in[999]);
  EXPECT_EQ(100, ht_in[10]);
  EXPECT_EQ(441, ht_in[21]);
  EXPECT_EQ(0, ht_in[22]);    // should not have been saved
  EXPECT_EQ(0, ht_in[563]);
}

TEST(HashtableTest, StringIO) {
  // Since the set case is just a special (easier) case than the map case,
  // I just test on sparse_hash_map.  This handles the difficult case where
  // we have to write our own custom reader/writer for the data.
  sparse_hash_map<string, string, Hasher, Hasher> ht_out;
  ht_out.set_deleted_key(string(""));
  for (int i = 32; i < 128; i++) {
    // This maps 'a' to 32 a's, 'b' to 33 b's, etc.
    ht_out[string(1, i)] = string(i, i);
  }
  ht_out.erase("c");   // just to test having some erased keys when we write.
  ht_out.erase("y");

  string file(TmpFile("stringio"));
  FILE* fp = fopen(file.c_str(), "wb");
  EXPECT_TRUE(fp != NULL);
  EXPECT_TRUE(ht_out.write_metadata(fp));
  for (sparse_hash_map<string, string, Hasher, Hasher>::const_iterator
           it = ht_out.begin(); it != ht_out.end(); ++it) {
    const string::size_type first_size = it->first.length();
    fwrite(&first_size, sizeof(first_size), 1, fp); // ignore endianness issues
    fwrite(it->first.c_str(), first_size, 1, fp);

    const string::size_type second_size = it->second.length();
    fwrite(&second_size, sizeof(second_size), 1, fp);
    fwrite(it->second.c_str(), second_size, 1, fp);
  }
  fclose(fp);

  sparse_hash_map<string, string, Hasher, Hasher> ht_in;
  fp = fopen(file.c_str(), "rb");
  EXPECT_TRUE(fp != NULL);
  EXPECT_TRUE(ht_in.read_metadata(fp));
  for (sparse_hash_map<string, string, Hasher, Hasher>::iterator
           it = ht_in.begin(); it != ht_in.end(); ++it) {
    string::size_type first_size;
    EXPECT_EQ(1u, fread(&first_size, sizeof(first_size), 1, fp));
    char* first = new char[first_size];
    EXPECT_EQ(1u, fread(first, first_size, 1, fp));

    string::size_type second_size;
    EXPECT_EQ(1u, fread(&second_size, sizeof(second_size), 1, fp));
    char* second = new char[second_size];
    EXPECT_EQ(1u, fread(second, second_size, 1, fp));

    // it points to garbage, so we have to use placement-new to initialize.
    // We also have to use const-cast since it->first is const.
    new(const_cast<string*>(&it->first)) string(first, first_size);
    new(&it->second) string(second, second_size);
    delete[] first;
    delete[] second;
  }
  fclose(fp);

  EXPECT_EQ(string("                                "), ht_in[" "]);
  EXPECT_EQ(string("+++++++++++++++++++++++++++++++++++++++++++"), ht_in["+"]);
  EXPECT_EQ(string(""), ht_in["c"]);    // should not have been saved
  EXPECT_EQ(string(""), ht_in["y"]);
}

TYPED_TEST(HashtableAllTest, Serialization) {
  if (!this->ht_.supports_serialization()) return;
  TypeParam ht_out;
  ht_out.set_deleted_key(this->UniqueKey(2000));
  for (int i = 1; i < 100; i++) {
    ht_out.insert(this->UniqueObject(i));
  }
  // just to test having some erased keys when we write.
  ht_out.erase(this->UniqueKey(56));
  ht_out.erase(this->UniqueKey(22));

  string file(TmpFile("serialization"));
  FILE* fp = fopen(file.c_str(), "wb");
  EXPECT_TRUE(fp != NULL);
  EXPECT_TRUE(ht_out.serialize(ValueSerializer(), fp));
  fclose(fp);

  TypeParam ht_in;
  fp = fopen(file.c_str(), "rb");
  EXPECT_TRUE(fp != NULL);
  EXPECT_TRUE(ht_in.unserialize(ValueSerializer(), fp));
  fclose(fp);

  EXPECT_EQ(this->UniqueObject(1), *ht_in.find(this->UniqueKey(1)));
  EXPECT_EQ(this->UniqueObject(99), *ht_in.find(this->UniqueKey(99)));
  EXPECT_FALSE(ht_in.count(this->UniqueKey(100)));
  EXPECT_EQ(this->UniqueObject(21), *ht_in.find(this->UniqueKey(21)));
  // should not have been saved
  EXPECT_FALSE(ht_in.count(this->UniqueKey(22)));
  EXPECT_FALSE(ht_in.count(this->UniqueKey(56)));
}

TYPED_TEST(HashtableIntTest, NopointerSerialization) {
  if (!this->ht_.supports_serialization()) return;
  TypeParam ht_out;
  ht_out.set_deleted_key(this->UniqueKey(2000));
  for (int i = 1; i < 100; i++) {
    ht_out.insert(this->UniqueObject(i));
  }
  // just to test having some erased keys when we write.
  ht_out.erase(this->UniqueKey(56));
  ht_out.erase(this->UniqueKey(22));

  string file(TmpFile("nopointer_serialization"));
  FILE* fp = fopen(file.c_str(), "wb");
  EXPECT_TRUE(fp != NULL);
  EXPECT_TRUE(ht_out.serialize(typename TypeParam::NopointerSerializer(), fp));
  fclose(fp);

  TypeParam ht_in;
  fp = fopen(file.c_str(), "rb");
  EXPECT_TRUE(fp != NULL);
  EXPECT_TRUE(ht_in.unserialize(typename TypeParam::NopointerSerializer(), fp));
  fclose(fp);

  EXPECT_EQ(this->UniqueObject(1), *ht_in.find(this->UniqueKey(1)));
  EXPECT_EQ(this->UniqueObject(99), *ht_in.find(this->UniqueKey(99)));
  EXPECT_FALSE(ht_in.count(this->UniqueKey(100)));
  EXPECT_EQ(this->UniqueObject(21), *ht_in.find(this->UniqueKey(21)));
  // should not have been saved
  EXPECT_FALSE(ht_in.count(this->UniqueKey(22)));
  EXPECT_FALSE(ht_in.count(this->UniqueKey(56)));
}

// We don't support serializing to a string by default, but you can do
// it by writing your own custom input/output class.
class StringIO {
 public:
  explicit StringIO(string* s) : s_(s) {}
  size_t Write(const void* buf, size_t len) {
    s_->append(reinterpret_cast<const char*>(buf), len);
    return len;
  }
  size_t Read(void* buf, size_t len) {
    if (s_->length() < len)
      len = s_->length();
    memcpy(reinterpret_cast<char*>(buf), s_->data(), len);
    s_->erase(0, len);
    return len;
  }
 private:
  string* const s_;
};

TYPED_TEST(HashtableIntTest, SerializingToString) {
  if (!this->ht_.supports_serialization()) return;
  TypeParam ht_out;
  ht_out.set_deleted_key(this->UniqueKey(2000));
  for (int i = 1; i < 100; i++) {
    ht_out.insert(this->UniqueObject(i));
  }
  // just to test having some erased keys when we write.
  ht_out.erase(this->UniqueKey(56));
  ht_out.erase(this->UniqueKey(22));

  string stringbuf;
  StringIO stringio(&stringbuf);
  EXPECT_TRUE(ht_out.serialize(typename TypeParam::NopointerSerializer(),
                               &stringio));

  TypeParam ht_in;
  EXPECT_TRUE(ht_in.unserialize(typename TypeParam::NopointerSerializer(),
                                &stringio));

  EXPECT_EQ(this->UniqueObject(1), *ht_in.find(this->UniqueKey(1)));
  EXPECT_EQ(this->UniqueObject(99), *ht_in.find(this->UniqueKey(99)));
  EXPECT_FALSE(ht_in.count(this->UniqueKey(100)));
  EXPECT_EQ(this->UniqueObject(21), *ht_in.find(this->UniqueKey(21)));
  // should not have been saved
  EXPECT_FALSE(ht_in.count(this->UniqueKey(22)));
  EXPECT_FALSE(ht_in.count(this->UniqueKey(56)));
}

// An easier way to do the above would be to use the existing stream methods.
TYPED_TEST(HashtableIntTest, SerializingToStringStream) {
  if (!this->ht_.supports_serialization()) return;
  TypeParam ht_out;
  ht_out.set_deleted_key(this->UniqueKey(2000));
  for (int i = 1; i < 100; i++) {
    ht_out.insert(this->UniqueObject(i));
  }
  // just to test having some erased keys when we write.
  ht_out.erase(this->UniqueKey(56));
  ht_out.erase(this->UniqueKey(22));

  std::stringstream string_buffer;
  EXPECT_TRUE(ht_out.serialize(typename TypeParam::NopointerSerializer(),
                               &string_buffer));

  TypeParam ht_in;
  EXPECT_TRUE(ht_in.unserialize(typename TypeParam::NopointerSerializer(),
                                &string_buffer));

  EXPECT_EQ(this->UniqueObject(1), *ht_in.find(this->UniqueKey(1)));
  EXPECT_EQ(this->UniqueObject(99), *ht_in.find(this->UniqueKey(99)));
  EXPECT_FALSE(ht_in.count(this->UniqueKey(100)));
  EXPECT_EQ(this->UniqueObject(21), *ht_in.find(this->UniqueKey(21)));
  // should not have been saved
  EXPECT_FALSE(ht_in.count(this->UniqueKey(22)));
  EXPECT_FALSE(ht_in.count(this->UniqueKey(56)));
}

// Verify that the metadata serialization is endianness and word size
// agnostic.
TYPED_TEST(HashtableAllTest, MetadataSerializationAndEndianness) {
  TypeParam ht_out;
  string kExpectedDense("\x13W\x86""B\0\0\0\0\0\0\0 \0\0\0\0\0\0\0\0\0\0\0\0",
                        24);
  string kExpectedSparse("$hu1\0\0\0 \0\0\0\0\0\0\0\0\0\0\0\0", 20);

  if (ht_out.supports_readwrite()) {
    string file(TmpFile("metadata_serialization"));
    FILE* fp = fopen(file.c_str(), "wb");
    EXPECT_TRUE(fp != NULL);

    EXPECT_TRUE(ht_out.write_metadata(fp));
    EXPECT_TRUE(ht_out.write_nopointer_data(fp));

    const size_t num_bytes = ftell(fp);
    fclose(fp);
    fp = fopen(file.c_str(), "rb");
    EXPECT_LE(num_bytes, static_cast<size_t>(24));
    char contents[24];
    EXPECT_EQ(num_bytes, fread(contents, 1, num_bytes, fp));
    EXPECT_EQ(EOF, fgetc(fp));       // check we're *exactly* the right size
    fclose(fp);
    // TODO(csilvers): check type of ht_out instead of looking at the 1st byte.
    if (contents[0] == kExpectedDense[0]) {
      EXPECT_EQ(kExpectedDense, string(contents, num_bytes));
    } else {
      EXPECT_EQ(kExpectedSparse, string(contents, num_bytes));
    }
  }

  // Do it again with new-style serialization.  Here we can use StringIO.
  if (ht_out.supports_serialization()) {
    string stringbuf;
    StringIO stringio(&stringbuf);
    EXPECT_TRUE(ht_out.serialize(typename TypeParam::NopointerSerializer(),
                                 &stringio));
    if (stringbuf[0] == kExpectedDense[0]) {
      EXPECT_EQ(kExpectedDense, stringbuf);
    } else {
      EXPECT_EQ(kExpectedSparse, stringbuf);
    }
  }
}


// ------------------------------------------------------------------------
// The above tests test the general API for correctness.  These tests
// test a few corner cases that have tripped us up in the past, and
// more general, cross-API issues like memory management.

TYPED_TEST(HashtableAllTest, BracketOperatorCrashing) {
  this->ht_.set_deleted_key(this->UniqueKey(1));
  for (int iters = 0; iters < 10; iters++) {
    // We start at 33 because after shrinking, we'll be at 32 buckets.
    for (int i = 33; i < 133; i++) {
      this->ht_.bracket_assign(this->UniqueKey(i),
                               this->ht_.get_data(this->UniqueObject(i)));
    }
    this->ht_.clear_no_resize();
    // This will force a shrink on the next insert, which we want to test.
    this->ht_.bracket_assign(this->UniqueKey(2),
                             this->ht_.get_data(this->UniqueObject(2)));
    this->ht_.erase(this->UniqueKey(2));
  }
}

// For data types with trivial copy-constructors and destructors, we
// should use an optimized routine for data-copying, that involves
// memmove.  We test this by keeping count of how many times the
// copy-constructor is called; it should be much less with the
// optimized code.
struct Memmove {
 public:
  Memmove(): i(0) {}
  explicit Memmove(int ival): i(ival) {}
  Memmove(const Memmove& that) { this->i = that.i; num_copies++; }
  int i;
  static int num_copies;
};
int Memmove::num_copies = 0;

struct NoMemmove {
 public:
  NoMemmove(): i(0) {}
  explicit NoMemmove(int ival): i(ival) {}
  NoMemmove(const NoMemmove& that) { this->i = that.i; num_copies++; }
  int i;
  static int num_copies;
};
int NoMemmove::num_copies = 0;

} // unnamed namespace

// This is what tells the hashtable code it can use memmove for this class:
_START_GOOGLE_NAMESPACE_
template<> struct has_trivial_copy<Memmove> : true_type { };
template<> struct has_trivial_destructor<Memmove> : true_type { };
_END_GOOGLE_NAMESPACE_

namespace {

TEST(HashtableTest, SimpleDataTypeOptimizations) {
  // Only sparsehashtable optimizes moves in this way.
  sparse_hash_map<int, Memmove, Hasher, Hasher> memmove;
  sparse_hash_map<int, NoMemmove, Hasher, Hasher> nomemmove;
  sparse_hash_map<int, Memmove, Hasher, Hasher, Alloc<int> >
      memmove_nonstandard_alloc;

  Memmove::num_copies = 0;
  for (int i = 10000; i > 0; i--) {
    memmove[i] = Memmove(i);
  }
  const int memmove_copies = Memmove::num_copies;

  NoMemmove::num_copies = 0;
  for (int i = 10000; i > 0; i--) {
    nomemmove[i] = NoMemmove(i);
  }
  const int nomemmove_copies = NoMemmove::num_copies;

  Memmove::num_copies = 0;
  for (int i = 10000; i > 0; i--) {
    memmove_nonstandard_alloc[i] = Memmove(i);
  }
  const int memmove_nonstandard_alloc_copies = Memmove::num_copies;

  EXPECT_GT(nomemmove_copies, memmove_copies);
  EXPECT_EQ(nomemmove_copies, memmove_nonstandard_alloc_copies);
}

TYPED_TEST(HashtableAllTest, ResizeHysteresis) {
  // We want to make sure that when we create a hashtable, and then
  // add and delete one element, the size of the hashtable doesn't
  // change.
  this->ht_.set_deleted_key(this->UniqueKey(1));
  typename TypeParam::size_type old_bucket_count = this->ht_.bucket_count();
  this->ht_.insert(this->UniqueObject(4));
  this->ht_.erase(this->UniqueKey(4));
  this->ht_.insert(this->UniqueObject(4));
  this->ht_.erase(this->UniqueKey(4));
  EXPECT_EQ(old_bucket_count, this->ht_.bucket_count());

  // Try it again, but with a hashtable that starts very small
  TypeParam ht(2);
  EXPECT_LT(ht.bucket_count(), 32u);   // verify we really do start small
  ht.set_deleted_key(this->UniqueKey(1));
  old_bucket_count = ht.bucket_count();
  ht.insert(this->UniqueObject(4));
  ht.erase(this->UniqueKey(4));
  ht.insert(this->UniqueObject(4));
  ht.erase(this->UniqueKey(4));
  EXPECT_EQ(old_bucket_count, ht.bucket_count());
}

TEST(HashtableTest, ConstKey) {
  // Sometimes people write hash_map<const int, int>, even though the
  // const isn't necessary.  Make sure we handle this cleanly.
  sparse_hash_map<const int, int, Hasher, Hasher> shm;
  shm.set_deleted_key(1);
  shm[10] = 20;

  dense_hash_map<const int, int, Hasher, Hasher> dhm;
  dhm.set_empty_key(1);
  dhm.set_deleted_key(2);
  dhm[10] = 20;
}

TYPED_TEST(HashtableAllTest, ResizeActuallyResizes) {
  // This tests for a problem we had where we could repeatedly "resize"
  // a hashtable to the same size it was before, on every insert.
  const typename TypeParam::size_type kSize = 1<<10;   // Pick any power of 2
  const float kResize = 0.8f;    // anything between 0.5 and 1 is fine.
  const int kThreshold = static_cast<int>(kSize * kResize - 1);
  this->ht_.set_resizing_parameters(0, kResize);
  this->ht_.set_deleted_key(this->UniqueKey(kThreshold + 100));

  // Get right up to the resizing threshold.
  for (int i = 0; i <= kThreshold; i++) {
    this->ht_.insert(this->UniqueObject(i+1));
  }
  // The bucket count should equal kSize.
  EXPECT_EQ(kSize, this->ht_.bucket_count());

  // Now start doing erase+insert pairs.  This should cause us to
  // copy the hashtable at most once.
  const int pre_copies = this->ht_.num_table_copies();
  for (int i = 0; i < static_cast<int>(kSize); i++) {
    this->ht_.erase(this->UniqueKey(kThreshold));
    this->ht_.insert(this->UniqueObject(kThreshold));
  }
  EXPECT_LT(this->ht_.num_table_copies(), pre_copies + 2);

  // Now create a hashtable where we go right to the threshold, then
  // delete everything and do one insert.  Even though our hashtable
  // is now tiny, we should still have at least kSize buckets, because
  // our shrink threshhold is 0.
  TypeParam ht2;
  ht2.set_deleted_key(this->UniqueKey(kThreshold + 100));
  ht2.set_resizing_parameters(0, kResize);
  EXPECT_LT(ht2.bucket_count(), kSize);
  for (int i = 0; i <= kThreshold; i++) {
    ht2.insert(this->UniqueObject(i+1));
  }
  EXPECT_EQ(ht2.bucket_count(), kSize);
  for (int i = 0; i <= kThreshold; i++) {
    ht2.erase(this->UniqueKey(i+1));
    EXPECT_EQ(ht2.bucket_count(), kSize);
  }
  ht2.insert(this->UniqueObject(kThreshold+2));
  EXPECT_GE(ht2.bucket_count(), kSize);
}

template<typename T> class DenseIntMap : public dense_hash_map<int, T> {
 public:
  DenseIntMap() { this->set_empty_key(0); }
};

class DenseStringSet : public dense_hash_set<string, Hasher, Hasher> {
 public:
  DenseStringSet() { this->set_empty_key(string("")); }
};

TEST(HashtableTest, NestedHashtables) {
  // People can do better than to have a hash_map of hash_maps, but we
  // should still support it.  I try a few different mappings.
  sparse_hash_map<string, sparse_hash_map<int, string>, Hasher, Hasher> ht1;
  sparse_hash_map<string, DenseStringSet, Hasher, Hasher> ht2;
  dense_hash_map<int, DenseIntMap<int>, Hasher, Hasher> ht3;
  ht3.set_empty_key(0);

  ht1["hi"];   // create a sub-ht with the default values
  ht1["lo"][1] = "there";
  sparse_hash_map<string, sparse_hash_map<int, string>, Hasher, Hasher>
      ht1copy = ht1;

  ht2["hi"];
  ht2["hi"].insert("lo");
  sparse_hash_map<string, DenseStringSet, Hasher, Hasher> ht2copy = ht2;

  ht3[1];
  ht3[2][3] = 4;
  dense_hash_map<int, DenseIntMap<int>, Hasher, Hasher> ht3copy = ht3;
}

TEST(HashtableTest, ResizeWithoutShrink) {
  const size_t N = 1000000L;
  const size_t max_entries = 40;
#define KEY(i, j)  (i * 4 + j) * 28 + 11

  dense_hash_map<size_t, int> ht;
  ht.set_empty_key(0);
  ht.set_deleted_key(1);
  ht.min_load_factor(0);
  ht.max_load_factor(0.2);

  for (size_t i = 0; i < N; ++i) {
    for (size_t j = 0; j < max_entries; ++j) {
      size_t key = KEY(i, j);
      ht[key] = 0;
    }
    for (size_t j = 0; j < max_entries / 2; ++j) {
      size_t key = KEY(i, j);
      ht.erase(key);
      ht[key + 1] = 0;
    }
    for (size_t j = 0; j < max_entries; ++j) {
      size_t key = KEY(i, j);
      ht.erase(key);
      ht.erase(key + (j < max_entries / 2));
    }
    EXPECT_LT(ht.bucket_count(), 4096);
  }
}

TEST(HashtableDeathTest, ResizeOverflow) {
  dense_hash_map<int, int> ht;
  EXPECT_DEATH(ht.resize(static_cast<size_t>(-1)),
               "overflows size_type");

  sparse_hash_map<int, int> ht2;
  EXPECT_DEATH(ht2.resize(static_cast<size_t>(-1)),
               "overflows size_type");
}

TEST(HashtableDeathTest, InsertSizeTypeOverflow) {
  static const int kMax = 256;
  vector<int> test_data(kMax);
  for (int i = 0; i < kMax; ++i) {
    test_data[i] = i+1000;
  }

  sparse_hash_set<int, Hasher, Hasher, Alloc<int, uint8, 10> > shs;
  dense_hash_set<int, Hasher, Hasher, Alloc<int, uint8, 10> > dhs;
  dhs.set_empty_key(-1);

  // Test we are using the correct allocator
  EXPECT_TRUE(shs.get_allocator().is_custom_alloc());
  EXPECT_TRUE(dhs.get_allocator().is_custom_alloc());

  // Test size_type overflow in insert(it, it)
  EXPECT_DEATH(dhs.insert(test_data.begin(), test_data.end()),
               "overflows size_type");
  EXPECT_DEATH(shs.insert(test_data.begin(), test_data.end()),
               "overflows size_type");
}

TEST(HashtableDeathTest, InsertMaxSizeOverflow) {
  static const int kMax = 256;
  vector<int> test_data(kMax);
  for (int i = 0; i < kMax; ++i) {
    test_data[i] = i+1000;
  }

  sparse_hash_set<int, Hasher, Hasher, Alloc<int, uint8, 10> > shs;
  dense_hash_set<int, Hasher, Hasher, Alloc<int, uint8, 10> > dhs;
  dhs.set_empty_key(-1);

  // Test max_size overflow
  EXPECT_DEATH(dhs.insert(test_data.begin(), test_data.begin() + 11),
               "exceed max_size");
  EXPECT_DEATH(shs.insert(test_data.begin(), test_data.begin() + 11),
               "exceed max_size");
}

TEST(HashtableDeathTest, ResizeSizeTypeOverflow) {
  // Test min-buckets overflow, when we want to resize too close to size_type
  sparse_hash_set<int, Hasher, Hasher, Alloc<int, uint8, 10> > shs;
  dense_hash_set<int, Hasher, Hasher, Alloc<int, uint8, 10> > dhs;
  dhs.set_empty_key(-1);

  EXPECT_DEATH(dhs.resize(250), "overflows size_type");  // 9+250 > 256
  EXPECT_DEATH(shs.resize(250), "overflows size_type");
}

TEST(HashtableDeathTest, ResizeDeltaOverflow) {
  static const int kMax = 256;
  vector<int> test_data(kMax);
  for (int i = 0; i < kMax; ++i) {
    test_data[i] = i+1000;
  }

  sparse_hash_set<int, Hasher, Hasher, Alloc<int, uint8, 255> > shs;
  dense_hash_set<int, Hasher, Hasher, Alloc<int, uint8, 255> > dhs;
  dhs.set_empty_key(-1);
  for (int i = 0; i < 9; i++) {
    dhs.insert(i);
    shs.insert(i);
  }
  EXPECT_DEATH(dhs.insert(test_data.begin(), test_data.begin() + 250),
               "overflows size_type");              // 9+250 > 256
  EXPECT_DEATH(shs.insert(test_data.begin(), test_data.begin() + 250),
               "overflows size_type");
}

// ------------------------------------------------------------------------
// This informational "test" comes last so it's easy to see.
// Also, benchmarks.

TYPED_TEST(HashtableAllTest, ClassSizes) {
  std::cout << "sizeof(" << typeid(TypeParam).name() << "): "
            << sizeof(this->ht_) << "\n";
}

}  // unnamed namespace

int main(int, char **) {
  // All the work is done in the static constructors.  If they don't
  // die, the tests have all passed.
  cout << "PASS\n";
  return 0;
}
