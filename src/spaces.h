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

#ifndef V8_SPACES_H_
#define V8_SPACES_H_

#include "list-inl.h"
#include "log.h"

namespace v8 { namespace internal {

// -----------------------------------------------------------------------------
// Heap structures:
//
// A JS heap consists of a young generation, an old generation, and a large
// object space. The young generation is divided into two semispaces. A
// scavenger implements Cheney's copying algorithm. The old generation is
// separated into a map space and an old object space. The map space contains
// all (and only) map objects, the rest of old objects go into the old space.
// The old generation is collected by a mark-sweep-compact collector.
//
// The semispaces of the young generation are contiguous.  The old and map
// spaces consists of a list of pages. A page has a page header, a remembered
// set area, and an object area. A page size is deliberately chosen as 8K
// bytes. The first word of a page is an opaque page header that has the
// address of the next page and its ownership information. The second word may
// have the allocation top address of this page. The next 248 bytes are
// remembered sets. Heap objects are aligned to the pointer size (4 bytes). A
// remembered set bit corresponds to a pointer in the object area.
//
// There is a separate large object space for objects larger than
// Page::kMaxHeapObjectSize, so that they do not have to move during
// collection.  The large object space is paged and uses the same remembered
// set implementation.  Pages in large object space may be larger than 8K.
//
// NOTE: The mark-compact collector rebuilds the remembered set after a
// collection. It reuses first a few words of the remembered set for
// bookkeeping relocation information.


// Some assertion macros used in the debugging mode.

#define ASSERT_PAGE_ALIGNED(address)                  \
  ASSERT((OffsetFrom(address) & Page::kPageAlignmentMask) == 0)

#define ASSERT_OBJECT_ALIGNED(address)                \
  ASSERT((OffsetFrom(address) & kObjectAlignmentMask) == 0)

#define ASSERT_OBJECT_SIZE(size)                      \
  ASSERT((0 < size) && (size <= Page::kMaxHeapObjectSize))

#define ASSERT_PAGE_OFFSET(offset)                    \
  ASSERT((Page::kObjectStartOffset <= offset)         \
      && (offset <= Page::kPageSize))

#define ASSERT_MAP_PAGE_INDEX(index)                            \
  ASSERT((0 <= index) && (index <= MapSpace::kMaxMapPageIndex))


class PagedSpace;
class MemoryAllocator;
struct AllocationInfo;

// -----------------------------------------------------------------------------
// A page normally has 8K bytes. Large object pages may be larger.  A page
// address is always aligned to the 8K page size.  A page is divided into
// three areas: the first two words are used for bookkeeping, the next 248
// bytes are used as remembered set, and the rest of the page is the object
// area.
//
// Pointers are aligned to the pointer size (4 bytes), only 1 bit is needed
// for a pointer in the remembered set. Given an address, its remembered set
// bit position (offset from the start of the page) is calculated by dividing
// its page offset by 32. Therefore, the object area in a page starts at the
// 256th byte (8K/32). Bytes 0 to 255 do not need the remembered set, so that
// the first two words (64 bits) in a page can be used for other purposes.
//
// The mark-compact collector transforms a map pointer into a page index and a
// page offset. The map space can have up to 1024 pages, and 8M bytes (1024 *
// 8K) in total.  Because a map pointer is aligned to the pointer size (4
// bytes), 11 bits are enough to encode the page offset. 21 bits (10 for the
// page index + 11 for the offset in the page) are required to encode a map
// pointer.
//
// The only way to get a page pointer is by calling factory methods:
//   Page* p = Page::FromAddress(addr); or
//   Page* p = Page::FromAllocationTop(top);
class Page {
 public:
  // Returns the page containing a given address. The address ranges
  // from [page_addr .. page_addr + kPageSize[
  //
  // Note that this function only works for addresses in normal paged
  // spaces and addresses in the first 8K of large object pages (ie,
  // the start of large objects but not necessarily derived pointers
  // within them).
  INLINE(static Page* FromAddress(Address a)) {
    return reinterpret_cast<Page*>(OffsetFrom(a) & ~kPageAlignmentMask);
  }

  // Returns the page containing an allocation top. Because an allocation
  // top address can be the upper bound of the page, we need to subtract
  // it with kPointerSize first. The address ranges from
  // [page_addr + kObjectStartOffset .. page_addr + kPageSize].
  INLINE(static Page* FromAllocationTop(Address top)) {
    Page* p = FromAddress(top - kPointerSize);
    ASSERT_PAGE_OFFSET(p->Offset(top));
    return p;
  }

  // Returns the start address of this page.
  Address address() { return reinterpret_cast<Address>(this); }

  // Checks whether this is a valid page address.
  bool is_valid() { return address() != NULL; }

  // Returns the next page of this page.
  inline Page* next_page();

  // Return the end of allocation in this page.
  inline Address AllocationTop();

  // Returns the start address of the object area in this page.
  Address ObjectAreaStart() { return address() + kObjectStartOffset; }

  // Returns the end address (exclusive) of the object area in this page.
  Address ObjectAreaEnd() { return address() + Page::kPageSize; }

  // Returns the start address of the remembered set area.
  Address RSetStart() { return address() + kRSetStartOffset; }

  // Returns the end address of the remembered set area (exclusive).
  Address RSetEnd() { return address() + kRSetEndOffset; }

  // Checks whether an address is page aligned.
  static bool IsAlignedToPageSize(Address a) {
    return 0 == (OffsetFrom(a) & kPageAlignmentMask);
  }

  // True if this page is a large object page.
  bool IsLargeObjectPage() { return (is_normal_page & 0x1) == 0; }

  // Returns the offset of a given address to this page.
  INLINE(int Offset(Address a)) {
    int offset = a - address();
    ASSERT_PAGE_OFFSET(offset);
    return offset;
  }

  // Returns the address for a given offset to the this page.
  Address OffsetToAddress(int offset) {
    ASSERT_PAGE_OFFSET(offset);
    return address() + offset;
  }

  // ---------------------------------------------------------------------
  // Remembered set support

  // Clears remembered set in this page.
  inline void ClearRSet();

  // Return the address of the remembered set word corresponding to an
  // object address/offset pair, and the bit encoded as a single-bit
  // mask in the output parameter 'bitmask'.
  INLINE(static Address ComputeRSetBitPosition(Address address, int offset,
                                               uint32_t* bitmask));

  // Sets the corresponding remembered set bit for a given address.
  INLINE(static void SetRSet(Address address, int offset));

  // Clears the corresponding remembered set bit for a given address.
  static inline void UnsetRSet(Address address, int offset);

  // Checks whether the remembered set bit for a given address is set.
  static inline bool IsRSetSet(Address address, int offset);

#ifdef DEBUG
  // Use a state to mark whether remembered set space can be used for other
  // purposes.
  enum RSetState { IN_USE,  NOT_IN_USE };
  static bool is_rset_in_use() { return rset_state_ == IN_USE; }
  static void set_rset_state(RSetState state) { rset_state_ = state; }
#endif

  // 8K bytes per page.
  static const int kPageSizeBits = 13;

  // Page size in bytes.
  static const int kPageSize = 1 << kPageSizeBits;

  // Page size mask.
  static const int kPageAlignmentMask = (1 << kPageSizeBits) - 1;

  // The end offset of the remembered set in a page
  // (heaps are aligned to pointer size).
  static const int kRSetEndOffset= kPageSize / kBitsPerPointer;

  // The start offset of the remembered set in a page.
  static const int kRSetStartOffset = kRSetEndOffset / kBitsPerPointer;

  // The start offset of the object area in a page.
  static const int kObjectStartOffset = kRSetEndOffset;

  // Object area size in bytes.
  static const int kObjectAreaSize = kPageSize - kObjectStartOffset;

  // Maximum object size that fits in a page.
  static const int kMaxHeapObjectSize = kObjectAreaSize;

  //---------------------------------------------------------------------------
  // Page header description.
  //
  // If a page is not in a large object space, the first word,
  // opaque_header, encodes the next page address (aligned to kPageSize 8K)
  // and the chunk number (0 ~ 8K-1).  Only MemoryAllocator should use
  // opaque_header. The value range of the opaque_header is [0..kPageSize[,
  // or [next_page_start, next_page_end[. It cannot point to a valid address
  // in the current page.  If a page is in the large object space, the first
  // word *may* (if the page start and large object chunk start are the
  // same) contain the address of the next large object chunk.
  int opaque_header;

  // If the page is not in the large object space, the low-order bit of the
  // second word is set. If the page is in the large object space, the
  // second word *may* (if the page start and large object chunk start are
  // the same) contain the large object chunk size.  In either case, the
  // low-order bit for large object pages will be cleared.
  int is_normal_page;

  // The following fields overlap with remembered set, they can only
  // be used in the mark-compact collector when remembered set is not
  // used.

  // The allocation pointer after relocating objects to this page.
  Address mc_relocation_top;

  // The index of the page in its owner space.
  int mc_page_index;

  // The forwarding address of the first live object in this page.
  Address mc_first_forwarded;

#ifdef DEBUG
 private:
  static RSetState rset_state_;  // state of the remembered set
#endif
};


// ----------------------------------------------------------------------------
// A space acquires chunks of memory from the operating system. The memory
// allocator manages chunks for the paged heap spaces (old space and map
// space).  A paged chunk consists of pages. Pages in a chunk have contiguous
// addresses and are linked as a list.
//
// The allocator keeps an initial chunk which is used for the new space.  The
// leftover regions of the initial chunk are used for the initial chunks of
// old space and map space if they are big enough to hold at least one page.
// The allocator assumes that there is one old space and one map space, each
// expands the space by allocating kPagesPerChunk pages except the last
// expansion (before running out of space).  The first chunk may contain fewer
// than kPagesPerChunk pages as well.
//
// The memory allocator also allocates chunks for the large object space, but
// they are managed by the space itself.  The new space does not expand.

class MemoryAllocator : public AllStatic {
 public:
  // Initializes its internal bookkeeping structures.
  // Max capacity of the total space.
  static bool Setup(int max_capacity);

  // Deletes valid chunks.
  static void TearDown();

  // Reserves an initial address range of virtual memory to be split between
  // the two new space semispaces, the old space, and the map space.  The
  // memory is not yet committed or assigned to spaces and split into pages.
  // The initial chunk is unmapped when the memory allocator is torn down.
  // This function should only be called when there is not already a reserved
  // initial chunk (initial_chunk_ should be NULL).  It returns the start
  // address of the initial chunk if successful, with the side effect of
  // setting the initial chunk, or else NULL if unsuccessful and leaves the
  // initial chunk NULL.
  static void* ReserveInitialChunk(const size_t requested);

  // Commits pages from an as-yet-unmanaged block of virtual memory into a
  // paged space.  The block should be part of the initial chunk reserved via
  // a call to ReserveInitialChunk.  The number of pages is always returned in
  // the output parameter num_pages.  This function assumes that the start
  // address is non-null and that it is big enough to hold at least one
  // page-aligned page.  The call always succeeds, and num_pages is always
  // greater than zero.
  static Page* CommitPages(Address start, size_t size, PagedSpace* owner,
                           int* num_pages);

  // Commit a contiguous block of memory from the initial chunk.  Assumes that
  // the address is not NULL, the size is greater than zero, and that the
  // block is contained in the initial chunk.  Returns true if it succeeded
  // and false otherwise.
  static bool CommitBlock(Address start, size_t size);

  // Attempts to allocate the requested (non-zero) number of pages from the
  // OS.  Fewer pages might be allocated than requested. If it fails to
  // allocate memory for the OS or cannot allocate a single page, this
  // function returns an invalid page pointer (NULL). The caller must check
  // whether the returned page is valid (by calling Page::is_valid()).  It is
  // guaranteed that allocated pages have contiguous addresses.  The actual
  // number of allocated page is returned in the output parameter
  // allocated_pages.
  static Page* AllocatePages(int requested_pages, int* allocated_pages,
                             PagedSpace* owner);

  // Frees pages from a given page and after. If 'p' is the first page
  // of a chunk, pages from 'p' are freed and this function returns an
  // invalid page pointer. Otherwise, the function searches a page
  // after 'p' that is the first page of a chunk. Pages after the
  // found page are freed and the function returns 'p'.
  static Page* FreePages(Page* p);

  // Allocates and frees raw memory of certain size.
  // These are just thin wrappers around OS::Allocate and OS::Free,
  // but keep track of allocated bytes as part of heap.
  static void* AllocateRawMemory(const size_t requested, size_t* allocated);
  static void FreeRawMemory(void* buf, size_t length);

  // Returns the maximum available bytes of heaps.
  static int Available() { return capacity_ < size_ ? 0 : capacity_ - size_; }

  // Links two pages.
  static inline void SetNextPage(Page* prev, Page* next);

  // Returns the next page of a given page.
  static inline Page* GetNextPage(Page* p);

  // Checks whether a page belongs to a space.
  static inline bool IsPageInSpace(Page* p, PagedSpace* space);

  // Returns the space that owns the given page.
  static inline PagedSpace* PageOwner(Page* page);

  // Finds the first/last page in the same chunk as a given page.
  static Page* FindFirstPageInSameChunk(Page* p);
  static Page* FindLastPageInSameChunk(Page* p);

#ifdef DEBUG
  // Reports statistic info of the space.
  static void ReportStatistics();
#endif

  // Due to encoding limitation, we can only have 8K chunks.
  static const int kMaxNofChunks = 1 << Page::kPageSizeBits;
  // If a chunk has at least 32 pages, the maximum heap size is about
  // 8 * 1024 * 32 * 8K = 2G bytes.
  static const int kPagesPerChunk = 64;
  static const int kChunkSize = kPagesPerChunk * Page::kPageSize;

 private:
  // Maximum space size in bytes.
  static int capacity_;

  // Allocated space size in bytes.
  static int size_;

  // The initial chunk of virtual memory.
  static VirtualMemory* initial_chunk_;

  // Allocated chunk info: chunk start address, chunk size, and owning space.
  class ChunkInfo BASE_EMBEDDED {
   public:
    ChunkInfo() : address_(NULL), size_(0), owner_(NULL) {}
    void init(Address a, size_t s, PagedSpace* o) {
      address_ = a;
      size_ = s;
      owner_ = o;
    }
    Address address() { return address_; }
    size_t size() { return size_; }
    PagedSpace* owner() { return owner_; }

   private:
    Address address_;
    size_t size_;
    PagedSpace* owner_;
  };

  // Chunks_, free_chunk_ids_ and top_ act as a stack of free chunk ids.
  static List<ChunkInfo> chunks_;
  static List<int> free_chunk_ids_;
  static int max_nof_chunks_;
  static int top_;

  // Push/pop a free chunk id onto/from the stack.
  static void Push(int free_chunk_id);
  static int Pop();
  static bool OutOfChunkIds() { return top_ == 0; }

  // Frees a chunk.
  static void DeleteChunk(int chunk_id);

  // Basic check whether a chunk id is in the valid range.
  static inline bool IsValidChunkId(int chunk_id);

  // Checks whether a chunk id identifies an allocated chunk.
  static inline bool IsValidChunk(int chunk_id);

  // Returns the chunk id that a page belongs to.
  static inline int GetChunkId(Page* p);

  // Initializes pages in a chunk. Returns the first page address.
  // This function and GetChunkId() are provided for the mark-compact
  // collector to rebuild page headers in the from space, which is
  // used as a marking stack and its page headers are destroyed.
  static Page* InitializePagesInChunk(int chunk_id, int pages_in_chunk,
                                      PagedSpace* owner);
};


// -----------------------------------------------------------------------------
// Interface for heap object iterator to be implemented by all object space
// object iterators.
//
// NOTE: The space specific object iterators also implements the own has_next()
//       and next() methods which are used to avoid using virtual functions
//       iterating a specific space.

class ObjectIterator : public Malloced {
 public:
  virtual ~ObjectIterator() { }

  virtual bool has_next_object() = 0;
  virtual HeapObject* next_object() = 0;
};


// -----------------------------------------------------------------------------
// Space iterator for iterating over all spaces.
//
// For each space an object iterator is provided. The deallocation of the
// returned object iterators is handled by the space iterator.

class SpaceIterator : public Malloced {
 public:
  SpaceIterator();
  virtual ~SpaceIterator();

  bool has_next();
  ObjectIterator* next();

 private:
  ObjectIterator* CreateIterator();

  int current_space_;  // from enum AllocationSpace.
  ObjectIterator* iterator_;  // object iterator for the current space.
};


// -----------------------------------------------------------------------------
// Heap object iterator in new/old/map spaces.
//
// A HeapObjectIterator iterates objects from a given address to the
// top of a space. The given address must be below the current
// allocation pointer (space top). If the space top changes during
// iteration (because of allocating new objects), the iterator does
// not iterate new objects. The caller function must create a new
// iterator starting from the old top in order to visit these new
// objects. Heap::Scavenage() is such an example.

class HeapObjectIterator: public ObjectIterator {
 public:
  // Creates a new object iterator in a given space. If a start
  // address is not given, the iterator starts from the space bottom.
  // If the size function is not given, the iterator calls the default
  // Object::Size().
  explicit HeapObjectIterator(PagedSpace* space);
  HeapObjectIterator(PagedSpace* space, HeapObjectCallback size_func);
  HeapObjectIterator(PagedSpace* space, Address start);
  HeapObjectIterator(PagedSpace* space,
                     Address start,
                     HeapObjectCallback size_func);

  inline bool has_next();
  inline HeapObject* next();

  // implementation of ObjectIterator.
  virtual bool has_next_object() { return has_next(); }
  virtual HeapObject* next_object() { return next(); }

 private:
  Address cur_addr_;  // current iteration point
  Address end_addr_;  // end iteration point
  Address cur_limit_;  // current page limit
  HeapObjectCallback size_func_;  // size function
  Page* end_page_;  // caches the page of the end address

  // Slow path of has_next, checks whether there are more objects in
  // the next page.
  bool HasNextInNextPage();

  // Initializes fields.
  void Initialize(Address start, Address end, HeapObjectCallback size_func);

#ifdef DEBUG
  // Verifies whether fields have valid values.
  void Verify();
#endif
};


// -----------------------------------------------------------------------------
// A PageIterator iterates pages in a space.
//
// The PageIterator class provides three modes for iterating pages in a space:
//   PAGES_IN_USE iterates pages that are in use by the allocator;
//   PAGES_USED_BY_GC iterates pages that hold relocated objects during a
//                    mark-compact collection;
//   ALL_PAGES iterates all pages in the space.

class PageIterator BASE_EMBEDDED {
 public:
  enum Mode {PAGES_IN_USE, PAGES_USED_BY_MC, ALL_PAGES};

  PageIterator(PagedSpace* space, Mode mode);

  inline bool has_next();
  inline Page* next();

 private:
  Page* cur_page_;  // next page to return
  Page* stop_page_;  // page where to stop
};


// -----------------------------------------------------------------------------
// A space has a list of pages. The next page can be accessed via
// Page::next_page() call. The next page of the last page is an
// invalid page pointer. A space can expand and shrink dynamically.

// An abstraction of allocation and relocation pointers in a page-structured
// space.
struct AllocationInfo {
  Address top;  // current allocation top
  Address limit;  // current allocation limit
};


// An abstraction of the accounting statistics of a page-structured space.
// The 'capacity' of a space is the number of object-area bytes (ie, not
// including page bookkeeping structures) currently in the space. The 'size'
// of a space is the number of allocated bytes, the 'waste' in the space is
// the number of bytes that are not allocated and not available to
// allocation without reorganizing the space via a GC (eg, small blocks due
// to internal fragmentation, top of page areas in map space), and the bytes
// 'available' is the number of unallocated bytes that are not waste.  The
// capacity is the sum of size, waste, and available.
//
// The stats are only set by functions that ensure they stay balanced. These
// functions increase or decrease one of the non-capacity stats in
// conjunction with capacity, or else they always balance increases and
// decreases to the non-capacity stats.
class AllocationStats BASE_EMBEDDED {
 public:
  AllocationStats() { Clear(); }

  // Zero out all the allocation statistics (ie, no capacity).
  void Clear() {
    capacity_ = 0;
    available_ = 0;
    size_ = 0;
    waste_ = 0;
  }

  // Reset the allocation statistics (ie, available = capacity with no
  // wasted or allocated bytes).
  void Reset() {
    available_ = capacity_;
    size_ = 0;
    waste_ = 0;
  }

  // Accessors for the allocation statistics.
  int Capacity() { return capacity_; }
  int Available() { return available_; }
  int Size() { return size_; }
  int Waste() { return waste_; }

  // Grow the space by adding available bytes.
  void ExpandSpace(int size_in_bytes) {
    capacity_ += size_in_bytes;
    available_ += size_in_bytes;
  }

  // Shrink the space by removing available bytes.
  void ShrinkSpace(int size_in_bytes) {
    capacity_ -= size_in_bytes;
    available_ -= size_in_bytes;
  }

  // Allocate from available bytes (available -> size).
  void AllocateBytes(int size_in_bytes) {
    available_ -= size_in_bytes;
    size_ += size_in_bytes;
  }

  // Free allocated bytes, making them available (size -> available).
  void DeallocateBytes(int size_in_bytes) {
    size_ -= size_in_bytes;
    available_ += size_in_bytes;
  }

  // Waste free bytes (available -> waste).
  void WasteBytes(int size_in_bytes) {
    available_ -= size_in_bytes;
    waste_ += size_in_bytes;
  }

  // Consider the wasted bytes to be allocated, as they contain filler
  // objects (waste -> size).
  void FillWastedBytes(int size_in_bytes) {
    waste_ -= size_in_bytes;
    size_ += size_in_bytes;
  }

 private:
  int capacity_;
  int available_;
  int size_;
  int waste_;
};


class PagedSpace : public Malloced {
  friend class PageIterator;
 public:
  // Creates a space with a maximum capacity, and an id.
  PagedSpace(int max_capacity, AllocationSpace id);

  // Set up the space using the given address range of virtual memory (from
  // the memory allocator's initial chunk) if possible.  If the block of
  // addresses is not big enough to contain a single page-aligned page, a
  // fresh chunk will be allocated.
  bool Setup(Address start, size_t size);

  // Returns true if the space has been successfully set up and not
  // subsequently torn down.
  bool HasBeenSetup();

  // Cleans up the space, frees all pages in this space except those belonging
  // to the initial chunk, uncommits addresses in the initial chunk.
  void TearDown();

  // Checks whether an object/address is in this space.
  inline bool Contains(Address a);
  bool Contains(HeapObject* o) { return Contains(o->address()); }

  // Finds an object that the given address falls in its body. Returns
  // Failure::Exception() if the operation failed.  The implementation
  // iterates objects in the page containing the address, the cost is
  // linear to the number of objects in the page. It may be slow.
  Object* FindObject(Address addr);

  // Clears remembered sets of pages in this space.
  void ClearRSet();

  // Current capacity without growing (Size() + Available() + Waste()).
  int Capacity() { return accounting_stats_.Capacity(); }

  // Available bytes without growing.
  int Available() { return accounting_stats_.Available(); }

  // Allocated bytes in this space.
  int Size() { return accounting_stats_.Size(); }

  // Wasted bytes due to fragmentation and not recoverable until the
  // next GC of this space.
  int Waste() { return accounting_stats_.Waste(); }

  // Returns the address of the first object in this space.
  Address bottom() { return first_page_->ObjectAreaStart(); }

  // Returns the allocation pointer in this space.
  Address top() { return allocation_info_.top; }

  AllocationSpace identity() { return identity_; }

  // If 'linear_only' is true, force allocation_mode_ to
  // LINEAR_ONLY. If 'linear_only' is false, allocation_mode_ is
  // checked to be LINEAR_ONLY and changed to LINEAR, allowing it to
  // alternate between LINEAR and FREE_LIST automatically.
  void SetLinearAllocationOnly(bool linear_only);

  // ---------------------------------------------------------------------------
  // Mark-compact collection support functions

  // Set the relocation point to the beginning of the space.
  void MCResetRelocationInfo();

  // Writes relocation info to the top page.
  void MCWriteRelocationInfoToPage() {
    TopPageOf(mc_forwarding_info_)->mc_relocation_top = mc_forwarding_info_.top;
  }

  // Computes the offset of a given address in this space to the beginning
  // of the space.
  int MCSpaceOffsetForAddress(Address addr);

  // Releases half of unused pages.
  void Shrink();

  // Ensures that the capacity is at least 'capacity'. Returns false on failure.
  bool EnsureCapacity(int capacity);

#ifdef DEBUG
  void CheckLinearAllocationOnly() { CHECK(allocation_mode_ == LINEAR_ONLY); }

  // Print meta info and objects in this space.
  void Print();

  // Report code object related statistics
  void CollectCodeStatistics();
  static void ReportCodeStatistics();
  static void ResetCodeStatistics();
#endif

 protected:
  // In LINEAR and LINEAR_ONLY mode, allocation is from the end of the last
  // page.  In FREE_LIST mode, allocation is from a fragment list of free
  // space at the end of recent pages. LINEAR and FREE_LIST mode alternate
  // automatically.  LINEAR_ONLY mode is sticky until converted to LINEAR by
  // an API call.
  enum AllocationMode { LINEAR_ONLY, LINEAR, FREE_LIST };

  // Maximum capacity of this space.
  int max_capacity_;

  // Accounting information for this space.
  AllocationStats accounting_stats_;

  // The first page in this space.
  Page* first_page_;

  // The allocation mode.
  AllocationMode allocation_mode_;

  // Normal allocation information.
  AllocationInfo allocation_info_;

  // Relocation information during mark-compact collections.
  AllocationInfo mc_forwarding_info_;

  // Sets allocation pointer to a page bottom.
  static void SetAllocationInfo(AllocationInfo* alloc_info, Page* p);

  // Returns the top page specified by an allocation info structure.
  static Page* TopPageOf(AllocationInfo alloc_info) {
    return Page::FromAllocationTop(alloc_info.limit);
  }

  // Expands the space by allocating a fixed number of pages. Returns false if
  // it cannot allocate requested number of pages from OS. Newly allocated
  // pages are appened to the last_page;
  bool Expand(Page* last_page);

#ifdef DEBUG
  void DoPrintRSet(const char* space_name);
#endif
 private:
  // Identity of this space.
  AllocationSpace identity_;

  // Returns the page of the allocation pointer.
  Page* AllocationTopPage() { return TopPageOf(allocation_info_); }

  // Returns a pointer to the page of the relocation pointer.
  Page* MCRelocationTopPage() { return TopPageOf(mc_forwarding_info_); }

#ifdef DEBUG
  // Returns the number of total pages in this space.
  int CountTotalPages();
#endif
};


#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
// HistogramInfo class for recording a single "bar" of a histogram.  This
// class is used for collecting statistics to print to stdout (when compiled
// with DEBUG) or to the log file (when compiled with
// ENABLE_LOGGING_AND_PROFILING).
class HistogramInfo BASE_EMBEDDED {
 public:
  HistogramInfo() : number_(0), bytes_(0) {}

  const char* name() { return name_; }
  void set_name(const char* name) { name_ = name; }

  int number() { return number_; }
  void increment_number(int num) { number_ += num; }

  int bytes() { return bytes_; }
  void increment_bytes(int size) { bytes_ += size; }

  // Clear the number of objects and size fields, but not the name.
  void clear() {
    number_ = 0;
    bytes_ = 0;
  }

 private:
  const char* name_;
  int number_;
  int bytes_;
};
#endif


// -----------------------------------------------------------------------------
// SemiSpace in young generation
//
// A semispace is a contiguous chunk of memory. The mark-compact collector
// uses the memory in the from space as a marking stack when tracing live
// objects.

class SemiSpace  BASE_EMBEDDED {
 public:
  // Creates a space in the young generation. The constructor does not
  // allocate memory from the OS.  A SemiSpace is given a contiguous chunk of
  // memory of size 'capacity' when set up, and does not grow or shrink
  // otherwise.  In the mark-compact collector, the memory region of the from
  // space is used as the marking stack. It requires contiguous memory
  // addresses.
  SemiSpace(int initial_capacity, int maximum_capacity);

  // Sets up the semispace using the given chunk.
  bool Setup(Address start, int size);

  // Tear down the space.  Heap memory was not allocated by the space, so it
  // is not deallocated here.
  void TearDown();

  // True if the space has been set up but not torn down.
  bool HasBeenSetup() { return start_ != NULL; }

  // Double the size of the semispace by committing extra virtual memory.
  // Assumes that the caller has checked that the semispace has not reached
  // its maxmimum capacity (and thus there is space available in the reserved
  // address range to grow).
  bool Double();

  // Returns the start address of the space.
  Address low() { return start_; }
  // Returns one past the end address of the space.
  Address high() { return low() + capacity_; }

  // Age mark accessors.
  Address age_mark() { return age_mark_; }
  void set_age_mark(Address mark) { age_mark_ = mark; }

  // True if the address is in the address range of this semispace (not
  // necessarily below the allocation pointer).
  bool Contains(Address a) {
    return (reinterpret_cast<uint32_t>(a) & address_mask_)
           == reinterpret_cast<uint32_t>(start_);
  }

  // True if the object is a heap object in the address range of this
  // semispace (not necessarily below the allocation pointer).
  bool Contains(Object* o) {
    return (reinterpret_cast<uint32_t>(o) & object_mask_) == object_expected_;
  }

  // The offset of an address from the begining of the space.
  int SpaceOffsetForAddress(Address addr) { return addr - low(); }

#ifdef DEBUG
  void Print();
#endif

 private:
  // The current and maximum capacity of the space.
  int capacity_;
  int maximum_capacity_;

  // The start address of the space.
  Address start_;
  // Used to govern object promotion during mark-compact collection.
  Address age_mark_;

  // Masks and comparison values to test for containment in this semispace.
  uint32_t address_mask_;
  uint32_t object_mask_;
  uint32_t object_expected_;

 public:
  TRACK_MEMORY("SemiSpace")
};


// A SemiSpaceIterator is an ObjectIterator that iterates over the active
// semispace of the heap's new space.  It iterates over the objects in the
// semispace from a given start address (defaulting to the bottom of the
// semispace) to the top of the semispace.  New objects allocated after the
// iterator is created are not iterated.
class SemiSpaceIterator : public ObjectIterator {
 public:
  // Create an iterator over the objects in the given space.  If no start
  // address is given, the iterator starts from the bottom of the space.  If
  // no size function is given, the iterator calls Object::Size().
  explicit SemiSpaceIterator(NewSpace* space);
  SemiSpaceIterator(NewSpace* space, HeapObjectCallback size_func);
  SemiSpaceIterator(NewSpace* space, Address start);

  bool has_next() {return current_ < limit_; }

  HeapObject* next() {
    ASSERT(has_next());

    HeapObject* object = HeapObject::FromAddress(current_);
    int size = (size_func_ == NULL) ? object->Size() : size_func_(object);
    ASSERT_OBJECT_SIZE(size);

    current_ += size;
    return object;
  }

  // Implementation of the ObjectIterator functions.
  virtual bool has_next_object() { return has_next(); }
  virtual HeapObject* next_object() { return next(); }

 private:
  void Initialize(NewSpace* space, Address start, Address end,
                  HeapObjectCallback size_func);

  // The semispace.
  SemiSpace* space_;
  // The current iteration point.
  Address current_;
  // The end of iteration.
  Address limit_;
  // The callback function.
  HeapObjectCallback size_func_;
};


// -----------------------------------------------------------------------------
// The young generation space.
//
// The new space consists of a contiguous pair of semispaces.  It simply
// forwards most functions to the appropriate semispace.

class NewSpace : public Malloced {
 public:
  // Create a new space with a given allocation capacity (ie, the capacity of
  // *one* of the semispaces).  The constructor does not allocate heap memory
  // from the OS.  When the space is set up, it is given a contiguous chunk of
  // memory of size 2 * semispace_capacity.  To support fast containment
  // testing in the new space, the size of this chunk must be a power of two
  // and it must be aligned to its size.
  NewSpace(int initial_semispace_capacity, int maximum_semispace_capacity);

  // Sets up the new space using the given chunk.
  bool Setup(Address start, int size);

  // Tears down the space.  Heap memory was not allocated by the space, so it
  // is not deallocated here.
  void TearDown();

  // True if the space has been set up but not torn down.
  bool HasBeenSetup() {
    return to_space_->HasBeenSetup() && from_space_->HasBeenSetup();
  }

  // Flip the pair of spaces.
  void Flip();

  // Doubles the capacity of the semispaces.  Assumes that they are not at
  // their maximum capacity.  Returns a flag indicating success or failure.
  bool Double();

  // True if the address or object lies in the address range of either
  // semispace (not necessarily below the allocation pointer).
  bool Contains(Address a) {
    return (reinterpret_cast<uint32_t>(a) & address_mask_)
        == reinterpret_cast<uint32_t>(start_);
  }
  bool Contains(Object* o) {
    return (reinterpret_cast<uint32_t>(o) & object_mask_) == object_expected_;
  }

  // Return the allocated bytes in the active semispace.
  int Size() { return top() - bottom(); }
  // Return the current capacity of a semispace.
  int Capacity() { return capacity_; }
  // Return the available bytes without growing in the active semispace.
  int Available() { return Capacity() - Size(); }

  // Return the maximum capacity of a semispace.
  int MaximumCapacity() { return maximum_capacity_; }

  // Return the address of the allocation pointer in the active semispace.
  Address top() { return allocation_info_.top; }
  // Return the address of the first object in the active semispace.
  Address bottom() { return to_space_->low(); }

  // Get the age mark of the inactive semispace.
  Address age_mark() { return from_space_->age_mark(); }
  // Set the age mark in the active semispace.
  void set_age_mark(Address mark) { to_space_->set_age_mark(mark); }

  // The start address of the space and a bit mask. Anding an address in the
  // new space with the mask will result in the start address.
  Address start() { return start_; }
  uint32_t mask() { return address_mask_; }

  // The allocation top and limit addresses.
  Address* allocation_top_address() { return &allocation_info_.top; }
  Address* allocation_limit_address() { return &allocation_info_.limit; }

  // Allocate the requested number of bytes in the space if possible, return a
  // failure object if not.
  Object* AllocateRaw(int size_in_bytes) {
    return AllocateRawInternal(size_in_bytes, &allocation_info_);
  }

  // Allocate the requested number of bytes for relocation during mark-compact
  // collection.
  Object* MCAllocateRaw(int size_in_bytes) {
    return AllocateRawInternal(size_in_bytes, &mc_forwarding_info_);
  }

  // Reset the allocation pointer to the beginning of the active semispace.
  void ResetAllocationInfo();
  // Reset the reloction pointer to the bottom of the inactive semispace in
  // preparation for mark-compact collection.
  void MCResetRelocationInfo();
  // Update the allocation pointer in the active semispace after a
  // mark-compact collection.
  void MCCommitRelocationInfo();

  // Get the extent of the inactive semispace (for use as a marking stack).
  Address FromSpaceLow() { return from_space_->low(); }
  Address FromSpaceHigh() { return from_space_->high(); }

  // Get the extent of the active semispace (to sweep newly copied objects
  // during a scavenge collection).
  Address ToSpaceLow() { return to_space_->low(); }
  Address ToSpaceHigh() { return to_space_->high(); }

  // Offsets from the beginning of the semispaces.
  int ToSpaceOffsetForAddress(Address a) {
    return to_space_->SpaceOffsetForAddress(a);
  }
  int FromSpaceOffsetForAddress(Address a) {
    return from_space_->SpaceOffsetForAddress(a);
  }

  // True if the object is a heap object in the address range of the
  // respective semispace (not necessarily below the allocation pointer of the
  // semispace).
  bool ToSpaceContains(Object* o) { return to_space_->Contains(o); }
  bool FromSpaceContains(Object* o) { return from_space_->Contains(o); }

  bool ToSpaceContains(Address a) { return to_space_->Contains(a); }
  bool FromSpaceContains(Address a) { return from_space_->Contains(a); }

#ifdef DEBUG
  // Verify the active semispace.
  void Verify();
  // Print the active semispace.
  void Print() { to_space_->Print(); }
#endif

#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  // Iterates the active semispace to collect statistics.
  void CollectStatistics();
  // Reports previously collected statistics of the active semispace.
  void ReportStatistics();
  // Clears previously collected statistics.
  void ClearHistograms();

  // Record the allocation or promotion of a heap object.  Note that we don't
  // record every single allocation, but only those that happen in the
  // to space during a scavenge GC.
  void RecordAllocation(HeapObject* obj);
  void RecordPromotion(HeapObject* obj);
#endif

 private:
  // The current and maximum capacities of a semispace.
  int capacity_;
  int maximum_capacity_;

  // The semispaces.
  SemiSpace* to_space_;
  SemiSpace* from_space_;

  // Start address and bit mask for containment testing.
  Address start_;
  uint32_t address_mask_;
  uint32_t object_mask_;
  uint32_t object_expected_;

  // Allocation pointer and limit for normal allocation and allocation during
  // mark-compact collection.
  AllocationInfo allocation_info_;
  AllocationInfo mc_forwarding_info_;

#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  HistogramInfo* allocated_histogram_;
  HistogramInfo* promoted_histogram_;
#endif

  // Implementation of AllocateRaw and MCAllocateRaw.
  inline Object* AllocateRawInternal(int size_in_bytes,
                                     AllocationInfo* alloc_info);

  friend class SemiSpaceIterator;

 public:
  TRACK_MEMORY("NewSpace")
};


// -----------------------------------------------------------------------------
// Free lists for old object spaces
//
// Free-list nodes are free blocks in the heap.  They look like heap objects
// (free-list node pointers have the heap object tag, and they have a map like
// a heap object).  They have a size and a next pointer.  The next pointer is
// the raw address of the next free list node (or NULL).
class FreeListNode: public HeapObject {
 public:
  // Obtain a free-list node from a raw address.  This is not a cast because
  // it does not check nor require that the first word at the address is a map
  // pointer.
  static FreeListNode* FromAddress(Address address) {
    return reinterpret_cast<FreeListNode*>(HeapObject::FromAddress(address));
  }

  // Set the size in bytes, which can be read with HeapObject::Size().  This
  // function also writes a map to the first word of the block so that it
  // looks like a heap object to the garbage collector and heap iteration
  // functions.
  void set_size(int size_in_bytes);

  // Accessors for the next field.
  inline Address next();
  inline void set_next(Address next);

 private:
  static const int kNextOffset = Array::kHeaderSize;

  DISALLOW_IMPLICIT_CONSTRUCTORS(FreeListNode);
};


// The free list for the old space.
class OldSpaceFreeList BASE_EMBEDDED {
 public:
  explicit OldSpaceFreeList(AllocationSpace owner);

  // Clear the free list.
  void Reset();

  // Return the number of bytes available on the free list.
  int available() { return available_; }

  // Place a node on the free list.  The block of size 'size_in_bytes'
  // starting at 'start' is placed on the free list.  The return value is the
  // number of bytes that have been lost due to internal fragmentation by
  // freeing the block.  Bookkeeping information will be written to the block,
  // ie, its contents will be destroyed.  The start address should be word
  // aligned, and the size should be a non-zero multiple of the word size.
  int Free(Address start, int size_in_bytes);

  // Allocate a block of size 'size_in_bytes' from the free list.  The block
  // is unitialized.  A failure is returned if no block is available.  The
  // number of bytes lost to fragmentation is returned in the output parameter
  // 'wasted_bytes'.  The size should be a non-zero multiple of the word size.
  Object* Allocate(int size_in_bytes, int* wasted_bytes);

 private:
  // The size range of blocks, in bytes. (Smaller allocations are allowed, but
  // will always result in waste.)
  static const int kMinBlockSize = Array::kHeaderSize + kPointerSize;
  static const int kMaxBlockSize = Page::kMaxHeapObjectSize;

  // The identity of the owning space, for building allocation Failure
  // objects.
  AllocationSpace owner_;

  // Total available bytes in all blocks on this free list.
  int available_;

  // Blocks are put on exact free lists in an array, indexed by size in words.
  // The available sizes are kept in an increasingly ordered list. Entries
  // corresponding to sizes < kMinBlockSize always have an empty free list
  // (but index kHead is used for the head of the size list).
  struct SizeNode {
    // Address of the head FreeListNode of the implied block size or NULL.
    Address head_node_;
    // Size (words) of the next larger available size if head_node_ != NULL.
    int next_size_;
  };
  static const int kFreeListsLength = kMaxBlockSize / kPointerSize + 1;
  SizeNode free_[kFreeListsLength];

  // Sentinel elements for the size list. Real elements are in ]kHead..kEnd[.
  static const int kHead = kMinBlockSize / kPointerSize - 1;
  static const int kEnd = kMaxInt;

  // We keep a "finger" in the size list to speed up a common pattern:
  // repeated requests for the same or increasing sizes.
  int finger_;

  // Starting from *prev, find and return the smallest size >= index (words),
  // or kEnd. Update *prev to be the largest size < index, or kHead.
  int FindSize(int index, int* prev) {
    int cur = free_[*prev].next_size_;
    while (cur < index) {
      *prev = cur;
      cur = free_[cur].next_size_;
    }
    return cur;
  }

  // Remove an existing element from the size list.
  void RemoveSize(int index) {
    int prev = kHead;
    int cur = FindSize(index, &prev);
    ASSERT(cur == index);
    free_[prev].next_size_ = free_[cur].next_size_;
    finger_ = prev;
  }

  // Insert a new element into the size list.
  void InsertSize(int index) {
    int prev = kHead;
    int cur = FindSize(index, &prev);
    ASSERT(cur != index);
    free_[prev].next_size_ = index;
    free_[index].next_size_ = cur;
  }

  // The size list is not updated during a sequence of calls to Free, but is
  // rebuilt before the next allocation.
  void RebuildSizeList();
  bool needs_rebuild_;

  DISALLOW_EVIL_CONSTRUCTORS(OldSpaceFreeList);
};


// The free list for the map space.
class MapSpaceFreeList BASE_EMBEDDED {
 public:
  MapSpaceFreeList();

  // Clear the free list.
  void Reset();

  // Return the number of bytes available on the free list.
  int available() { return available_; }

  // Place a node on the free list.  The block starting at 'start' (assumed to
  // have size Map::kSize) is placed on the free list.  Bookkeeping
  // information will be written to the block, ie, its contents will be
  // destroyed.  The start address should be word aligned.
  void Free(Address start);

  // Allocate a map-sized block from the free list.  The block is unitialized.
  // A failure is returned if no block is available.
  Object* Allocate();

 private:
  // Available bytes on the free list.
  int available_;

  // The head of the free list.
  Address head_;

  DISALLOW_EVIL_CONSTRUCTORS(MapSpaceFreeList);
};


// -----------------------------------------------------------------------------
// Old object space (excluding map objects)

class OldSpace : public PagedSpace {
 public:
  // Creates an old space object with a given maximum capacity.
  // The constructor does not allocate pages from OS.
  explicit OldSpace(int max_capacity, AllocationSpace id)
      : PagedSpace(max_capacity, id), free_list_(id) {
  }

  // Returns maximum available bytes that the old space can have.
  int MaxAvailable() {
    return (MemoryAllocator::Available() / Page::kPageSize)
           * Page::kObjectAreaSize;
  }

  // The bytes available on the free list (ie, not above the linear allocation
  // pointer).
  int AvailableFree() { return free_list_.available(); }

  // The top of allocation in a page in this space.
  Address PageAllocationTop(Page* page) {
    return page == TopPageOf(allocation_info_) ? top() : page->ObjectAreaEnd();
  }

  // Allocates requested bytes. May return Failure if the space is full.
  Object* AllocateRaw(int size_in_bytes) {
    ASSERT_OBJECT_SIZE(size_in_bytes);
    return AllocateRawInternal(size_in_bytes, &allocation_info_);
  }

  // Allocates requested bytes for object relocation.
  Object* MCAllocateRaw(int size_in_bytes) {
    ASSERT_OBJECT_SIZE(size_in_bytes);
    return AllocateRawInternal(size_in_bytes, &mc_forwarding_info_);
  }

  // Give a block of memory to the space's free list.  It might be added to
  // the free list or accounted as waste.
  void Free(Address start, int size_in_bytes) {
    int wasted_bytes = free_list_.Free(start, size_in_bytes);
    accounting_stats_.DeallocateBytes(size_in_bytes);
    accounting_stats_.WasteBytes(wasted_bytes);
  }

  // Prepare for full garbage collection.  Resets the relocation pointer and
  // clears the free list.
  void PrepareForMarkCompact(bool will_compact);

  // Adjust the top of relocation pointer to point to the end of the object
  // given by 'address' and 'size_in_bytes'.  Move it to the next page if
  // necessary, ensure that it points to the address, then increment it by the
  // size.
  void MCAdjustRelocationEnd(Address address, int size_in_bytes);

  // Updates the allocation pointer to the relocation top after a mark-compact
  // collection.
  void MCCommitRelocationInfo();

#ifdef DEBUG
  // Verify integrity of this space.
  void Verify();

  // Reports statistics for the space
  void ReportStatistics();
  // Dump the remembered sets in the space to stdout.
  void PrintRSet();
#endif

 private:
  // The space's free list.
  OldSpaceFreeList free_list_;

  // During relocation, we keep a pointer to the most recently relocated
  // object in order to know when to move to the next page.
  Address mc_end_of_relocation_;

  // Implementation of AllocateRaw. Allocates requested number of bytes using
  // the given allocation information according to the space's current
  // allocation mode.
  Object* AllocateRawInternal(int size_in_bytes, AllocationInfo* alloc_info);

  // Slow path of AllocateRaw functions.
  Object* SlowAllocateRaw(int size_in_bytes, AllocationInfo* alloc_info);

 public:
  TRACK_MEMORY("OldSpace")
};


// -----------------------------------------------------------------------------
// Old space for all map objects

class MapSpace : public PagedSpace {
 public:
  // Creates a map space object with a maximum capacity.
  explicit MapSpace(int max_capacity) : PagedSpace(max_capacity, MAP_SPACE) { }

  // The bytes available on the free list (ie, not above the linear allocation
  // pointer).
  int AvailableFree() { return free_list_.available(); }

  // The top of allocation in a page in this space.
  Address PageAllocationTop(Page* page) {
    return page == TopPageOf(allocation_info_) ? top()
        : page->ObjectAreaEnd() - kPageExtra;
  }

  // Allocates requested bytes. May return Failure if the space is full.
  Object* AllocateRaw(int size_in_bytes) {
    ASSERT_OBJECT_SIZE(size_in_bytes);
    return AllocateRawInternal(size_in_bytes, &allocation_info_);
  }

  // Allocates requested bytes for object relocation.
  Object* MCAllocateRaw(int size_in_bytes) {
    ASSERT_OBJECT_SIZE(size_in_bytes);
    return AllocateRawInternal(size_in_bytes, &mc_forwarding_info_);
  }

  // Give a map-sized block of memory to the space's free list.
  void Free(Address start) {
    free_list_.Free(start);
    accounting_stats_.DeallocateBytes(Map::kSize);
  }

  // Given an index, returns the page address.
  Address PageAddress(int page_index) { return page_addresses_[page_index]; }

  // Prepares for a mark-compact GC.
  void PrepareForMarkCompact(bool will_compact);

  // Updates the allocation pointer to the relocation top after a mark-compact
  // collection.
  void MCCommitRelocationInfo();

#ifdef DEBUG
  // Verify integrity of this space.
  void Verify();

  // Reports statistic info of the space
  void ReportStatistics();
  // Dump the remembered sets in the space to stdout.
  void PrintRSet();
#endif

  // Constants.
  static const int kMapPageIndexBits = 10;
  static const int kMaxMapPageIndex = (1 << kMapPageIndexBits) - 1;

  static const int kPageExtra = Page::kObjectAreaSize % Map::kSize;

 private:
  // The space's free list.
  MapSpaceFreeList free_list_;

  // An array of page start address in a map space.
  Address page_addresses_[kMaxMapPageIndex];

  // Implementation of AllocateRaw. Allocates requested bytes using
  // the given allocation information.
  Object* AllocateRawInternal(int size_in_bytes, AllocationInfo* alloc_info);

  // Slow path of AllocateRaw functions.
  Object* SlowAllocateRaw(int size_int_bytes, AllocationInfo* alloc_info);

 public:
  TRACK_MEMORY("MapSpace")
};


// -----------------------------------------------------------------------------
// Large objects ( > Page::kMaxHeapObjectSize ) are allocated and managed by
// the large object space. A large object is allocated from OS heap with
// extra padding bytes (Page::kPageSize + Page::kObjectStartOffset).
// A large object always starts at Page::kObjectStartOffset to a page.
// Large objects do not move during garbage collections.
//

// A LargeObjectChunk holds exactly one large object page with exactly one
// large object.
class LargeObjectChunk {
 public:
  // Allocates a new LargeObjectChunk that contains a large object page
  // (Page::kPageSize aligned) that has at least size_in_bytes (for a large
  // object and possibly extra remembered set words) bytes after the object
  // area start of that page. The allocated chunk size is set in the output
  // parameter chunk_size.
  static LargeObjectChunk* New(int size_in_bytes, size_t* chunk_size);

  // Interpret a raw address as a large object chunk.
  static LargeObjectChunk* FromAddress(Address address) {
    return reinterpret_cast<LargeObjectChunk*>(address);
  }

  // Returns the address of this chunk.
  Address address() { return reinterpret_cast<Address>(this); }

  // Accessors for the fields of the chunk.
  LargeObjectChunk* next() { return next_; }
  void set_next(LargeObjectChunk* chunk) { next_ = chunk; }

  size_t size() { return size_; }
  void set_size(size_t size_in_bytes) { size_ = size_in_bytes; }

  // Returns the object in this chunk.
  inline HeapObject* GetObject();

  // Given a requested size (including any extra remembereed set words),
  // returns the physical size of a chunk to be allocated.
  static int ChunkSizeFor(int size_in_bytes);

  // Given a chunk size, returns the object size it can accomodate (not
  // including any extra remembered set words).  Used by
  // LargeObjectSpace::Available.  Note that this can overestimate the size
  // of object that will fit in a chunk---if the object requires extra
  // remembered set words (eg, for large fixed arrays), the actual object
  // size for the chunk will be smaller than reported by this function.
  static int ObjectSizeFor(int chunk_size) {
    if (chunk_size <= (Page::kPageSize + Page::kObjectStartOffset)) return 0;
    return chunk_size - Page::kPageSize - Page::kObjectStartOffset;
  }

 private:
  // A pointer to the next large object chunk in the space or NULL.
  LargeObjectChunk* next_;

  // The size of this chunk.
  size_t size_;

 public:
  TRACK_MEMORY("LargeObjectChunk")
};


class LargeObjectSpace {
  friend class LargeObjectIterator;
 public:
  LargeObjectSpace();

  // Initializes internal data structures.
  bool Setup();

  // Releases internal resources, frees objects in this space.
  void TearDown();

  // Allocates a (non-FixedArray) large object.
  Object* AllocateRaw(int size_in_bytes);
  // Allocates a large FixedArray.
  Object* AllocateRawFixedArray(int size_in_bytes);

  // Available bytes for objects in this space, not including any extra
  // remembered set words.
  int Available() {
    return LargeObjectChunk::ObjectSizeFor(MemoryAllocator::Available());
  }

  int Size() {
    return size_;
  }

  int PageCount() {
    return page_count_;
  }

  // Finds an object for a given address, returns Failure::Exception()
  // if it is not found. The function iterates through all objects in this
  // space, may be slow.
  Object* FindObject(Address a);

  // Clears remembered sets.
  void ClearRSet();

  // Iterates objects whose remembered set bits are set.
  void IterateRSet(ObjectSlotCallback func);

  // Frees unmarked objects.
  void FreeUnmarkedObjects();

  // Checks whether a heap object is in this space; O(1).
  bool Contains(HeapObject* obj);

  // Checks whether the space is empty.
  bool IsEmpty() { return first_chunk_ == NULL; }

#ifdef DEBUG
  void Verify();
  void Print();
  void ReportStatistics();
  void CollectCodeStatistics();
  // Dump the remembered sets in the space to stdout.
  void PrintRSet();
#endif
  // Checks whether an address is in the object area in this space.  It
  // iterates all objects in the space. May be slow.
  bool SlowContains(Address addr) { return !FindObject(addr)->IsFailure(); }

 private:
  // The head of the linked list of large object chunks.
  LargeObjectChunk* first_chunk_;
  int size_;  // allocated bytes
  int page_count_;  // number of chunks


  // Shared implementation of AllocateRaw and AllocateRawFixedArray.
  Object* AllocateRawInternal(int requested_size, int object_size);

  // Returns the number of extra bytes (rounded up to the nearest full word)
  // required for extra_object_bytes of extra pointers (in bytes).
  static inline int ExtraRSetBytesFor(int extra_object_bytes);

 public:
  TRACK_MEMORY("LargeObjectSpace")
};


class LargeObjectIterator: public ObjectIterator {
 public:
  explicit LargeObjectIterator(LargeObjectSpace* space);
  LargeObjectIterator(LargeObjectSpace* space, HeapObjectCallback size_func);

  bool has_next() { return current_ != NULL; }
  HeapObject* next();

  // implementation of ObjectIterator.
  virtual bool has_next_object() { return has_next(); }
  virtual HeapObject* next_object() { return next(); }

 private:
  LargeObjectChunk* current_;
  HeapObjectCallback size_func_;
};


} }  // namespace v8::internal

#endif  // V8_SPACES_H_
