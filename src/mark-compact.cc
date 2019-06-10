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

#include "execution.h"
#include "global-handles.h"
#include "ic-inl.h"
#include "mark-compact.h"
#include "stub-cache.h"

namespace v8 { namespace internal {

#ifdef DEBUG
// The verification code used between phases of the m-c collector does not
// currently work.
//
// TODO(1240833): Fix the heap verification code and turn this into a real
// flag.
static const bool FLAG_verify_global_gc = false;

DECLARE_bool(gc_verbose);
#endif  // DEBUG

DEFINE_bool(always_compact, false, "Perform compaction on every full GC");
DEFINE_bool(never_compact, false,
            "Never perform compaction on full GC - testing only");

DEFINE_bool(cleanup_ics_at_gc, true,
            "Flush inline caches prior to mark compact collection.");
DEFINE_bool(cleanup_caches_in_maps_at_gc, true,
            "Flush code caches in maps during mark compact cycle.");

DECLARE_bool(gc_global);

// ----------------------------------------------------------------------------
// MarkCompactCollector

bool MarkCompactCollector::compacting_collection_ = false;

#ifdef DEBUG
MarkCompactCollector::CollectorState MarkCompactCollector::state_ = IDLE;

// Counters used for debugging the marking phase of mark-compact or mark-sweep
// collection.
int MarkCompactCollector::live_bytes_ = 0;
int MarkCompactCollector::live_young_objects_ = 0;
int MarkCompactCollector::live_old_objects_ = 0;
int MarkCompactCollector::live_immutable_objects_ = 0;
int MarkCompactCollector::live_map_objects_ = 0;
int MarkCompactCollector::live_lo_objects_ = 0;
#endif

void MarkCompactCollector::CollectGarbage() {
  Prepare();

  MarkLiveObjects();

  SweepLargeObjectSpace();

  if (compacting_collection_) {
    EncodeForwardingAddresses();

    UpdatePointers();

    RelocateObjects();

    RebuildRSets();

  } else {
    SweepSpaces();
  }

  Finish();
}


void MarkCompactCollector::Prepare() {
  static const int kFragmentationLimit = 50;  // Percent.
#ifdef DEBUG
  ASSERT(state_ == IDLE);
  state_ = PREPARE_GC;
#endif
  ASSERT(!FLAG_always_compact || !FLAG_never_compact);

  compacting_collection_ = FLAG_always_compact;

  // We compact the old generation if it gets too fragmented (ie, we could
  // recover an expected amount of space by reclaiming the waste and free
  // list blocks).  We always compact when the flag --gc-global is true
  // because objects do not get promoted out of new space on non-compacting
  // GCs.
  if (!compacting_collection_) {
    int old_gen_recoverable = Heap::old_space()->Waste()
                            + Heap::old_space()->AvailableFree()
                            + Heap::code_space()->Waste()
                            + Heap::code_space()->AvailableFree();
    int old_gen_used = old_gen_recoverable
                     + Heap::old_space()->Size()
                     + Heap::code_space()->Size();
    int old_gen_fragmentation = (old_gen_recoverable * 100) / old_gen_used;
    if (old_gen_fragmentation > kFragmentationLimit) {
      compacting_collection_ = true;
    }
  }

  if (FLAG_never_compact) compacting_collection_ = false;

#ifdef DEBUG
  if (compacting_collection_) {
    // We will write bookkeeping information to the remembered set area
    // starting now.
    Page::set_rset_state(Page::NOT_IN_USE);
  }
#endif

  Heap::map_space()->PrepareForMarkCompact(compacting_collection_);
  Heap::old_space()->PrepareForMarkCompact(compacting_collection_);
  Heap::code_space()->PrepareForMarkCompact(compacting_collection_);

  Counters::global_objects.Set(0);

#ifdef DEBUG
  live_bytes_ = 0;
  live_young_objects_ = 0;
  live_old_objects_ = 0;
  live_immutable_objects_ = 0;
  live_map_objects_ = 0;
  live_lo_objects_ = 0;
#endif
}


void MarkCompactCollector::Finish() {
#ifdef DEBUG
  ASSERT(state_ == SWEEP_SPACES || state_ == REBUILD_RSETS);
  state_ = IDLE;
#endif
  // The stub cache is not traversed during GC; clear the cache to
  // force lazy re-initialization of it. This must be done after the
  // GC, because it relies on the new address of certain old space
  // objects (empty string, illegal builtin).
  StubCache::Clear();
}


// ---------------------------------------------------------------------------
// Forwarding pointers and map pointer encoding
// | 11 bits | offset to the live object in the page
// | 11 bits | offset in a map page
// | 10 bits | map table index

static const int kMapPageIndexBits = 10;
static const int kMapPageOffsetBits = 11;
static const int kForwardingOffsetBits = 11;
static const int kAlignmentBits = 1;

static const int kMapPageIndexShift = 0;
static const int kMapPageOffsetShift =
  kMapPageIndexShift + kMapPageIndexBits;
static const int kForwardingOffsetShift =
  kMapPageOffsetShift + kMapPageOffsetBits;

// 0x000003FF
static const uint32_t kMapPageIndexMask =
  (1 << kMapPageOffsetShift) - 1;

// 0x001FFC00
static const uint32_t kMapPageOffsetMask =
  ((1 << kForwardingOffsetShift) - 1) & ~kMapPageIndexMask;

// 0xFFE00000
static const uint32_t kForwardingOffsetMask =
  ~(kMapPageIndexMask | kMapPageOffsetMask);


static uint32_t EncodePointers(Address map_addr, int offset) {
  // Offset is the distance to the first alive object in the same
  // page. The offset between two objects in the same page should not
  // exceed the object area size of a page.
  ASSERT(0 <= offset && offset < Page::kObjectAreaSize);

  int compact_offset = offset >> kObjectAlignmentBits;
  ASSERT(compact_offset < (1 << kForwardingOffsetBits));

  Page* map_page = Page::FromAddress(map_addr);
  int map_page_index = map_page->mc_page_index;
  ASSERT_MAP_PAGE_INDEX(map_page_index);

  int map_page_offset = map_page->Offset(map_addr) >> kObjectAlignmentBits;

  return (compact_offset << kForwardingOffsetShift)
    | (map_page_offset << kMapPageOffsetShift)
    | (map_page_index << kMapPageIndexShift);
}


static int DecodeOffset(uint32_t encoded) {
  // The offset field is represented in the MSB.
  int offset = (encoded >> kForwardingOffsetShift) << kObjectAlignmentBits;
  ASSERT(0 <= offset && offset < Page::kObjectAreaSize);
  return offset;
}


static Address DecodeMapPointer(uint32_t encoded, MapSpace* map_space) {
  int map_page_index = (encoded & kMapPageIndexMask) >> kMapPageIndexShift;
  ASSERT_MAP_PAGE_INDEX(map_page_index);

  int map_page_offset = ((encoded & kMapPageOffsetMask) >> kMapPageOffsetShift)
                        << kObjectAlignmentBits;

  return (map_space->PageAddress(map_page_index) + map_page_offset);
}


// ----------------------------------------------------------------------------
// Phase 1: tracing and marking live objects.
//   before: all objects are in normal state.
//   after: a live object's map pointer is marked as '00'.

// Marking all live objects in the heap as part of mark-sweep or mark-compact
// collection.  Before marking, all objects are in their normal state.  After
// marking, live objects' map pointers are marked indicating that the object
// has been found reachable.
//
// The marking algorithm is a (mostly) depth-first (because of possible stack
// overflow) traversal of the graph of objects reachable from the roots.  It
// uses an explicit stack of pointers rather than recursion.  The young
// generation's inactive ('from') space is used as a marking stack.  The
// objects in the marking stack are the ones that have been reached and marked
// but their children have not yet been visited.
//
// The marking stack can overflow during traversal.  In that case, we set an
// overflow flag.  When the overflow flag is set, we continue marking objects
// reachable from the objects on the marking stack, but no longer push them on
// the marking stack.  Instead, we mark them as both marked and overflowed.
// When the stack is in the overflowed state, objects marked as overflowed
// have been reached and marked but their children have not been visited yet.
// After emptying the marking stack, we clear the overflow flag and traverse
// the heap looking for objects marked as overflowed, push them on the stack,
// and continue with marking.  This process repeats until all reachable
// objects have been marked.

static MarkingStack marking_stack;

// Helper class for marking pointers in HeapObjects.
class MarkingVisitor : public ObjectVisitor {
 public:

  void VisitPointer(Object** p) {
    MarkObjectByPointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    // Mark all objects pointed to in [start, end).
    const int kMinRangeForMarkingRecursion = 64;
    if (end - start >= kMinRangeForMarkingRecursion) {
      if (VisitUnmarkedObjects(start, end)) return;
      // We are close to a stack overflow, so just mark the objects.
    }
    for (Object** p = start; p < end; p++) MarkObjectByPointer(p);
  }

  void BeginCodeIteration(Code* code) {
    // When iterating over a code object during marking
    // ic targets are derived pointers.
    ASSERT(code->ic_flag() == Code::IC_TARGET_IS_ADDRESS);
  }

  void EndCodeIteration(Code* code) {
    // If this is a compacting collection, set ic targets
    // are pointing to object headers.
    if (IsCompacting()) code->set_ic_flag(Code::IC_TARGET_IS_OBJECT);
  }

  void VisitCodeTarget(RelocInfo* rinfo) {
    ASSERT(is_code_target(rinfo->rmode()));
    Code* code = CodeFromDerivedPointer(rinfo->target_address());
    if (FLAG_cleanup_ics_at_gc && code->is_inline_cache_stub()) {
      IC::Clear(rinfo->pc());
      // Please note targets for cleared inline cached do not have to be
      // marked since they are contained in Heap::non_monomorphic_cache().
    } else {
      MarkCompactCollector::MarkObject(code);
    }
    if (IsCompacting()) {
      // When compacting we convert the target to a real object pointer.
      code = CodeFromDerivedPointer(rinfo->target_address());
      rinfo->set_target_object(code);
    }
  }

  void VisitDebugTarget(RelocInfo* rinfo) {
    ASSERT(is_js_return(rinfo->rmode()) && rinfo->is_call_instruction());
    HeapObject* code = CodeFromDerivedPointer(rinfo->call_address());
    MarkCompactCollector::MarkObject(code);
    // When compacting we convert the call to a real object pointer.
    if (IsCompacting()) rinfo->set_call_object(code);
  }

 private:
  // Mark obj if needed.
  void MarkObject(Object* obj) {
    if (!obj->IsHeapObject()) return;
    MarkCompactCollector::MarkObject(HeapObject::cast(obj));
  }

  // Mark object pointed to by p.
  void MarkObjectByPointer(Object** p) {
    Object* obj = *p;
    if (!obj->IsHeapObject()) return;

    // Optimization: Bypass ConsString object where right size is
    // Heap::empty_string().
    // Please note this checks performed equals:
    //   object->IsConsString() &&
    //   (ConsString::cast(object)->second() == Heap::empty_string())
    // except the map for the object might be marked.
    intptr_t map_word =
        reinterpret_cast<intptr_t>(HeapObject::cast(obj)->map());
    uint32_t tag =
        (reinterpret_cast<Map*>(clear_mark_bit(map_word)))->instance_type();
    if ((tag < FIRST_NONSTRING_TYPE) &&
        (kConsStringTag ==
         static_cast<StringRepresentationTag>(tag &
                                              kStringRepresentationMask)) &&
        (Heap::empty_string() ==
         reinterpret_cast<String*>(
             reinterpret_cast<ConsString*>(obj)->second()))) {
      // Since we don't have the object start it is impossible to update the
      // remeber set quickly.  Therefore this optimization only is taking
      // place when we can avoid changing.
      Object* first = reinterpret_cast<ConsString*>(obj)->first();
      if (Heap::InNewSpace(obj) || !Heap::InNewSpace(first)) {
        obj = first;
        *p = obj;
      }
    }
    MarkCompactCollector::MarkObject(HeapObject::cast(obj));
  }

  // Tells whether the mark sweep collection will perform compaction.
  bool IsCompacting() { return MarkCompactCollector::IsCompacting(); }

  // Retrieves the Code pointer from derived code entry.
  Code* CodeFromDerivedPointer(Address addr) {
    ASSERT(addr != NULL);
    return reinterpret_cast<Code*>(
        HeapObject::FromAddress(addr - Code::kHeaderSize));
  }

  // Visit an unmarked object.
  void VisitUnmarkedObject(HeapObject* obj) {
    ASSERT(Heap::Contains(obj));
#ifdef DEBUG
    MarkCompactCollector::UpdateLiveObjectCount(obj);
#endif
    Map* map = obj->map();
    set_mark(obj);
    // Mark the map pointer and the body.
    MarkCompactCollector::MarkObject(map);
    obj->IterateBody(map->instance_type(), obj->SizeFromMap(map), this);
  }

  // Visit all unmarked objects pointed to by [start, end).
  // Returns false if the operation fails (lack of stack space).
  inline bool VisitUnmarkedObjects(Object** start, Object** end) {
    // Return false is we are close to the stack limit.
    StackLimitCheck check;
    if (check.HasOverflowed()) return false;

    // Visit the unmarked objects.
    for (Object** p = start; p < end; p++) {
      if (!(*p)->IsHeapObject()) continue;
      HeapObject* obj = HeapObject::cast(*p);
      if (is_marked(obj)) continue;
      VisitUnmarkedObject(obj);
    }
    return true;
  }
};


// Helper class for pruning the symbol table.
class SymbolTableCleaner : public ObjectVisitor {
 public:
  SymbolTableCleaner() : pointers_removed_(0) { }
  void VisitPointers(Object** start, Object** end) {
    // Visit all HeapObject pointers in [start, end).
    for (Object** p = start; p < end; p++) {
      if ((*p)->IsHeapObject() && !is_marked(HeapObject::cast(*p))) {
        // Set the entry to null_value (as deleted).
        *p = Heap::null_value();
        pointers_removed_++;
      }
    }
  }

  int PointersRemoved() {
    return pointers_removed_;
  }
 private:
  int pointers_removed_;
};


static void MarkObjectGroups(MarkingVisitor* marker) {
  List<ObjectGroup*>& object_groups = GlobalHandles::ObjectGroups();

  for (int i = 0; i < object_groups.length(); i++) {
    ObjectGroup* entry = object_groups[i];
    bool group_marked = false;
    List<Object**>& objects = entry->objects_;
    for (int j = 0; j < objects.length(); j++) {
      Object* obj = *objects[j];
      if (obj->IsHeapObject() && is_marked(HeapObject::cast(obj))) {
        group_marked = true;
        break;
      }
    }

    if (!group_marked) continue;

    for (int j = 0; j < objects.length(); j++) {
      marker->VisitPointer(objects[j]);
    }
  }
}


void MarkCompactCollector::MarkUnmarkedObject(HeapObject* obj) {
#ifdef DEBUG
  if (!is_marked(obj)) UpdateLiveObjectCount(obj);
#endif
  ASSERT(!is_marked(obj));
  if (obj->IsJSGlobalObject()) Counters::global_objects.Increment();

  if (FLAG_cleanup_caches_in_maps_at_gc && obj->IsMap()) {
    Map::cast(obj)->ClearCodeCache();
  }

  set_mark(obj);
  if (!marking_stack.overflowed()) {
    ASSERT(Heap::Contains(obj));
    marking_stack.Push(obj);
  } else {
    // Set object's stack overflow bit, wait for rescan.
    set_overflow(obj);
  }
}


void MarkCompactCollector::MarkObjectsReachableFromTopFrame() {
  MarkingVisitor marking_visitor;
  do {
    while (!marking_stack.is_empty()) {
      HeapObject* obj = marking_stack.Pop();
      ASSERT(Heap::Contains(obj));
      ASSERT(is_marked(obj) && !is_overflowed(obj));

      // Because the object is marked, the map pointer is not tagged as a
      // normal HeapObject pointer, we need to recover the map pointer,
      // then use the map pointer to mark the object body.
      intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
      Map* map = reinterpret_cast<Map*>(clear_mark_bit(map_word));
      MarkObject(map);
      obj->IterateBody(map->instance_type(), obj->SizeFromMap(map),
                       &marking_visitor);
    };
    // Check objects in object groups.
    MarkObjectGroups(&marking_visitor);
  } while (!marking_stack.is_empty());
}


static int OverflowObjectSize(HeapObject* obj) {
  // Recover the normal map pointer, it might be marked as live and
  // overflowed.
  intptr_t map_word = reinterpret_cast<intptr_t>(obj->map());
  map_word = clear_mark_bit(map_word);
  map_word = clear_overflow_bit(map_word);
  return obj->SizeFromMap(reinterpret_cast<Map*>(map_word));
}


static bool VisitOverflowedObject(HeapObject* obj) {
  if (!is_overflowed(obj)) return true;
  ASSERT(is_marked(obj));

  if (marking_stack.overflowed()) return false;

  clear_overflow(obj);  // clear overflow bit
  ASSERT(Heap::Contains(obj));
  marking_stack.Push(obj);
  return true;
}


template<class T>
static void ScanOverflowedObjects(T* it) {
  while (it->has_next()) {
    HeapObject* obj = it->next();
    if (!VisitOverflowedObject(obj)) {
      ASSERT(marking_stack.overflowed());
      break;
    }
  }
}


bool MarkCompactCollector::MustBeMarked(Object** p) {
  // Check whether *p is a HeapObject pointer.
  if (!(*p)->IsHeapObject()) return false;
  return !is_marked(HeapObject::cast(*p));
}


void MarkCompactCollector::MarkLiveObjects() {
#ifdef DEBUG
  ASSERT(state_ == PREPARE_GC);
  state_ = MARK_LIVE_OBJECTS;
#endif
  // The to space contains live objects, the from space is used as a marking
  // stack.
  marking_stack.Initialize(Heap::new_space()->FromSpaceLow(),
                           Heap::new_space()->FromSpaceHigh());

  ASSERT(!marking_stack.overflowed());

  // Mark the heap roots, including global variables, stack variables, etc.
  MarkingVisitor marking_visitor;

  Heap::IterateStrongRoots(&marking_visitor);

  // Take care of the symbol table specially.
  SymbolTable* symbol_table = SymbolTable::cast(Heap::symbol_table());
#ifdef DEBUG
  UpdateLiveObjectCount(symbol_table);
#endif

  // 1. mark the prefix of the symbol table and push the objects on
  // the stack.
  symbol_table->IteratePrefix(&marking_visitor);
  // 2. mark the symbol table without pushing it on the stack.
  set_mark(symbol_table);  // map word is changed.

  bool has_processed_weak_pointers = false;

  // Mark objects reachable from the roots.
  while (true) {
    MarkObjectsReachableFromTopFrame();

    if (!marking_stack.overflowed()) {
      if (has_processed_weak_pointers) break;
      // First we mark weak pointers not yet reachable.
      GlobalHandles::MarkWeakRoots(&MustBeMarked);
      // Then we process weak pointers and process the transitive closure.
      GlobalHandles::IterateWeakRoots(&marking_visitor);
      has_processed_weak_pointers = true;
      continue;
    }

    // The marking stack overflowed, we need to rebuild it by scanning the
    // whole heap.
    marking_stack.clear_overflowed();

    // We have early stops if the stack overflowed again while scanning
    // overflowed objects in a space.
    SemiSpaceIterator new_it(Heap::new_space(), &OverflowObjectSize);
    ScanOverflowedObjects(&new_it);
    if (marking_stack.overflowed()) continue;

    HeapObjectIterator old_it(Heap::old_space(), &OverflowObjectSize);
    ScanOverflowedObjects(&old_it);
    if (marking_stack.overflowed()) continue;

    HeapObjectIterator code_it(Heap::code_space(), &OverflowObjectSize);
    ScanOverflowedObjects(&code_it);
    if (marking_stack.overflowed()) continue;

    HeapObjectIterator map_it(Heap::map_space(), &OverflowObjectSize);
    ScanOverflowedObjects(&map_it);
    if (marking_stack.overflowed()) continue;

    LargeObjectIterator lo_it(Heap::lo_space(), &OverflowObjectSize);
    ScanOverflowedObjects(&lo_it);
  }

  // Prune the symbol table removing all symbols only pointed to by
  // the symbol table.
  SymbolTableCleaner v;
  symbol_table->IterateElements(&v);
  symbol_table->ElementsRemoved(v.PointersRemoved());

#ifdef DEBUG
  if (FLAG_verify_global_gc) VerifyHeapAfterMarkingPhase();
#endif

  // Remove object groups after marking phase.
  GlobalHandles::RemoveObjectGroups();

  // Objects in the active semispace of the young generation will be relocated
  // to the inactive semispace.  Set the relocation info to the beginning of
  // the inactive semispace.
  Heap::new_space()->MCResetRelocationInfo();
}


#ifdef DEBUG
void MarkCompactCollector::UpdateLiveObjectCount(HeapObject* obj) {
  live_bytes_ += obj->Size();
  if (Heap::new_space()->Contains(obj)) {
    live_young_objects_++;
  } else if (Heap::map_space()->Contains(obj)) {
    ASSERT(obj->IsMap());
    live_map_objects_++;
  } else if (Heap::old_space()->Contains(obj)) {
    live_old_objects_++;
  } else if (Heap::code_space()->Contains(obj)) {
    live_immutable_objects_++;
  } else if (Heap::lo_space()->Contains(obj)) {
    live_lo_objects_++;
  } else {
    UNREACHABLE();
  }
}


static int CountMarkedCallback(HeapObject* obj) {
  if (!is_marked(obj)) return obj->Size();
  clear_mark(obj);
  int obj_size = obj->Size();
  set_mark(obj);
  return obj_size;
}


void MarkCompactCollector::VerifyHeapAfterMarkingPhase() {
  Heap::new_space()->Verify();
  Heap::old_space()->Verify();
  Heap::code_space()->Verify();
  Heap::map_space()->Verify();

  int live_objects;

#define CHECK_LIVE_OBJECTS(it, expected)                   \
          live_objects = 0;                                \
          while (it.has_next()) {                          \
            HeapObject* obj = HeapObject::cast(it.next()); \
            if (is_marked(obj)) live_objects++;            \
          }                                                \
          ASSERT(live_objects == expected);

  SemiSpaceIterator new_it(Heap::new_space(), &CountMarkedCallback);
  CHECK_LIVE_OBJECTS(new_it, live_young_objects_);

  HeapObjectIterator old_it(Heap::old_space(), &CountMarkedCallback);
  CHECK_LIVE_OBJECTS(old_it, live_old_objects_);

  HeapObjectIterator code_it(Heap::code_space(), &CountMarkedCallback);
  CHECK_LIVE_OBJECTS(code_it, live_immutable_objects_);

  HeapObjectIterator map_it(Heap::map_space(), &CountMarkedCallback);
  CHECK_LIVE_OBJECTS(map_it, live_map_objects_);

  LargeObjectIterator lo_it(Heap::lo_space(), &CountMarkedCallback);
  CHECK_LIVE_OBJECTS(lo_it, live_lo_objects_);

#undef CHECK_LIVE_OBJECTS
}
#endif  // DEBUG


void MarkCompactCollector::SweepLargeObjectSpace() {
#ifdef DEBUG
  ASSERT(state_ == MARK_LIVE_OBJECTS);
  state_ =
      compacting_collection_ ? ENCODE_FORWARDING_ADDRESSES : SWEEP_SPACES;
#endif
  // Deallocate unmarked objects and clear marked bits for marked objects.
  Heap::lo_space()->FreeUnmarkedObjects();
}


// -------------------------------------------------------------------------
// Phase 2: Encode forwarding addresses.
// When compacting, forwarding addresses for objects in old space and map
// space are encoded in their map pointer word (along with an encoding of
// their map pointers).
//
//  31             21 20              10 9               0
// +-----------------+------------------+-----------------+
// |forwarding offset|page offset of map|page index of map|
// +-----------------+------------------+-----------------+
//  11 bits           11 bits            10 bits
//
// An address range [start, end) can have both live and non-live objects.
// Maximal non-live regions are marked so they can be skipped on subsequent
// sweeps of the heap.  A distinguished map-pointer encoding is used to mark
// free regions of one-word size (in which case the next word is the start
// of a live object).  A second distinguished map-pointer encoding is used
// to mark free regions larger than one word, and the size of the free
// region (including the first word) is written to the second word of the
// region.
//
// Any valid map page offset must lie in the object area of the page, so map
// page offsets less than Page::kObjectStartOffset are invalid.  We use a
// pair of distinguished invalid map encodings (for single word and multiple
// words) to indicate free regions in the page found during computation of
// forwarding addresses and skipped over in subsequent sweeps.
static const uint32_t kSingleFreeEncoding = 0;
static const uint32_t kMultiFreeEncoding = 1;


// Encode a free region, defined by the given start address and size, in the
// first word or two of the region.
void EncodeFreeRegion(Address free_start, int free_size) {
  ASSERT(free_size >= kIntSize);
  if (free_size == kIntSize) {
    Memory::uint32_at(free_start) = kSingleFreeEncoding;
  } else {
    ASSERT(free_size >= 2 * kIntSize);
    Memory::uint32_at(free_start) = kMultiFreeEncoding;
    Memory::int_at(free_start + kIntSize) = free_size;
  }

#ifdef DEBUG
  // Zap the body of the free region.
  if (FLAG_enable_slow_asserts) {
    for (int offset = 2 * kIntSize;
         offset < free_size;
         offset += kPointerSize) {
      Memory::Address_at(free_start + offset) = kZapValue;
    }
  }
#endif
}


// Try to promote all objects in new space.  Heap numbers and sequential
// strings are promoted to the code space, all others to the old space.
inline Object* MCAllocateFromNewSpace(HeapObject* object, int object_size) {
  bool has_pointers = !object->IsHeapNumber() && !object->IsSeqString();
  Object* forwarded = has_pointers ?
      Heap::old_space()->MCAllocateRaw(object_size) :
      Heap::code_space()->MCAllocateRaw(object_size);

  if (forwarded->IsFailure()) {
    forwarded = Heap::new_space()->MCAllocateRaw(object_size);
  }
  return forwarded;
}


// Allocation functions for the paged spaces call the space's MCAllocateRaw.
inline Object* MCAllocateFromOldSpace(HeapObject* object, int object_size) {
  return Heap::old_space()->MCAllocateRaw(object_size);
}


inline Object* MCAllocateFromCodeSpace(HeapObject* object, int object_size) {
  return Heap::code_space()->MCAllocateRaw(object_size);
}


inline Object* MCAllocateFromMapSpace(HeapObject* object, int object_size) {
  return Heap::map_space()->MCAllocateRaw(object_size);
}


// The forwarding address is encoded at the same offset as the current
// to-space object, but in from space.
inline void EncodeForwardingAddressInNewSpace(HeapObject* old_object,
                                              int object_size,
                                              Object* new_object,
                                              int* ignored) {
  int offset =
      Heap::new_space()->ToSpaceOffsetForAddress(old_object->address());
  Memory::Address_at(Heap::new_space()->FromSpaceLow() + offset) =
      HeapObject::cast(new_object)->address();
}


// The forwarding address is encoded in the map pointer of the object as an
// offset (in terms of live bytes) from the address of the first live object
// in the page.
inline void EncodeForwardingAddressInPagedSpace(HeapObject* old_object,
                                                int object_size,
                                                Object* new_object,
                                                int* offset) {
  // Record the forwarding address of the first live object if necessary.
  if (*offset == 0) {
    Page::FromAddress(old_object->address())->mc_first_forwarded =
        HeapObject::cast(new_object)->address();
  }

  uint32_t encoded = EncodePointers(old_object->map()->address(), *offset);
  old_object->set_map(reinterpret_cast<Map*>(encoded));
  *offset += object_size;
  ASSERT(*offset <= Page::kObjectAreaSize);
}


// Most non-live objects are ignored.
inline void IgnoreNonLiveObject(HeapObject* object) {}


// A code deletion event is logged for non-live code objects.
inline void LogNonLiveCodeObject(HeapObject* object) {
  if (object->IsCode()) LOG(CodeDeleteEvent(object->address()));
}


// Function template that, given a range of addresses (eg, a semispace or a
// paged space page), iterates through the objects in the range to clear
// mark bits and compute and encode forwarding addresses.  As a side effect,
// maximal free chunks are marked so that they can be skipped on subsequent
// sweeps.
//
// The template parameters are an allocation function, a forwarding address
// encoding function, and a function to process non-live objects.
template<MarkCompactCollector::AllocationFunction Alloc,
         MarkCompactCollector::EncodingFunction Encode,
         MarkCompactCollector::ProcessNonLiveFunction ProcessNonLive>
inline void EncodeForwardingAddressesInRange(Address start,
                                             Address end,
                                             int* offset) {
  // The start address of the current free region while sweeping the space.
  // This address is set when a transition from live to non-live objects is
  // encountered.  A value (an encoding of the 'next free region' pointer)
  // is written to memory at this address when a transition from non-live to
  // live objects is encountered.
  Address free_start = NULL;

  // A flag giving the state of the previously swept object.  Initially true
  // to ensure that free_start is initialized to a proper address before
  // trying to write to it.
  bool is_prev_alive = true;

  int object_size;  // Will be set on each iteration of the loop.
  for (Address current = start; current < end; current += object_size) {
    HeapObject* object = HeapObject::FromAddress(current);
    if (is_marked(object)) {
      clear_mark(object);
      object_size = object->Size();

      Object* forwarded = Alloc(object, object_size);
      // Allocation cannot fail, because we are compacting the space.
      ASSERT(!forwarded->IsFailure());
      Encode(object, object_size, forwarded, offset);

#ifdef DEBUG
      if (FLAG_gc_verbose) {
        PrintF("forward %p -> %p.\n", object->address(),
               HeapObject::cast(forwarded)->address());
      }
#endif
      if (!is_prev_alive) {  // Transition from non-live to live.
        EncodeFreeRegion(free_start, current - free_start);
        is_prev_alive = true;
      }
    } else {  // Non-live object.
      object_size = object->Size();
      ProcessNonLive(object);
      if (is_prev_alive) {  // Transition from live to non-live.
        free_start = current;
        is_prev_alive = false;
      }
    }
  }

  // If we ended on a free region, mark it.
  if (!is_prev_alive) EncodeFreeRegion(free_start, end - free_start);
}


// Functions to encode the forwarding pointers in each compactable space.
void MarkCompactCollector::EncodeForwardingAddressesInNewSpace() {
  int ignored;
  EncodeForwardingAddressesInRange<MCAllocateFromNewSpace,
                                   EncodeForwardingAddressInNewSpace,
                                   IgnoreNonLiveObject>(
      Heap::new_space()->bottom(),
      Heap::new_space()->top(),
      &ignored);
}


template<MarkCompactCollector::AllocationFunction Alloc,
         MarkCompactCollector::ProcessNonLiveFunction ProcessNonLive>
void MarkCompactCollector::EncodeForwardingAddressesInPagedSpace(
    PagedSpace* space) {
  PageIterator it(space, PageIterator::PAGES_IN_USE);
  while (it.has_next()) {
    Page* p = it.next();
    // The offset of each live object in the page from the first live object
    // in the page.
    int offset = 0;
    EncodeForwardingAddressesInRange<Alloc,
                                     EncodeForwardingAddressInPagedSpace,
                                     ProcessNonLive>(
        p->ObjectAreaStart(),
        p->AllocationTop(),
        &offset);
  }
}


static void SweepSpace(NewSpace* space) {
  HeapObject* object;
  for (Address current = space->bottom();
       current < space->top();
       current += object->Size()) {
    object = HeapObject::FromAddress(current);
    if (is_marked(object)) {
      clear_mark(object);
    } else {
      // We give non-live objects a map that will correctly give their size,
      // since their existing map might not be live after the collection.
      int size = object->Size();
      if (size >= Array::kHeaderSize) {
        object->set_map(Heap::byte_array_map());
        ByteArray::cast(object)->set_length(ByteArray::LengthFor(size));
      } else {
        ASSERT(size == kPointerSize);
        object->set_map(Heap::one_word_filler_map());
      }
      ASSERT(object->Size() == size);
    }
    // The object is now unmarked for the call to Size() at the top of the
    // loop.
  }
}


static void SweepSpace(PagedSpace* space, DeallocateFunction dealloc) {
  PageIterator it(space, PageIterator::PAGES_IN_USE);
  while (it.has_next()) {
    Page* p = it.next();

    bool is_previous_alive = true;
    Address free_start = NULL;
    HeapObject* object;

    for (Address current = p->ObjectAreaStart();
         current < p->AllocationTop();
         current += object->Size()) {
      object = HeapObject::FromAddress(current);
      if (is_marked(object)) {
        clear_mark(object);
        if (MarkCompactCollector::IsCompacting() && object->IsCode()) {
          // If this is compacting collection marked code objects have had
          // their IC targets converted to objects.
          // They need to be converted back to addresses.
          Code::cast(object)->ConvertICTargetsFromObjectToAddress();
        }
        if (!is_previous_alive) {  // Transition from free to live.
          dealloc(free_start, current - free_start);
          is_previous_alive = true;
        }
      } else {
        if (object->IsCode()) {
          LOG(CodeDeleteEvent(Code::cast(object)->address()));
        }
        if (is_previous_alive) {  // Transition from live to free.
          free_start = current;
          is_previous_alive = false;
        }
      }
      // The object is now unmarked for the call to Size() at the top of the
      // loop.
    }

    // If the last region was not live we need to from free_start to the
    // allocation top in the page.
    if (!is_previous_alive) {
      int free_size = p->AllocationTop() - free_start;
      if (free_size > 0) {
        dealloc(free_start, free_size);
      }
    }
  }
}


void MarkCompactCollector::DeallocateOldBlock(Address start,
                                              int size_in_bytes) {
  Heap::ClearRSetRange(start, size_in_bytes);
  Heap::old_space()->Free(start, size_in_bytes);
}


void MarkCompactCollector::DeallocateCodeBlock(Address start,
                                               int size_in_bytes) {
  Heap::code_space()->Free(start, size_in_bytes);
}


void MarkCompactCollector::DeallocateMapBlock(Address start,
                                              int size_in_bytes) {
  // Objects in map space are frequently assumed to have size Map::kSize and a
  // valid map in their first word.  Thus, we break the free block up into
  // chunks and free them separately.
  ASSERT(size_in_bytes % Map::kSize == 0);
  Heap::ClearRSetRange(start, size_in_bytes);
  Address end = start + size_in_bytes;
  for (Address a = start; a < end; a += Map::kSize) {
    Heap::map_space()->Free(a);
  }
}


void MarkCompactCollector::EncodeForwardingAddresses() {
  ASSERT(state_ == ENCODE_FORWARDING_ADDRESSES);
  // Compute the forwarding pointers in each space.
  EncodeForwardingAddressesInPagedSpace<MCAllocateFromOldSpace,
                                        IgnoreNonLiveObject>(
      Heap::old_space());

  EncodeForwardingAddressesInPagedSpace<MCAllocateFromCodeSpace,
                                        LogNonLiveCodeObject>(
      Heap::code_space());

  // Compute new space next to last after the old and code spaces have been
  // compacted.  Objects in new space can be promoted to old or code space.
  EncodeForwardingAddressesInNewSpace();

  // Compute map space last because computing forwarding addresses
  // overwrites non-live objects.  Objects in the other spaces rely on
  // non-live map pointers to get the sizes of non-live objects.
  EncodeForwardingAddressesInPagedSpace<MCAllocateFromMapSpace,
                                        IgnoreNonLiveObject>(
      Heap::map_space());

  // Write relocation info to the top page, so we can use it later.  This is
  // done after promoting objects from the new space so we get the correct
  // allocation top.
  Heap::old_space()->MCWriteRelocationInfoToPage();
  Heap::code_space()->MCWriteRelocationInfoToPage();
  Heap::map_space()->MCWriteRelocationInfoToPage();
}


void MarkCompactCollector::SweepSpaces() {
  ASSERT(state_ == SWEEP_SPACES);
  ASSERT(!IsCompacting());
  // Noncompacting collections simply sweep the spaces to clear the mark
  // bits and free the nonlive blocks (for old and map spaces).  We sweep
  // the map space last because freeing non-live maps overwrites them and
  // the other spaces rely on possibly non-live maps to get the sizes for
  // non-live objects.
  SweepSpace(Heap::old_space(), &DeallocateOldBlock);
  SweepSpace(Heap::code_space(), &DeallocateCodeBlock);
  SweepSpace(Heap::new_space());
  SweepSpace(Heap::map_space(), &DeallocateMapBlock);
}


// Iterate the live objects in a range of addresses (eg, a page or a
// semispace).  The live regions of the range have been linked into a list.
// The first live region is [first_live_start, first_live_end), and the last
// address in the range is top.  The callback function is used to get the
// size of each live object.
int MarkCompactCollector::IterateLiveObjectsInRange(
    Address start,
    Address end,
    HeapObjectCallback size_func) {
  int live_objects = 0;
  Address current = start;
  while (current < end) {
    uint32_t encoded_map = Memory::uint32_at(current);
    if (encoded_map == kSingleFreeEncoding) {
      current += kPointerSize;
    } else if (encoded_map == kMultiFreeEncoding) {
      current += Memory::int_at(current + kIntSize);
    } else {
      live_objects++;
      current += size_func(HeapObject::FromAddress(current));
    }
  }
  return live_objects;
}


int MarkCompactCollector::IterateLiveObjects(NewSpace* space,
                                             HeapObjectCallback size_f) {
  ASSERT(MARK_LIVE_OBJECTS < state_ && state_ <= RELOCATE_OBJECTS);
  return IterateLiveObjectsInRange(space->bottom(), space->top(), size_f);
}


int MarkCompactCollector::IterateLiveObjects(PagedSpace* space,
                                             HeapObjectCallback size_f) {
  ASSERT(MARK_LIVE_OBJECTS < state_ && state_ <= RELOCATE_OBJECTS);
  int total = 0;
  PageIterator it(space, PageIterator::PAGES_IN_USE);
  while (it.has_next()) {
    Page* p = it.next();
    total += IterateLiveObjectsInRange(p->ObjectAreaStart(),
                                       p->AllocationTop(),
                                       size_f);
  }
  return total;
}


#ifdef DEBUG
static int VerifyMapObject(HeapObject* obj) {
  InstanceType type = reinterpret_cast<Map*>(obj)->instance_type();
  ASSERT(FIRST_TYPE <= type && type <= LAST_TYPE);
  return Map::kSize;
}


void MarkCompactCollector::VerifyHeapAfterEncodingForwardingAddresses() {
  Heap::new_space()->Verify();
  Heap::old_space()->Verify();
  Heap::code_space()->Verify();
  Heap::map_space()->Verify();

  ASSERT(state_ == ENCODE_FORWARDING_ADDRESSES);
  int live_maps = IterateLiveObjects(Heap::map_space(), &VerifyMapObject);
  ASSERT(live_maps == live_map_objects_);

  // Verify page headers in paged spaces.
  VerifyPageHeaders(Heap::old_space());
  VerifyPageHeaders(Heap::code_space());
  VerifyPageHeaders(Heap::map_space());
}


void MarkCompactCollector::VerifyPageHeaders(PagedSpace* space) {
  PageIterator mc_it(space, PageIterator::PAGES_USED_BY_MC);
  while (mc_it.has_next()) {
    Page* p = mc_it.next();
    Address mc_alloc_top = p->mc_relocation_top;
    ASSERT(p->ObjectAreaStart() <= mc_alloc_top &&
           mc_alloc_top <= p->ObjectAreaEnd());
  }

  int page_count = 0;
  PageIterator it(space, PageIterator::PAGES_IN_USE);
  while (it.has_next()) {
    Page* p = it.next();
    ASSERT(p->mc_page_index == page_count);
    page_count++;

    // first_forwarded could be 'deadbeed' if no live objects in this page
    Address first_forwarded = p->mc_first_forwarded;
    ASSERT(first_forwarded == kZapValue ||
           space->Contains(first_forwarded));
  }
}
#endif


// ----------------------------------------------------------------------------
// Phase 3: Update pointers

// Helper class for updating pointers in HeapObjects.
class UpdatingVisitor: public ObjectVisitor {
 public:

  void VisitPointer(Object** p) {
    MarkCompactCollector::UpdatePointer(p);
  }

  void VisitPointers(Object** start, Object** end) {
    // Mark all HeapObject pointers in [start, end)
    for (Object** p = start; p < end; p++) {
      MarkCompactCollector::UpdatePointer(p);
    }
  }
};

void MarkCompactCollector::UpdatePointers() {
#ifdef DEBUG
  ASSERT(state_ == ENCODE_FORWARDING_ADDRESSES);
  state_ = UPDATE_POINTERS;
#endif
  UpdatingVisitor updating_visitor;
  Heap::IterateRoots(&updating_visitor);
  GlobalHandles::IterateWeakRoots(&updating_visitor);

  int live_maps = IterateLiveObjects(Heap::map_space(),
                                     &UpdatePointersInOldObject);
  int live_olds = IterateLiveObjects(Heap::old_space(),
                                     &UpdatePointersInOldObject);
  int live_immutables = IterateLiveObjects(Heap::code_space(),
                                           &UpdatePointersInOldObject);
  int live_news = IterateLiveObjects(Heap::new_space(),
                                     &UpdatePointersInNewObject);

  // Large objects do not move, the map word can be updated directly.
  LargeObjectIterator it(Heap::lo_space());
  while (it.has_next()) UpdatePointersInNewObject(it.next());

  USE(live_maps);
  USE(live_olds);
  USE(live_immutables);
  USE(live_news);

#ifdef DEBUG
  ASSERT(live_maps == live_map_objects_);
  ASSERT(live_olds == live_old_objects_);
  ASSERT(live_immutables == live_immutable_objects_);
  ASSERT(live_news == live_young_objects_);

  if (FLAG_verify_global_gc) VerifyHeapAfterUpdatingPointers();
#endif
}


int MarkCompactCollector::UpdatePointersInNewObject(HeapObject* obj) {
  // Keep old map pointers
  Map* old_map = obj->map();
  ASSERT(old_map->IsHeapObject());

  Address forwarded = GetForwardingAddressInOldSpace(old_map);

  ASSERT(Heap::map_space()->Contains(old_map));
  ASSERT(Heap::map_space()->Contains(forwarded));
#ifdef DEBUG
  if (FLAG_gc_verbose) {
    PrintF("update %p : %p -> %p\n", obj->address(), old_map->address(),
           forwarded);
  }
#endif
  // Update the map pointer.
  obj->set_map(reinterpret_cast<Map*>(HeapObject::FromAddress(forwarded)));

  // We have to compute the object size relying on the old map because
  // map objects are not relocated yet.
  int obj_size = obj->SizeFromMap(old_map);

  // Update pointers in the object body.
  UpdatingVisitor updating_visitor;
  obj->IterateBody(old_map->instance_type(), obj_size, &updating_visitor);
  return obj_size;
}


int MarkCompactCollector::UpdatePointersInOldObject(HeapObject* obj) {
  // Decode the map pointer.
  uint32_t encoded = reinterpret_cast<uint32_t>(obj->map());
  Address map_addr = DecodeMapPointer(encoded, Heap::map_space());
  ASSERT(Heap::map_space()->Contains(HeapObject::FromAddress(map_addr)));

  // At this point, the first word of map_addr is also encoded, cannot
  // cast it to Map* using Map::cast.
  Map* map = reinterpret_cast<Map*>(HeapObject::FromAddress(map_addr));
  int obj_size = obj->SizeFromMap(map);
  InstanceType type = map->instance_type();

  // Update map pointer.
  Address new_map_addr = GetForwardingAddressInOldSpace(map);
  int offset = DecodeOffset(encoded);
  encoded = EncodePointers(new_map_addr, offset);
  obj->set_map(reinterpret_cast<Map*>(encoded));

#ifdef DEBUG
  if (FLAG_gc_verbose) {
    PrintF("update %p : %p -> %p\n", obj->address(),
           map_addr, new_map_addr);
  }
#endif

  // Update pointers in the object body.
  UpdatingVisitor updating_visitor;
  obj->IterateBody(type, obj_size, &updating_visitor);
  return obj_size;
}


Address MarkCompactCollector::GetForwardingAddressInOldSpace(HeapObject* obj) {
  // Object should either in old or map space.
  uint32_t encoded = reinterpret_cast<uint32_t>(obj->map());

  // Offset to the first live object's forwarding address.
  int offset = DecodeOffset(encoded);
  Address obj_addr = obj->address();

  // Find the first live object's forwarding address.
  Page* p = Page::FromAddress(obj_addr);
  Address first_forwarded = p->mc_first_forwarded;

  // Page start address of forwarded address.
  Page* forwarded_page = Page::FromAddress(first_forwarded);
  int forwarded_offset = forwarded_page->Offset(first_forwarded);

  // Find end of allocation of in the page of first_forwarded.
  Address mc_top = forwarded_page->mc_relocation_top;
  int mc_top_offset = forwarded_page->Offset(mc_top);

  // Check if current object's forward pointer is in the same page
  // as the first live object's forwarding pointer
  if (forwarded_offset + offset < mc_top_offset) {
    // In the same page.
    return first_forwarded + offset;
  }

  // Must be in the next page, NOTE: this may cross chunks.
  Page* next_page = forwarded_page->next_page();
  ASSERT(next_page->is_valid());

  offset -= (mc_top_offset - forwarded_offset);
  offset += Page::kObjectStartOffset;

  ASSERT_PAGE_OFFSET(offset);
  ASSERT(next_page->OffsetToAddress(offset) < next_page->mc_relocation_top);

  return next_page->OffsetToAddress(offset);
}

void MarkCompactCollector::UpdatePointer(Object** p) {
  // We need to check if p is in to_space.
  if (!(*p)->IsHeapObject()) return;

  HeapObject* obj = HeapObject::cast(*p);
  Address old_addr = obj->address();
  Address new_addr;

  ASSERT(!Heap::InFromSpace(obj));

  if (Heap::new_space()->Contains(obj)) {
    Address f_addr = Heap::new_space()->FromSpaceLow() +
                     Heap::new_space()->ToSpaceOffsetForAddress(old_addr);
    new_addr = Memory::Address_at(f_addr);

#ifdef DEBUG
    ASSERT(Heap::old_space()->Contains(new_addr) ||
           Heap::code_space()->Contains(new_addr) ||
           Heap::new_space()->FromSpaceContains(new_addr));

    if (Heap::new_space()->FromSpaceContains(new_addr)) {
      ASSERT(Heap::new_space()->FromSpaceOffsetForAddress(new_addr) <=
             Heap::new_space()->ToSpaceOffsetForAddress(old_addr));
    }
#endif

  } else if (Heap::lo_space()->Contains(obj)) {
    // Don't move objects in the large object space.
    new_addr = obj->address();

  } else {
    ASSERT(Heap::old_space()->Contains(obj) ||
           Heap::code_space()->Contains(obj) ||
           Heap::map_space()->Contains(obj));

    new_addr = GetForwardingAddressInOldSpace(obj);
    ASSERT(Heap::old_space()->Contains(new_addr) ||
           Heap::code_space()->Contains(new_addr) ||
           Heap::map_space()->Contains(new_addr));

#ifdef DEBUG
    if (Heap::old_space()->Contains(obj)) {
      ASSERT(Heap::old_space()->MCSpaceOffsetForAddress(new_addr) <=
             Heap::old_space()->MCSpaceOffsetForAddress(old_addr));
    } else if (Heap::code_space()->Contains(obj)) {
      ASSERT(Heap::code_space()->MCSpaceOffsetForAddress(new_addr) <=
             Heap::code_space()->MCSpaceOffsetForAddress(old_addr));
    } else {
      ASSERT(Heap::map_space()->MCSpaceOffsetForAddress(new_addr) <=
             Heap::map_space()->MCSpaceOffsetForAddress(old_addr));
    }
#endif
  }

  *p = HeapObject::FromAddress(new_addr);

#ifdef DEBUG
  if (FLAG_gc_verbose) {
    PrintF("update %p : %p -> %p\n",
           reinterpret_cast<Address>(p), old_addr, new_addr);
  }
#endif
}


#ifdef DEBUG
void MarkCompactCollector::VerifyHeapAfterUpdatingPointers() {
  ASSERT(state_ == UPDATE_POINTERS);

  Heap::new_space()->Verify();
  Heap::old_space()->Verify();
  Heap::code_space()->Verify();
  Heap::map_space()->Verify();

  // We don't have object size info after updating pointers, not much we can
  // do here.
  VerifyPageHeaders(Heap::old_space());
  VerifyPageHeaders(Heap::code_space());
  VerifyPageHeaders(Heap::map_space());
}
#endif


// ----------------------------------------------------------------------------
// Phase 4: Relocate objects

void MarkCompactCollector::RelocateObjects() {
#ifdef DEBUG
  ASSERT(state_ == UPDATE_POINTERS);
  state_ = RELOCATE_OBJECTS;
#endif
  // Relocates objects, always relocate map objects first. Relocating
  // objects in other space relies on map objects to get object size.
  int live_maps = IterateLiveObjects(Heap::map_space(), &RelocateMapObject);
  int live_olds = IterateLiveObjects(Heap::old_space(), &RelocateOldObject);
  int live_immutables =
      IterateLiveObjects(Heap::code_space(), &RelocateCodeObject);
  int live_news = IterateLiveObjects(Heap::new_space(), &RelocateNewObject);

  USE(live_maps);
  USE(live_olds);
  USE(live_immutables);
  USE(live_news);
#ifdef DEBUG
  ASSERT(live_maps == live_map_objects_);
  ASSERT(live_olds == live_old_objects_);
  ASSERT(live_immutables == live_immutable_objects_);
  ASSERT(live_news == live_young_objects_);
#endif

  // Notify code object in LO to convert IC target to address
  // This must happen after lo_space_->Compact
  LargeObjectIterator it(Heap::lo_space());
  while (it.has_next()) { ConvertCodeICTargetToAddress(it.next()); }

  // Flips from and to spaces
  Heap::new_space()->Flip();

  // Sets age_mark to bottom in to space
  Address mark = Heap::new_space()->bottom();
  Heap::new_space()->set_age_mark(mark);

  Heap::new_space()->MCCommitRelocationInfo();
#ifdef DEBUG
  // It is safe to write to the remembered sets as remembered sets on a
  // page-by-page basis after committing the m-c forwarding pointer.
  Page::set_rset_state(Page::IN_USE);
#endif
  Heap::map_space()->MCCommitRelocationInfo();
  Heap::old_space()->MCCommitRelocationInfo();
  Heap::code_space()->MCCommitRelocationInfo();

#ifdef DEBUG
  if (FLAG_verify_global_gc) VerifyHeapAfterRelocatingObjects();
#endif
}


int MarkCompactCollector::ConvertCodeICTargetToAddress(HeapObject* obj) {
  if (obj->IsCode()) {
    Code::cast(obj)->ConvertICTargetsFromObjectToAddress();
  }
  return obj->Size();
}


int MarkCompactCollector::RelocateMapObject(HeapObject* obj) {
  // decode map pointer (forwarded address)
  uint32_t encoded = reinterpret_cast<uint32_t>(obj->map());
  Address map_addr = DecodeMapPointer(encoded, Heap::map_space());
  ASSERT(Heap::map_space()->Contains(HeapObject::FromAddress(map_addr)));

  // Get forwarding address before resetting map pointer
  Address new_addr = GetForwardingAddressInOldSpace(obj);

  // recover map pointer
  obj->set_map(reinterpret_cast<Map*>(HeapObject::FromAddress(map_addr)));

  // The meta map object may not be copied yet.
  Address old_addr = obj->address();

  if (new_addr != old_addr) {
    memmove(new_addr, old_addr, Map::kSize);  // copy contents
  }

#ifdef DEBUG
  if (FLAG_gc_verbose) {
    PrintF("relocate %p -> %p\n", old_addr, new_addr);
  }
#endif

  return Map::kSize;
}


int MarkCompactCollector::RelocateOldObject(HeapObject* obj) {
  // decode map pointer (forwarded address)
  uint32_t encoded = reinterpret_cast<uint32_t>(obj->map());
  Address map_addr = DecodeMapPointer(encoded, Heap::map_space());
  ASSERT(Heap::map_space()->Contains(HeapObject::FromAddress(map_addr)));

  // Get forwarding address before resetting map pointer
  Address new_addr = GetForwardingAddressInOldSpace(obj);

  // recover map pointer
  obj->set_map(reinterpret_cast<Map*>(HeapObject::FromAddress(map_addr)));

  // This is a non-map object, it relies on the assumption that the Map space
  // is compacted before the Old space (see RelocateObjects).
  int obj_size = obj->Size();
  ASSERT_OBJECT_SIZE(obj_size);

  Address old_addr = obj->address();

  ASSERT(Heap::old_space()->MCSpaceOffsetForAddress(new_addr) <=
         Heap::old_space()->MCSpaceOffsetForAddress(old_addr));

  Heap::old_space()->MCAdjustRelocationEnd(new_addr, obj_size);

  if (new_addr != old_addr) {
    memmove(new_addr, old_addr, obj_size);  // copy contents
  }

  HeapObject* copied_to = HeapObject::FromAddress(new_addr);
  if (copied_to->IsCode()) {
    // may also update inline cache target.
    Code::cast(copied_to)->Relocate(new_addr - old_addr);
    // Notify the logger that compile code has moved.
    LOG(CodeMoveEvent(old_addr, new_addr));
  }

#ifdef DEBUG
  if (FLAG_gc_verbose) {
    PrintF("relocate %p -> %p\n", old_addr, new_addr);
  }
#endif

  return obj_size;
}


int MarkCompactCollector::RelocateCodeObject(HeapObject* obj) {
  // decode map pointer (forwarded address)
  uint32_t encoded = reinterpret_cast<uint32_t>(obj->map());
  Address map_addr = DecodeMapPointer(encoded, Heap::map_space());
  ASSERT(Heap::map_space()->Contains(HeapObject::FromAddress(map_addr)));

  // Get forwarding address before resetting map pointer
  Address new_addr = GetForwardingAddressInOldSpace(obj);

  // recover map pointer
  obj->set_map(reinterpret_cast<Map*>(HeapObject::FromAddress(map_addr)));

  // This is a non-map object, it relies on the assumption that the Map space
  // is compacted before the other spaces (see RelocateObjects).
  int obj_size = obj->Size();
  ASSERT_OBJECT_SIZE(obj_size);

  Address old_addr = obj->address();

  ASSERT(Heap::code_space()->MCSpaceOffsetForAddress(new_addr) <=
         Heap::code_space()->MCSpaceOffsetForAddress(old_addr));

  Heap::code_space()->MCAdjustRelocationEnd(new_addr, obj_size);

  // convert inline cache target to address using old address
  if (obj->IsCode()) {
    // convert target to address first related to old_address
    Code::cast(obj)->ConvertICTargetsFromObjectToAddress();
  }

  if (new_addr != old_addr) {
    memmove(new_addr, old_addr, obj_size);  // copy contents
  }

  HeapObject* copied_to = HeapObject::FromAddress(new_addr);
  if (copied_to->IsCode()) {
    // may also update inline cache target.
    Code::cast(copied_to)->Relocate(new_addr - old_addr);
    // Notify the logger that compile code has moved.
    LOG(CodeMoveEvent(old_addr, new_addr));
  }

#ifdef DEBUG
  if (FLAG_gc_verbose) {
    PrintF("relocate %p -> %p\n", old_addr, new_addr);
  }
#endif

  return obj_size;
}


#ifdef DEBUG
class VerifyCopyingVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** p = start; p < end; p++) {
      MarkCompactCollector::VerifyCopyingObjects(p);
    }
  }
};

#endif

int MarkCompactCollector::RelocateNewObject(HeapObject* obj) {
  int obj_size = obj->Size();

  // Get forwarding address
  Address old_addr = obj->address();
  int offset = Heap::new_space()->ToSpaceOffsetForAddress(old_addr);

  Address new_addr =
    Memory::Address_at(Heap::new_space()->FromSpaceLow() + offset);

  if (Heap::new_space()->FromSpaceContains(new_addr)) {
    ASSERT(Heap::new_space()->FromSpaceOffsetForAddress(new_addr) <=
           Heap::new_space()->ToSpaceOffsetForAddress(old_addr));
  } else {
    bool has_pointers = !obj->IsHeapNumber() && !obj->IsSeqString();
    if (has_pointers) {
      Heap::old_space()->MCAdjustRelocationEnd(new_addr, obj_size);
    } else {
      Heap::code_space()->MCAdjustRelocationEnd(new_addr, obj_size);
    }
  }

  // New and old addresses cannot overlap.
  memcpy(reinterpret_cast<void*>(new_addr),
         reinterpret_cast<void*>(old_addr),
         obj_size);

#ifdef DEBUG
  if (FLAG_gc_verbose) {
    PrintF("relocate %p -> %p\n", old_addr, new_addr);
  }
  if (FLAG_verify_global_gc) {
    VerifyCopyingVisitor v;
    HeapObject* copied_to = HeapObject::FromAddress(new_addr);
    copied_to->Iterate(&v);
  }
#endif

  return obj_size;
}


#ifdef DEBUG
void MarkCompactCollector::VerifyHeapAfterRelocatingObjects() {
  ASSERT(state_ == RELOCATE_OBJECTS);

  Heap::new_space()->Verify();
  Heap::old_space()->Verify();
  Heap::code_space()->Verify();
  Heap::map_space()->Verify();

  PageIterator old_it(Heap::old_space(), PageIterator::PAGES_IN_USE);
  while (old_it.has_next()) {
    Page* p = old_it.next();
    ASSERT_PAGE_OFFSET(p->Offset(p->AllocationTop()));
  }

  PageIterator code_it(Heap::code_space(), PageIterator::PAGES_IN_USE);
  while (code_it.has_next()) {
    Page* p = code_it.next();
    ASSERT_PAGE_OFFSET(p->Offset(p->AllocationTop()));
  }

  PageIterator map_it(Heap::map_space(), PageIterator::PAGES_IN_USE);
  while (map_it.has_next()) {
    Page* p = map_it.next();
    ASSERT_PAGE_OFFSET(p->Offset(p->AllocationTop()));
  }
}
#endif


#ifdef DEBUG
void MarkCompactCollector::VerifyCopyingObjects(Object** p) {
  if (!(*p)->IsHeapObject()) return;
  ASSERT(!Heap::InToSpace(*p));
}
#endif  // DEBUG


// -----------------------------------------------------------------------------
// Phase 5: rebuild remembered sets

void MarkCompactCollector::RebuildRSets() {
#ifdef DEBUG
  ASSERT(state_ == RELOCATE_OBJECTS);
  state_ = REBUILD_RSETS;
#endif
  Heap::RebuildRSets();
}

} }  // namespace v8::internal
