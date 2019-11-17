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

#ifndef V8_UTILS_H_
#define V8_UTILS_H_

namespace v8 { namespace internal {

// ----------------------------------------------------------------------------
// General helper functions

// Returns true iff x is a power of 2.  Does not work for zero.
template <typename T>
static inline bool IsPowerOf2(T x) {
  return (x & (x - 1)) == 0;
}


// Returns smallest power of 2 greater or equal to x (from Hacker's Delight).
int32_t NextPowerOf2(uint32_t x);


// The C++ standard leaves the semantics of '>>'
// undefined for negative signed operands. Most
// implementations do the right thing, though.
static inline int ArithmeticShiftRight(int x, int s) {
  return x >> s;
}


// Compute the 0-relative offset of some absolute value x of type T.
// This allows conversion of Addresses and integral types into 0-relative
// int offsets.
// 判断两个指针的距离，每一步距离等于T的类型
template <typename T>
static inline int OffsetFrom(T x) {
  return x - static_cast<T>(0);
}


// Compute the absolute value of type T for some 0-relative offset x.
// This allows conversion of 0-relative int offsets into Addresses
// and integral types.
template <typename T>
static inline T AddressFrom(int x) {
  return static_cast<T>(0) + x;
}


// Return the largest multiple of m which is <= x.
template <typename T>
static inline T RoundDown(T x, int m) {
  ASSERT(IsPowerOf2(m));
  /*
    假设m是2，-2的底层表示为1111...10(高位为1，其余位取反再加一)，
    相与即把低y位清0,如果低位全是0，则说明是整数倍，y即2的x次方等于m中的y。
  */
  return AddressFrom<T>(OffsetFrom(x) & -m);
}


// Return the smallest multiple of m which is >= x.
template <typename T>
static inline T RoundUp(T x, int m) {
  /*
    如果x是m的整数倍，则低y位全是0，加上(m-1)不影响结果，因为会被-m清掉，
    如果x不是m的整数倍，则小于y位的值至少为1，加上(m-1)，使得结果比RoundDown多一倍，
    而RoundDown是小于y的最大值，加一倍即大于x的最小值。
  */
  return RoundDown(x + m - 1, m);
}


template <typename T>
static inline bool IsAligned(T value, T alignment) {
  ASSERT(IsPowerOf2(alignment));
  return (value & (alignment - 1)) == 0;
}


// Returns true if (addr + offset) is aligned.
static inline bool IsAddressAligned(Address addr, int alignment, int offset) {
  int offs = OffsetFrom(addr + offset);
  return IsAligned(offs, alignment);
}


// Returns the maximum of the two parameters.
template <typename T>
static T Max(T a, T b) {
  return a < b ? b : a;
}


// Returns the minimum of the two parameters.
template <typename T>
static T Min(T a, T b) {
  return a < b ? a : b;
}


// ----------------------------------------------------------------------------
// BitField is a help template for encoding and decode bitfield with unsigned
// content.
template<class T, int shift, int size>
class BitField {
 public:
  // Tells whether the provided value fits into the bit field.
  // 判断低几位之外的其他位是否都等于0
  static bool is_valid(T value) {
    return (static_cast<uint32_t>(value) & ~((1U << (size)) - 1)) == 0;
  }

  // Returns a uint32_t mask of bit field.
  // 从第shift+1到shift+size位为1，其他位为0
  static uint32_t mask() {
    return (1U << (size + shift)) - (1U << shift);
  }

  // Returns a uint32_t with the bit field value encoded.
  // 设置某位的值
  static uint32_t encode(T value) {
    ASSERT(is_valid(value));
    return static_cast<uint32_t>(value) << shift;
  }

  // Extracts the bit field from the value.
  // 取某位的值，需要屏蔽其他不相关的位的值
  static T decode(uint32_t value) {
    return static_cast<T>((value >> shift) & ((1U << (size)) - 1));
  }
};


// ----------------------------------------------------------------------------
// Support for compressed, machine-independent encoding
// and decoding of integer values of arbitrary size.

// Encoding and decoding from/to a buffer at position p;
// the result is the position after the encoded integer.
// Small signed integers in the range -64 <= x && x < 64
// are encoded in 1 byte; larger values are encoded in 2
// or more bytes. At most sizeof(int) + 1 bytes are used
// in the worst case.
byte* EncodeInt(byte* p, int x);
byte* DecodeInt(byte* p, int* x);


// Encoding and decoding from/to a buffer at position p - 1
// moving backward; the result is the position of the last
// byte written. These routines are useful to read/write
// into a buffer starting at the end of the buffer.
byte* EncodeUnsignedIntBackward(byte* p, unsigned int x);

// The decoding function is inlined since its performance is
// important to mark-sweep garbage collection.
inline byte* DecodeUnsignedIntBackward(byte* p, unsigned int* x) {
  byte b = *--p;
  if (b >= 128) {
    *x = static_cast<unsigned int>(b) - 128;
    return p;
  }
  unsigned int r = static_cast<unsigned int>(b);
  unsigned int s = 7;
  b = *--p;
  while (b < 128) {
    r |= static_cast<unsigned int>(b) << s;
    s += 7;
    b = *--p;
  }
  // b >= 128
  *x = r | ((static_cast<unsigned int>(b) - 128) << s);
  return p;
}


// ----------------------------------------------------------------------------
// I/O support.

// Our version of printf(). Avoids compilation errors that we get
// with standard printf when attempting to print pointers, etc.
// (the errors are due to the extra compilation flags, which we
// want elsewhere).
void PrintF(const char* format, ...);

// Our version of fflush.
void Flush();


// Read a line of characters after printing the prompt to stdout. The resulting
// char* needs to be disposed off with DeleteArray by the caller.
char* ReadLine(const char* prompt);


// Read and return the raw chars in a file. the size of the buffer is returned
// in size.
// The returned buffer is not 0-terminated. It must be freed by the caller.
char* ReadChars(const char* filename, int* size, bool verbose = true);


// Write size chars from str to the file given by filename.
// The file is overwritten. Returns the number of chars written.
int WriteChars(const char* filename,
               const char* str,
               int size,
               bool verbose = true);


// Write the C code
// const char* <varname> = "<str>";
// const int <varname>_len = <len>;
// to the file given by filename. Only the first len chars are written.
int WriteAsCFile(const char* filename, const char* varname,
                 const char* str, int size, bool verbose = true);


// ----------------------------------------------------------------------------
// Miscellaneous

// A static resource holds a static instance that can be reserved in
// a local scope using an instance of Access.  Attempts to re-reserve
// the instance will cause an error.
template <typename T>
class StaticResource {
 public:
  StaticResource() : is_reserved_(false)  {}

 private:
  template <typename S> friend class Access;
  T instance_;
  bool is_reserved_;
};


// Locally scoped access to a static resource.
template <typename T>
class Access {
 public:
  explicit Access(StaticResource<T>* resource)
    : resource_(resource)
    , instance_(&resource->instance_) {
    ASSERT(!resource->is_reserved_);
    resource->is_reserved_ = true;
  }

  ~Access() {
    resource_->is_reserved_ = false;
    resource_ = NULL;
    instance_ = NULL;
  }

  T* value()  { return instance_; }
  T* operator -> ()  { return instance_; }

 private:
  StaticResource<T>* resource_;
  T* instance_;
};


template <typename T>
class Vector {
 public:
  Vector(T* data, int length) : start_(data), length_(length) {
    ASSERT(length == 0 || (length > 0 && data != NULL));
  }

  // Returns the length of the vector.
  int length() const { return length_; }

  // Returns whether or not the vector is empty.
  bool is_empty() const { return length_ == 0; }

  // Returns the pointer to the start of the data in the vector.
  T* start() const { return start_; }

  // Access individual vector elements - checks bounds in debug mode.
  T& operator[](int index) const {
    ASSERT(0 <= index && index < length_);
    return start_[index];
  }

  // Returns a clone of this vector with a new backing store.
  Vector<T> Clone() const {
    T* result = NewArray<T>(length_);
    for (int i = 0; i < length_; i++) result[i] = start_[i];
    return Vector<T>(result, length_);
  }

  // Releases the array underlying this vector. Once disposed the
  // vector is empty.
  void Dispose() {
    DeleteArray(start_);
    start_ = NULL;
    length_ = 0;
  }

  // Factory method for creating empty vectors.
  static Vector<T> empty() { return Vector<T>(NULL, 0); }

 private:
  T* start_;
  int length_;
};


inline Vector<const char> CStrVector(const char* data) {
  return Vector<const char>(data, strlen(data));
}

inline Vector<char> MutableCStrVector(char* data) {
  return Vector<char>(data, strlen(data));
}

template <typename T>
inline Vector< Handle<Object> > HandleVector(v8::internal::Handle<T>* elms,
                                             int length) {
  return Vector< Handle<Object> >(
      reinterpret_cast<v8::internal::Handle<Object>*>(elms), length);
}


// Simple support to read a file into a 0-terminated C-string.
// The returned buffer must be freed by the caller.
// On return, *exits tells whether the file exisited.
Vector<const char> ReadFile(const char* filename,
                            bool* exists,
                            bool verbose = true);


// Simple wrapper that allows an ExternalString to refer to a
// Vector<const char>. Doesn't assume ownership of the data.
class AsciiStringAdapter: public v8::String::ExternalAsciiStringResource {
 public:
  explicit AsciiStringAdapter(Vector<const char> data) : data_(data) {}

  virtual const char* data() const { return data_.start(); }

  virtual size_t length() const { return data_.length(); }

 private:
  Vector<const char> data_;
};


} }  // namespace v8::internal

#endif  // V8_UTILS_H_
