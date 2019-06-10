// Copyright 2006-2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
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

#include "v8.h"

#include "accessors.h"
#include "api.h"
#include "execution.h"
#include "global-handles.h"
#include "ic-inl.h"
#include "natives.h"
#include "platform.h"
#include "runtime.h"
#include "serialize.h"
#include "stub-cache.h"
#include "v8threads.h"

namespace v8 { namespace internal {

#ifdef DEBUG
DEFINE_bool(debug_serialization, false,
            "write debug information into the snapshot.");
#endif


// Encoding: a RelativeAddress must be able to fit in a pointer:
// it is encoded as an Address with (from MS to LS bits):
// 27 bits identifying a word in the space, in one of three formats:
// - MAP and OLD spaces: 16 bits of page number, 11 bits of word offset in page
// - NEW space:          27 bits of word offset
// - LO space:           27 bits of page number
// 3 bits to encode the AllocationSpace
// 2 bits identifying this as a HeapObject

const int kSpaceShift = kHeapObjectTagSize;
const int kSpaceBits = kSpaceTagSize;
const int kSpaceMask = kSpaceTagMask;

const int kOffsetShift = kSpaceShift + kSpaceBits;
const int kOffsetBits = 11;
const int kOffsetMask = (1 << kOffsetBits) - 1;

const int kPageBits = 32 - (kOffsetBits + kSpaceBits + kHeapObjectTagSize);
const int kPageShift = kOffsetShift + kOffsetBits;
const int kPageMask = (1 << kPageBits) - 1;

const int kPageAndOffsetShift = kOffsetShift;
const int kPageAndOffsetBits = kPageBits + kOffsetBits;
const int kPageAndOffsetMask = (1 << kPageAndOffsetBits) - 1;


static inline AllocationSpace Space(Address addr) {
  const int encoded = reinterpret_cast<int>(addr);
  return static_cast<AllocationSpace>((encoded >> kSpaceShift) & kSpaceMask);
}


static inline int PageIndex(Address addr) {
  const int encoded = reinterpret_cast<int>(addr);
  return (encoded >> kPageShift) & kPageMask;
}


static inline int PageOffset(Address addr) {
  const int encoded = reinterpret_cast<int>(addr);
  return ((encoded >> kOffsetShift) & kOffsetMask) << kObjectAlignmentBits;
}


static inline int NewSpaceOffset(Address addr) {
  const int encoded = reinterpret_cast<int>(addr);
  return ((encoded >> kPageAndOffsetShift) & kPageAndOffsetMask) <<
      kObjectAlignmentBits;
}


static inline int LargeObjectIndex(Address addr) {
  const int encoded = reinterpret_cast<int>(addr);
  return (encoded >> kPageAndOffsetShift) & kPageAndOffsetMask;
}


// A RelativeAddress encodes a heap address that is independent of
// the actual memory addresses in real heap. The general case (for the
// OLD, CODE and MAP spaces) is as a (space id, page number, page offset)
// triple. The NEW space has page number == 0, because there are no
// pages. The LARGE_OBJECT space has page offset = 0, since there is
// exactly one object per page.  RelativeAddresses are encodable as
// Addresses, so that they can replace the map() pointers of
// HeapObjects. The encoded Addresses are also encoded as HeapObjects
// and allow for marking (is_marked() see mark(), clear_mark()...) as
// used by the Mark-Compact collector.

class RelativeAddress {
 public:
  RelativeAddress(AllocationSpace space, int page_index, int page_offset)
  : space_(space), page_index_(page_index), page_offset_(page_offset) {}

  // Return the encoding of 'this' as an Address. Decode with constructor.
  Address Encode() const;

  AllocationSpace space() const { return space_; }
  int page_index() const { return page_index_; }
  int page_offset() const { return page_offset_; }

  bool in_paged_space() const {
    return space_ == CODE_SPACE || space_ == OLD_SPACE || space_ == MAP_SPACE;
  }

  void next_address(int offset) { page_offset_ += offset; }
  void next_page(int init_offset = 0) {
    page_index_++;
    page_offset_ = init_offset;
  }

#ifdef DEBUG
  void Verify();
#endif

 private:
  AllocationSpace space_;
  int page_index_;
  int page_offset_;
};


Address RelativeAddress::Encode() const {
  ASSERT(page_index_ >= 0);
  int word_offset = 0;
  int result = 0;
  switch (space_) {
    case MAP_SPACE:
    case OLD_SPACE:
    case CODE_SPACE:
      ASSERT_EQ(0, page_index_ & ~kPageMask);
      word_offset = page_offset_ >> kObjectAlignmentBits;
      ASSERT_EQ(0, word_offset & ~kOffsetMask);
      result = (page_index_ << kPageShift) | (word_offset << kOffsetShift);
      break;
    case NEW_SPACE:
      ASSERT_EQ(0, page_index_);
      word_offset = page_offset_ >> kObjectAlignmentBits;
      ASSERT_EQ(0, word_offset & ~kPageAndOffsetMask);
      result = word_offset << kPageAndOffsetShift;
      break;
    case LO_SPACE:
      ASSERT_EQ(0, page_offset_);
      ASSERT_EQ(0, page_index_ & ~kPageAndOffsetMask);
      result = page_index_ << kPageAndOffsetShift;
      break;
  }
  // OR in AllocationSpace and kHeapObjectTag
  ASSERT_EQ(0, space_ & ~kSpaceMask);
  result |= (space_ << kSpaceShift) | kHeapObjectTag;
  return reinterpret_cast<Address>(result);
}


#ifdef DEBUG
void RelativeAddress::Verify() {
  ASSERT(page_offset_ >= 0 && page_index_ >= 0);
  switch (space_) {
    case MAP_SPACE:
    case OLD_SPACE:
    case CODE_SPACE:
      ASSERT(Page::kObjectStartOffset <= page_offset_ &&
             page_offset_ <= Page::kPageSize);
      break;
    case NEW_SPACE:
      ASSERT(page_index_ == 0);
      break;
    case LO_SPACE:
      ASSERT(page_offset_ == 0);
      break;
  }
}
#endif

// A SimulatedHeapSpace simulates the allocation of objects in a page in
// the heap. It uses linear allocation - that is, it doesn't simulate the
// use of a free list. This simulated
// allocation must exactly match that done by Heap.

class SimulatedHeapSpace {
 public:
  // The default constructor initializes to an invalid state.
  SimulatedHeapSpace(): current_(LAST_SPACE, -1, -1) {}

  // Sets 'this' to the first address in 'space' that would be
  // returned by allocation in an empty heap.
  void InitEmptyHeap(AllocationSpace space);

  // Sets 'this' to the next address in 'space' that would be returned
  // by allocation in the current heap. Intended only for testing
  // serialization and deserialization in the current address space.
  void InitCurrentHeap(AllocationSpace space);

  // Returns the RelativeAddress where the next
  // object of 'size' bytes will be allocated, and updates 'this' to
  // point to the next free address beyond that object.
  RelativeAddress Allocate(int size);

 private:
  RelativeAddress current_;
};


void SimulatedHeapSpace::InitEmptyHeap(AllocationSpace space) {
  switch (space) {
    case MAP_SPACE:
    case OLD_SPACE:
    case CODE_SPACE:
      current_ = RelativeAddress(space, 0, Page::kObjectStartOffset);
      break;
    case NEW_SPACE:
    case LO_SPACE:
      current_ = RelativeAddress(space, 0, 0);
      break;
  }
}


void SimulatedHeapSpace::InitCurrentHeap(AllocationSpace space) {
  switch (space) {
    case MAP_SPACE:
    case OLD_SPACE:
    case CODE_SPACE: {
      PagedSpace* ps;
      if (space == MAP_SPACE) {
        ps = Heap::map_space();
      } else if (space == OLD_SPACE) {
        ps = Heap::old_space();
      } else {
        ASSERT(space == CODE_SPACE);
        ps = Heap::code_space();
      }
      Address top = ps->top();
      Page* top_page = Page::FromAllocationTop(top);
      int page_index = 0;
      PageIterator it(ps, PageIterator::PAGES_IN_USE);
      while (it.has_next()) {
        if (it.next() == top_page) break;
        page_index++;
      }
      current_ = RelativeAddress(space, page_index, top_page->Offset(top));
      break;
    }
    case NEW_SPACE:
      current_ =
        RelativeAddress(space, 0, Heap::NewSpaceTop() - Heap::NewSpaceStart());
      break;
    case LO_SPACE:
      int page_index = 0;
      for (LargeObjectIterator it(Heap::lo_space()); it.has_next(); it.next()) {
        page_index++;
      }
      current_ = RelativeAddress(space, page_index, 0);
      break;
  }
}


RelativeAddress SimulatedHeapSpace::Allocate(int size) {
#ifdef DEBUG
  current_.Verify();
#endif
  int alloc_size = OBJECT_SIZE_ALIGN(size);
  if (current_.in_paged_space() &&
      current_.page_offset() + alloc_size > Page::kPageSize) {
    ASSERT(alloc_size <= Page::kMaxHeapObjectSize);
    current_.next_page(Page::kObjectStartOffset);
  }
  RelativeAddress result = current_;
  if (current_.space() == LO_SPACE) {
    current_.next_page();
  } else {
    current_.next_address(alloc_size);
  }
#ifdef DEBUG
  current_.Verify();
  result.Verify();
#endif
  return result;
}

// -----------------------------------------------------------------------------
// Coding of external references.

// The encoding of an external reference. The type is in the high word.
// The id is in the low word.
static uint32_t EncodeExternal(TypeCode type, uint16_t id) {
  return static_cast<uint32_t>(type) << 16 | id;
}


static int* GetInternalPointer(StatsCounter* counter) {
  // All counters refer to dummy_counter, if deserializing happens without
  // setting up counters.
  static int dummy_counter = 0;
  return counter->Enabled() ? counter->GetInternalPointer() : &dummy_counter;
}


// ExternalReferenceTable is a helper class that defines the relationship
// between external references and their encodings. It is used to build
// hashmaps in ExternalReferenceEncoder and ExternalReferenceDecoder.
class ExternalReferenceTable {
 public:
  static ExternalReferenceTable* instance() {
    if (!instance_) instance_ = new ExternalReferenceTable();
    return instance_;
  }

  int size() const { return refs_.length(); }

  Address address(int i) { return refs_[i].address; }

  uint32_t code(int i) { return refs_[i].code; }

  const char* name(int i) { return refs_[i].name; }

  int max_id(int code) { return max_id_[code]; }

 private:
  static ExternalReferenceTable* instance_;

  ExternalReferenceTable();

  struct ExternalReferenceEntry {
    Address address;
    uint32_t code;
    const char* name;
  };

  void Add(Address address, TypeCode type, uint16_t id, const char* name) {
    CHECK_NE(NULL, address);
    ExternalReferenceEntry entry;
    entry.address = address;
    entry.code = EncodeExternal(type, id);
    entry.name = name;
    CHECK_NE(0, entry.code);
    refs_.Add(entry);
    if (id > max_id_[type]) max_id_[type] = id;
  }

  List<ExternalReferenceEntry> refs_;
  int max_id_[kTypeCodeCount];
};


ExternalReferenceTable* ExternalReferenceTable::instance_ = NULL;


ExternalReferenceTable::ExternalReferenceTable() : refs_(64) {
  for (int type_code = 0; type_code < kTypeCodeCount; type_code++) {
    max_id_[type_code] = 0;
  }

  // Define all entries in the table.

  // Builtins
#define DEF_ENTRY_C(name, ignore) \
  Add(Builtins::c_function_address(Builtins::c_##name), \
      C_BUILTIN, \
      Builtins::c_##name, \
      "Builtins::" #name);

  BUILTIN_LIST_C(DEF_ENTRY_C)
#undef DEF_ENTRY_C

#define DEF_ENTRY_C(name, ignore) \
  Add(Builtins::builtin_address(Builtins::name), \
      BUILTIN, \
      Builtins::name, \
      "Builtins::" #name);
#define DEF_ENTRY_A(name, kind, state) DEF_ENTRY_C(name, _)

  BUILTIN_LIST_C(DEF_ENTRY_C)
  BUILTIN_LIST_A(DEF_ENTRY_A)
#undef DEF_ENTRY_C
#undef DEF_ENTRY_A

  // Runtime functions
#define RUNTIME_ENTRY(name, nargs) \
  Add(Runtime::FunctionForId(Runtime::k##name)->entry, \
      RUNTIME_FUNCTION, \
      Runtime::k##name, \
      "Runtime::" #name);

  RUNTIME_FUNCTION_LIST(RUNTIME_ENTRY)
#undef RUNTIME_ENTRY

  // IC utilities
#define IC_ENTRY(name) \
  Add(IC::AddressFromUtilityId(IC::k##name), \
      IC_UTILITY, \
      IC::k##name, \
      "IC::" #name);

  IC_UTIL_LIST(IC_ENTRY)
#undef IC_ENTRY

  // Debug addresses
  Add(Debug_Address(Debug::k_after_break_target_address).address(),
      DEBUG_ADDRESS,
      Debug::k_after_break_target_address << kDebugIdShift,
      "Debug::after_break_target_address()");
  Add(Debug_Address(Debug::k_debug_break_return_address).address(),
      DEBUG_ADDRESS,
      Debug::k_debug_break_return_address << kDebugIdShift,
      "Debug::debug_break_return_address()");
  const char* debug_register_format = "Debug::register_address(%i)";
  size_t dr_format_length = strlen(debug_register_format);
  for (int i = 0; i < kNumJSCallerSaved; ++i) {
    char* name = NewArray<char>(dr_format_length + 1);
    OS::SNPrintF(name, dr_format_length, debug_register_format, i);
    Add(Debug_Address(Debug::k_register_address, i).address(),
        DEBUG_ADDRESS,
        Debug::k_register_address << kDebugIdShift | i,
        name);
  }

  // Stat counters
#define COUNTER_ENTRY(name, caption) \
  Add(reinterpret_cast<Address>(GetInternalPointer(&Counters::name)), \
      STATS_COUNTER, \
      Counters::k_##name, \
      "Counters::" #name);

  STATS_COUNTER_LIST_1(COUNTER_ENTRY)
  STATS_COUNTER_LIST_2(COUNTER_ENTRY)
#undef COUNTER_ENTRY

  // Top addresses
  const char* top_address_format = "Top::get_address_from_id(%i)";
  size_t top_format_length = strlen(top_address_format);
  for (uint16_t i = 0; i < Top::k_top_address_count; ++i) {
    char* name = NewArray<char>(top_format_length + 1);
    OS::SNPrintF(name, top_format_length, top_address_format, i);
    Add(Top::get_address_from_id((Top::AddressId)i), TOP_ADDRESS, i, name);
  }

  // Extensions
  Add(FUNCTION_ADDR(GCExtension::GC), EXTENSION, 1,
      "GCExtension::GC");
  Add(FUNCTION_ADDR(PrintExtension::Print), EXTENSION, 2,
      "PrintExtension::Print");
  Add(FUNCTION_ADDR(LoadExtension::Load), EXTENSION, 3,
      "LoadExtension::Load");
  Add(FUNCTION_ADDR(QuitExtension::Quit), EXTENSION, 4,
      "QuitExtension::Quit");

  // Accessors
#define ACCESSOR_DESCRIPTOR_DECLARATION(name) \
  Add((Address)&Accessors::name, \
      ACCESSOR, \
      Accessors::k##name, \
      "Accessors::" #name);

  ACCESSOR_DESCRIPTOR_LIST(ACCESSOR_DESCRIPTOR_DECLARATION)
#undef ACCESSOR_DESCRIPTOR_DECLARATION

  // Stub cache tables
  Add(SCTableReference::keyReference(StubCache::kPrimary).address(),
      STUB_CACHE_TABLE,
      1,
      "StubCache::primary_->key");
  Add(SCTableReference::valueReference(StubCache::kPrimary).address(),
      STUB_CACHE_TABLE,
      2,
      "StubCache::primary_->value");
  Add(SCTableReference::keyReference(StubCache::kSecondary).address(),
      STUB_CACHE_TABLE,
      3,
      "StubCache::secondary_->key");
  Add(SCTableReference::valueReference(StubCache::kSecondary).address(),
      STUB_CACHE_TABLE,
      4,
      "StubCache::secondary_->value");

  // Runtime entries
  Add(FUNCTION_ADDR(Runtime::PerformGC),
      RUNTIME_ENTRY,
      1,
      "Runtime::PerformGC");
  Add(FUNCTION_ADDR(StackFrameIterator::RestoreCalleeSavedForTopHandler),
      RUNTIME_ENTRY,
      2,
      "StackFrameIterator::RestoreCalleeSavedForTopHandler");

  // Miscellaneous
  Add(ExternalReference::builtin_passed_function().address(),
      UNCLASSIFIED,
      1,
      "Builtins::builtin_passed_function");
  Add(ExternalReference::the_hole_value_location().address(),
      UNCLASSIFIED,
      2,
      "Factory::the_hole_value().location()");
  Add(ExternalReference::address_of_stack_guard_limit().address(),
      UNCLASSIFIED,
      3,
      "StackGuard::address_of_limit()");
  Add(ExternalReference::debug_break().address(),
      UNCLASSIFIED,
      4,
      "Debug::Break()");
  Add(ExternalReference::new_space_start().address(),
      UNCLASSIFIED,
      5,
      "Heap::NewSpaceStart()");
  Add(ExternalReference::new_space_allocation_limit_address().address(),
      UNCLASSIFIED,
      6,
      "Heap::NewSpaceAllocationLimitAddress()");
  Add(ExternalReference::new_space_allocation_top_address().address(),
      UNCLASSIFIED,
      7,
      "Heap::NewSpaceAllocationTopAddress()");
  Add(ExternalReference::debug_step_in_fp_address().address(),
      UNCLASSIFIED,
      8,
      "Debug::step_in_fp_addr()");
}


ExternalReferenceEncoder::ExternalReferenceEncoder()
    : encodings_(Match) {
  ExternalReferenceTable* external_references =
      ExternalReferenceTable::instance();
  for (int i = 0; i < external_references->size(); ++i) {
    Put(external_references->address(i), i);
  }
}


uint32_t ExternalReferenceEncoder::Encode(Address key) const {
  int index = IndexOf(key);
  return index >=0 ? ExternalReferenceTable::instance()->code(index) : 0;
}


const char* ExternalReferenceEncoder::NameOfAddress(Address key) const {
  int index = IndexOf(key);
  return index >=0 ? ExternalReferenceTable::instance()->name(index) : NULL;
}


int ExternalReferenceEncoder::IndexOf(Address key) const {
  if (key == NULL) return -1;
  HashMap::Entry* entry =
      const_cast<HashMap &>(encodings_).Lookup(key, Hash(key), false);
  return entry == NULL ? -1 : reinterpret_cast<int>(entry->value);
}


void ExternalReferenceEncoder::Put(Address key, int index) {
  HashMap::Entry* entry = encodings_.Lookup(key, Hash(key), true);
  entry->value = reinterpret_cast<void *>(index);
}


ExternalReferenceDecoder::ExternalReferenceDecoder()
  : encodings_(NewArray<Address*>(kTypeCodeCount)) {
  ExternalReferenceTable* external_references =
      ExternalReferenceTable::instance();
  for (int type = kFirstTypeCode; type < kTypeCodeCount; ++type) {
    int max = external_references->max_id(type) + 1;
    encodings_[type] = NewArray<Address>(max + 1);
  }
  for (int i = 0; i < external_references->size(); ++i) {
    Put(external_references->code(i), external_references->address(i));
  }
}


ExternalReferenceDecoder::~ExternalReferenceDecoder() {
  for (int type = kFirstTypeCode; type < kTypeCodeCount; ++type) {
    DeleteArray(encodings_[type]);
  }
  DeleteArray(encodings_);
}


//------------------------------------------------------------------------------
// Implementation of Serializer


// Helper class to write the bytes of the serialized heap.

class SnapshotWriter {
 public:
  SnapshotWriter() {
    len_ = 0;
    max_ = 8 << 10;  // 8K initial size
    str_ = NewArray<char>(max_);
  }

  ~SnapshotWriter() {
    DeleteArray(str_);
  }

  void GetString(char** str, int* len) {
    *str = NewArray<char>(len_);
    memcpy(*str, str_, len_);
    *len = len_;
  }

  void Reserve(int bytes, int pos);

  void PutC(char c) {
    InsertC(c, len_);
  }

  void PutInt(int i) {
    InsertInt(i, len_);
  }

  void PutBytes(const byte* a, int size) {
    InsertBytes(a, len_, size);
  }

  void PutString(const char* s) {
    InsertString(s, len_);
  }

  int InsertC(char c, int pos) {
    Reserve(1, pos);
    str_[pos] = c;
    len_++;
    return pos + 1;
  }

  int InsertInt(int i, int pos) {
    return InsertBytes(reinterpret_cast<byte*>(&i), pos, sizeof(i));
  }

  int InsertBytes(const byte* a, int pos, int size) {
    Reserve(size, pos);
    memcpy(&str_[pos], a, size);
    len_ += size;
    return pos + size;
  }

  int InsertString(const char* s, int pos);

  int length() { return len_; }

 private:
  char* str_;  // the snapshot
  int len_;   // the curent length of str_
  int max_;   // the allocated size of str_
};


void SnapshotWriter::Reserve(int bytes, int pos) {
  CHECK(0 <= pos && pos <= len_);
  while (len_ + bytes >= max_) {
    max_ *= 2;
    char* old = str_;
    str_ = NewArray<char>(max_);
    memcpy(str_, old, len_);
    DeleteArray(old);
  }
  if (pos < len_) {
    char* old = str_;
    str_ = NewArray<char>(max_);
    memcpy(str_, old, pos);
    memcpy(str_ + pos + bytes, old + pos, len_ - pos);
    DeleteArray(old);
  }
}

int SnapshotWriter::InsertString(const char* s, int pos) {
  int size = strlen(s);
  pos = InsertC('[', pos);
  pos = InsertInt(size, pos);
  pos = InsertC(']', pos);
  return InsertBytes(reinterpret_cast<const byte*>(s), pos, size);
}


Serializer::Serializer()
  : global_handles_(4) {
  root_ = true;
  roots_ = 0;
  objects_ = 0;
  reference_encoder_ = NULL;
  writer_ = new SnapshotWriter();
  for (int i = 0; i <= LAST_SPACE; i++) {
    allocator_[i] = new SimulatedHeapSpace();
  }
}


Serializer::~Serializer() {
  for (int i = 0; i <= LAST_SPACE; i++) {
    delete allocator_[i];
  }
  if (reference_encoder_) delete reference_encoder_;
  delete writer_;
}


bool Serializer::serialization_enabled_ = true;


#ifdef DEBUG
static const int kMaxTagLength = 32;

void Serializer::Synchronize(const char* tag) {
  if (FLAG_debug_serialization) {
    int length = strlen(tag);
    ASSERT(length <= kMaxTagLength);
    writer_->PutC('S');
    writer_->PutInt(length);
    writer_->PutBytes(reinterpret_cast<const byte*>(tag), length);
  }
}
#endif


void Serializer::InitializeAllocators() {
  for (int i = 0; i <= LAST_SPACE; i++) {
    allocator_[i]->InitEmptyHeap(static_cast<AllocationSpace>(i));
  }
}


void Serializer::Serialize() {
  // No active threads.
  CHECK_EQ(NULL, ThreadState::FirstInUse());
  // No active or weak handles.
  CHECK(HandleScopeImplementer::instance()->Blocks()->is_empty());
  CHECK_EQ(0, GlobalHandles::NumberOfWeakHandles());
  // We need a counter function during serialization to resolve the
  // references to counters in the code on the heap.
  CHECK(StatsTable::HasCounterFunction());
  CHECK(enabled());
  InitializeAllocators();
  reference_encoder_ = new ExternalReferenceEncoder();
  PutHeader();
  Heap::IterateRoots(this);
  PutLog();
  PutContextStack();
  disable();
}


void Serializer::Finalize(char** str, int* len) {
  writer_->GetString(str, len);
}


// Serialize roots by writing them into the stream. Serialize pointers
// in HeapObjects by changing them to the encoded address where the
// object will be allocated on deserialization

void Serializer::VisitPointers(Object** start, Object** end) {
  bool root = root_;
  root_ = false;
  for (Object** p = start; p < end; ++p) {
    bool serialized;
    if (root) {
      roots_++;
      Address a = Encode(*p, &serialized);
      // If the object was not just serialized,
      // write its encoded address instead.
      if (!serialized) PutEncodedAddress(a);
    } else {
      // Rewrite the pointer in the HeapObject.
      *p = reinterpret_cast<Object*>(Encode(*p, &serialized));
    }
  }
  root_ = root;
}


void Serializer::VisitExternalReferences(Address* start, Address* end) {
  for (Address* p = start; p < end; ++p) {
    uint32_t code = reference_encoder_->Encode(*p);
    CHECK(*p == NULL ? code == 0 : code != 0);
    *p = reinterpret_cast<Address>(code);
  }
}


void Serializer::VisitRuntimeEntry(RelocInfo* rinfo) {
  Address target = rinfo->target_address();
  uint32_t encoding = reference_encoder_->Encode(target);
  CHECK(target == NULL ? encoding == 0 : encoding != 0);
  uint32_t* pc = reinterpret_cast<uint32_t*>(rinfo->pc());
  *pc = encoding;
}


class GlobalHandlesRetriever: public ObjectVisitor {
 public:
  explicit GlobalHandlesRetriever(List<Object**>* handles)
  : global_handles_(handles) {}

  virtual void VisitPointers(Object** start, Object** end) {
    for (; start != end; ++start) {
      global_handles_->Add(start);
    }
  }

 private:
  List<Object**>* global_handles_;
};


void Serializer::PutFlags() {
  writer_->PutC('F');
  List<char*>* argv = FlagList::argv();
  writer_->PutInt(argv->length());
  writer_->PutC('[');
  for (int i = 0; i < argv->length(); i++) {
    if (i > 0) writer_->PutC('|');
    writer_->PutString((*argv)[i]);
    DeleteArray((*argv)[i]);
  }
  writer_->PutC(']');
  flags_end_ = writer_->length();
  delete argv;
}


void Serializer::PutHeader() {
  PutFlags();
  writer_->PutC('D');
#ifdef DEBUG
  writer_->PutC(FLAG_debug_serialization ? '1' : '0');
#else
  writer_->PutC('0');
#endif
  // Write sizes of paged memory spaces.
  writer_->PutC('S');
  writer_->PutC('[');
  writer_->PutInt(Heap::old_space()->Size());
  writer_->PutC('|');
  writer_->PutInt(Heap::code_space()->Size());
  writer_->PutC('|');
  writer_->PutInt(Heap::map_space()->Size());
  writer_->PutC(']');
  // Write global handles.
  writer_->PutC('G');
  writer_->PutC('[');
  GlobalHandlesRetriever ghr(&global_handles_);
  GlobalHandles::IterateRoots(&ghr);
  for (int i = 0; i < global_handles_.length(); i++) {
    writer_->PutC('N');
  }
  writer_->PutC(']');
}


#ifdef ENABLE_LOGGING_AND_PROFILING
  DECLARE_bool(log_code);
  DECLARE_string(logfile);
#endif


void Serializer::PutLog() {
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (FLAG_log_code) {
    Logger::TearDown();
    int pos = writer_->InsertC('L', flags_end_);
    bool exists;
    Vector<const char> log = ReadFile(FLAG_logfile, &exists);
    writer_->InsertString(log.start(), pos);
    log.Dispose();
  }
#endif
}


static int IndexOf(const List<Object**>& list, Object** element) {
  for (int i = 0; i < list.length(); i++) {
    if (list[i] == element) return i;
  }
  return -1;
}


void Serializer::PutGlobalHandleStack(const List<Handle<Object> >& stack) {
  writer_->PutC('[');
  writer_->PutInt(stack.length());
  for (int i = stack.length() - 1; i >= 0; i--) {
    writer_->PutC('|');
    int gh_index = IndexOf(global_handles_, stack[i].location());
    CHECK_GE(gh_index, 0);
    writer_->PutInt(gh_index);
  }
  writer_->PutC(']');
}


void Serializer::PutContextStack() {
  List<Handle<Object> > contexts(2);
  while (HandleScopeImplementer::instance()->HasSavedContexts()) {
    Handle<Object> context =
      HandleScopeImplementer::instance()->RestoreContext();
    contexts.Add(context);
  }
  PutGlobalHandleStack(contexts);

  List<Handle<Object> > security_contexts(2);
  while (HandleScopeImplementer::instance()->HasSavedSecurityContexts()) {
    Handle<Object> context =
      HandleScopeImplementer::instance()->RestoreSecurityContext();
    security_contexts.Add(context);
  }
  PutGlobalHandleStack(security_contexts);
}


void Serializer::PutEncodedAddress(Address addr) {
  writer_->PutC('P');
  writer_->PutInt(reinterpret_cast<int>(addr));
}


Address Serializer::Encode(Object* o, bool* serialized) {
  *serialized = false;
  if (o->IsSmi()) {
    return reinterpret_cast<Address>(o);
  } else {
    HeapObject* obj = HeapObject::cast(o);
    if (is_marked(obj)) {
      // Already serialized: encoded address is in map.
      intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
      return reinterpret_cast<Address>(clear_mark_bit(map_word));
    } else {
      // First visit: serialize the object.
      *serialized = true;
      return PutObject(obj);
    }
  }
}


Address Serializer::PutObject(HeapObject* obj) {
  Map* map = obj->map();
  InstanceType type = map->instance_type();
  int size = obj->SizeFromMap(map);

  // Simulate the allocation of obj to predict where it will be
  // allocated during deserialization.
  Address addr = Allocate(obj).Encode();

  if (type == CODE_TYPE) {
    Code* code = Code::cast(obj);
    // Ensure Code objects contain Object pointers, not Addresses.
    code->ConvertICTargetsFromAddressToObject();
    LOG(CodeMoveEvent(code->address(), addr));
  }

  // Put the encoded address in the map() of the object, and mark the
  // object. Do this to break recursion before visiting any pointers
  // in the object.
  obj->set_map(reinterpret_cast<Map*>(addr));
  set_mark(obj);

  // Write out the object prologue: type, size, and simulated address of obj.
  writer_->PutC('[');
  CHECK_EQ(0, size & kObjectAlignmentMask);
  writer_->PutInt(type);
  writer_->PutInt(size >> kObjectAlignmentBits);
  PutEncodedAddress(addr);  // encodes AllocationSpace

  // Get the map's encoded address, possibly serializing it on first
  // visit. Note that we do not update obj->map(), since
  // it already contains the forwarding address of 'obj'.
  bool serialized;
  Address map_addr = Encode(map, &serialized);

  // Visit all the pointers in the object other than the map. This
  // will rewrite these pointers in place in the body of the object
  // with their encoded RelativeAddresses, and recursively serialize
  // any as-yet-unvisited objects.
  obj->IterateBody(type, size, this);

  // Mark end of recursively embedded objects, start of object body.
  writer_->PutC('|');
  // Write out the encoded address for the map.
  PutEncodedAddress(map_addr);

  // Write out the raw contents of the object following the map
  // pointer containing the now-updated pointers. No compression, but
  // fast to deserialize.
  writer_->PutBytes(obj->address() + HeapObject::kSize,
                    size - HeapObject::kSize);

#ifdef DEBUG
  if (FLAG_debug_serialization) {
    // Write out the object epilogue to catch synchronization errors.
    PutEncodedAddress(addr);
    writer_->PutC(']');
  }
#endif

  objects_++;
  return addr;
}


RelativeAddress Serializer::Allocate(HeapObject* obj) {
  // Find out which AllocationSpace 'obj' is in.
  AllocationSpace s;
  bool found = false;
  for (int i = 0; !found && i <= LAST_SPACE; i++) {
    s = static_cast<AllocationSpace>(i);
    found = Heap::InSpace(obj, s);
  }
  CHECK(found);
  int size = obj->Size();
  return allocator_[s]->Allocate(size);
}


//------------------------------------------------------------------------------
// Implementation of Deserializer


static const int kInitArraySize = 32;


Deserializer::Deserializer(const char* str, int len)
  : reader_(str, len),
    map_pages_(kInitArraySize), old_pages_(kInitArraySize),
    code_pages_(kInitArraySize), large_objects_(kInitArraySize),
    global_handles_(4) {
  root_ = true;
  roots_ = 0;
  objects_ = 0;
  reference_decoder_ = NULL;
#ifdef DEBUG
  expect_debug_information_ = false;
#endif
}


Deserializer::~Deserializer() {
  if (reference_decoder_) delete reference_decoder_;
}


void Deserializer::ExpectEncodedAddress(Address expected) {
  Address a = GetEncodedAddress();
  USE(a);
  ASSERT(a == expected);
}


#ifdef DEBUG
void Deserializer::Synchronize(const char* tag) {
  if (expect_debug_information_) {
    char buf[kMaxTagLength];
    reader_.ExpectC('S');
    int length = reader_.GetInt();
    ASSERT(length <= kMaxTagLength);
    reader_.GetBytes(reinterpret_cast<Address>(buf), length);
    ASSERT_EQ(strlen(tag), length);
    ASSERT(strncmp(tag, buf, length) == 0);
  }
}
#endif


void Deserializer::Deserialize() {
  // No active threads.
  ASSERT_EQ(NULL, ThreadState::FirstInUse());
  // No active handles.
  ASSERT(HandleScopeImplementer::instance()->Blocks()->is_empty());
  reference_decoder_ = new ExternalReferenceDecoder();
  // By setting linear allocation only, we forbid the use of free list
  // allocation which is not predicted by SimulatedAddress.
  Heap::SetLinearAllocationOnly(true);
  GetHeader();
  Heap::IterateRoots(this);
  GetContextStack();
  Heap::SetLinearAllocationOnly(false);
  Heap::RebuildRSets();
}


void Deserializer::VisitPointers(Object** start, Object** end) {
  bool root = root_;
  root_ = false;
  for (Object** p = start; p < end; ++p) {
    if (root) {
      roots_++;
      // Read the next object or pointer from the stream
      // pointer in the stream.
      int c = reader_.GetC();
      if (c == '[') {
        *p = GetObject();  // embedded object
      } else {
        ASSERT(c == 'P');  // pointer to previously serialized object
        *p = Resolve(reinterpret_cast<Address>(reader_.GetInt()));
      }
    } else {
      // A pointer internal to a HeapObject that we've already
      // read: resolve it to a true address (or Smi)
      *p = Resolve(reinterpret_cast<Address>(*p));
    }
  }
  root_ = root;
}


void Deserializer::VisitExternalReferences(Address* start, Address* end) {
  for (Address* p = start; p < end; ++p) {
    uint32_t code = reinterpret_cast<uint32_t>(*p);
    *p = reference_decoder_->Decode(code);
  }
}


void Deserializer::VisitRuntimeEntry(RelocInfo* rinfo) {
  uint32_t* pc = reinterpret_cast<uint32_t*>(rinfo->pc());
  uint32_t encoding = *pc;
  Address target = reference_decoder_->Decode(encoding);
  rinfo->set_target_address(target);
}


DECLARE_bool(use_ic);
DECLARE_bool(debug_code);
DECLARE_bool(lazy);

void Deserializer::GetFlags() {
  reader_.ExpectC('F');
  int argc = reader_.GetInt() + 1;
  char** argv = NewArray<char*>(argc);
  reader_.ExpectC('[');
  for (int i = 1; i < argc; i++) {
    if (i > 1) reader_.ExpectC('|');
    argv[i] = reader_.GetString();
  }
  reader_.ExpectC(']');
  has_log_ = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp("--log-code", argv[i]) == 0) {
      has_log_ = true;
    } else if (strcmp("--nouse-ic", argv[i]) == 0) {
      FLAG_use_ic = false;
    } else if (strcmp("--debug-code", argv[i]) == 0) {
      FLAG_debug_code = true;
    } else if (strcmp("--nolazy", argv[i]) == 0) {
      FLAG_lazy = false;
    }
    DeleteArray(argv[i]);
  }

  DeleteArray(argv);
}


void Deserializer::GetLog() {
  if (has_log_) {
    reader_.ExpectC('L');
    char* snapshot_log = reader_.GetString();
#ifdef ENABLE_LOGGING_AND_PROFILING
    if (FLAG_log_code) {
      LOG(Preamble(snapshot_log));
    }
#endif
    DeleteArray(snapshot_log);
  }
}


static void InitPagedSpace(PagedSpace* space,
                           int capacity,
                           List<Page*>* page_list) {
  space->EnsureCapacity(capacity);
  // TODO(1240712): PagedSpace::EnsureCapacity can return false due to
  // a failure to allocate from the OS to expand the space.
  PageIterator it(space, PageIterator::ALL_PAGES);
  while (it.has_next()) page_list->Add(it.next());
}


void Deserializer::GetHeader() {
  reader_.ExpectC('D');
#ifdef DEBUG
  expect_debug_information_ = reader_.GetC() == '1';
#else
  // In release mode, don't attempt to read a snapshot containing
  // synchronization tags.
  if (reader_.GetC() != '0') FATAL("Snapshot contains synchronization tags.");
#endif
  // Ensure sufficient capacity in paged memory spaces to avoid growth
  // during deserialization.
  reader_.ExpectC('S');
  reader_.ExpectC('[');
  InitPagedSpace(Heap::old_space(), reader_.GetInt(), &old_pages_);
  reader_.ExpectC('|');
  InitPagedSpace(Heap::code_space(), reader_.GetInt(), &code_pages_);
  reader_.ExpectC('|');
  InitPagedSpace(Heap::map_space(), reader_.GetInt(), &map_pages_);
  reader_.ExpectC(']');
  // Create placeholders for global handles later to be fill during
  // IterateRoots.
  reader_.ExpectC('G');
  reader_.ExpectC('[');
  int c = reader_.GetC();
  while (c != ']') {
    ASSERT(c == 'N');
    global_handles_.Add(GlobalHandles::Create(NULL).location());
    c = reader_.GetC();
  }
}


void Deserializer::GetGlobalHandleStack(List<Handle<Object> >* stack) {
  reader_.ExpectC('[');
  int length = reader_.GetInt();
  for (int i = 0; i < length; i++) {
    reader_.ExpectC('|');
    int gh_index = reader_.GetInt();
    stack->Add(global_handles_[gh_index]);
  }
  reader_.ExpectC(']');
}


void Deserializer::GetContextStack() {
  List<Handle<Object> > entered_contexts(2);
  GetGlobalHandleStack(&entered_contexts);
  for (int i = 0; i < entered_contexts.length(); i++) {
    HandleScopeImplementer::instance()->SaveContext(entered_contexts[i]);
  }
  List<Handle<Object> > security_contexts(2);
  GetGlobalHandleStack(&security_contexts);
  for (int i = 0; i < security_contexts.length(); i++) {
    HandleScopeImplementer::instance()->
      SaveSecurityContext(security_contexts[i]);
  }
}


Address Deserializer::GetEncodedAddress() {
  reader_.ExpectC('P');
  return reinterpret_cast<Address>(reader_.GetInt());
}


Object* Deserializer::GetObject() {
  // Read the prologue: type, size and encoded address.
  InstanceType type = static_cast<InstanceType>(reader_.GetInt());
  int size = reader_.GetInt() << kObjectAlignmentBits;
  Address a = GetEncodedAddress();

  // Get a raw object of the right size in the right space.
  Object* o = Heap::AllocateRaw(size, Space(a));
  ASSERT(!o->IsFailure());
  // Check that the simulation of heap allocation was correct.
  ASSERT(o == Resolve(a));

  // Read any recursively embedded objects.
  int c = reader_.GetC();
  while (c == '[') {
    GetObject();
    c = reader_.GetC();
  }
  ASSERT(c == '|');

  // Read, resolve and set the map pointer: don't rely on map being initialized.
  Address map_addr = GetEncodedAddress();
  Map* map = reinterpret_cast<Map*>(Resolve(map_addr));
  HeapObject* obj = reinterpret_cast<HeapObject*>(o);
  obj->set_map(map);

  // Read the uninterpreted contents of the object after the map
  reader_.GetBytes(obj->address() + HeapObject::kSize,
                    size - HeapObject::kSize);

#ifdef DEBUG
  if (expect_debug_information_) {
    // Read in the epilogue to check that we're still synchronized
    ExpectEncodedAddress(a);
    reader_.ExpectC(']');
  }
#endif

  // Resolve the encoded pointers we just read in
  obj->IterateBody(type, size, this);

  if (type == CODE_TYPE) {
    Code* code = Code::cast(obj);
    // Convert relocations from Object* to Address in Code objects
    code->ConvertICTargetsFromObjectToAddress();
    LOG(CodeMoveEvent(a, code->address()));
  }
  objects_++;
  return o;
}


static inline Object* ResolvePaged(int page_index,
                                   int page_offset,
                                   PagedSpace* space,
                                   List<Page*>* page_list) {
#ifdef DEBUG
  space->CheckLinearAllocationOnly();
#endif

  ASSERT(page_index < page_list->length());
  Address address = (*page_list)[page_index]->OffsetToAddress(page_offset);
  return HeapObject::FromAddress(address);
}


template<typename T>
void ConcatReversed(List<T> * target, const List<T> & source) {
  for (int i = source.length() - 1; i >= 0; i--) {
    target->Add(source[i]);
  }
}


Object* Deserializer::Resolve(Address encoded) {
  Object* o = reinterpret_cast<Object*>(encoded);
  if (o->IsSmi()) return o;

  // Encoded addresses of HeapObjects always have 'HeapObject' tags.
  ASSERT(o->IsHeapObject());

  switch (Space(encoded)) {
    // For Map space and Old space, we cache the known Pages in
    // map_pages and old_pages respectively. Even though MapSpace
    // keeps a list of page addresses, we don't rely on it since
    // GetObject uses AllocateRaw, and that appears not to update
    // the page list.
    case MAP_SPACE:
      return ResolvePaged(PageIndex(encoded), PageOffset(encoded),
                          Heap::map_space(), &map_pages_);
    case OLD_SPACE:
      return ResolvePaged(PageIndex(encoded), PageOffset(encoded),
                          Heap::old_space(), &old_pages_);
    case CODE_SPACE:
      return ResolvePaged(PageIndex(encoded), PageOffset(encoded),
                          Heap::code_space(), &code_pages_);
    case NEW_SPACE:
      return HeapObject::FromAddress(Heap::NewSpaceStart() +
                                     NewSpaceOffset(encoded));
    case LO_SPACE:
      // Cache the known large_objects, allocated one per 'page'
      int index = LargeObjectIndex(encoded);
      if (index >= large_objects_.length()) {
        int new_object_count =
          Heap::lo_space()->PageCount() - large_objects_.length();
        List<Object*> new_objects(new_object_count);
        LargeObjectIterator it(Heap::lo_space());
        for (int i = 0; i < new_object_count; i++) {
          new_objects.Add(it.next());
        }
#ifdef DEBUG
        for (int i = large_objects_.length() - 1; i >= 0; i--) {
          ASSERT(it.next() == large_objects_[i]);
        }
#endif
        ConcatReversed(&large_objects_, new_objects);
        ASSERT(index < large_objects_.length());
      }
      return large_objects_[index];  // s.page_offset() is ignored.
  }
  UNREACHABLE();
  return NULL;
}


} }  // namespace v8::internal
