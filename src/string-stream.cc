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

#include "factory.h"
#include "string-stream.h"

namespace v8 { namespace internal {

static const int kMentionedObjectCacheMaxSize = 256;
static List<HeapObject*, PreallocatedStorage>* debug_object_cache = NULL;
static Object* current_security_token = NULL;


char* HeapStringAllocator::allocate(unsigned bytes) {
  space_ = NewArray<char>(bytes);
  return space_;
}


NoAllocationStringAllocator::NoAllocationStringAllocator(unsigned bytes) {
  size_ = bytes;
  space_ = NewArray<char>(bytes);
}


NoAllocationStringAllocator::NoAllocationStringAllocator(char* memory,
                                                         unsigned size) {
  size_ = size;
  space_ = memory;
}


bool StringStream::Put(char c) {
  if (space() == 0) return false;
  if (length_ >= capacity_ - 1) {
    unsigned new_capacity = capacity_;
    char* new_buffer = allocator_->grow(&new_capacity);
    if (new_capacity > capacity_) {
      capacity_ = new_capacity;
      buffer_ = new_buffer;
    } else {
      // Indicate truncation with dots.
      memset(cursor(), '.', space());
      length_ = capacity_;
      buffer_[length_ - 2] = '\n';
      buffer_[length_ - 1] = '\0';
      return false;
    }
  }
  buffer_[length_] = c;
  buffer_[length_ + 1] = '\0';
  length_++;
  return true;
}


// A control character is one that configures a format element.  For
// instance, in %.5s, .5 are control characters.
static bool IsControlChar(char c) {
  switch (c) {
  case '0': case '1': case '2': case '3': case '4': case '5':
  case '6': case '7': case '8': case '9': case '.': case '-':
    return true;
  default:
    return false;
  }
}


void StringStream::Add(const char* format, Vector<FmtElm> elms) {
  // If we already ran out of space then return immediately.
  if (space() == 0)
    return;
  int offset = 0;
  int elm = 0;
  while (format[offset] != '\0') {
    if (format[offset] != '%' || elm == elms.length()) {
      Put(format[offset]);
      offset++;
      continue;
    }
    // Read this formatting directive into a temporary buffer
    const int kTempSize = 24;
    char temp_buffer[kTempSize];
    // Wrap temp buffer in a vector to get bounds checking in debug
    // mode
    Vector<char> temp(temp_buffer, kTempSize);
    int format_length = 0;
    // Skip over the whole control character sequence until the
    // format element type
    temp[format_length++] = format[offset++];
    // '\0' is not a control character so we don't have to
    // explicitly check for the end of the string
    while (IsControlChar(format[offset]))
      temp[format_length++] = format[offset++];
    char type = format[offset];
    if (type == '\0') return;
    temp[format_length++] = type;
    temp[format_length] = '\0';
    offset++;
    FmtElm current = elms[elm++];
    switch (type) {
    case 's': {
      ASSERT_EQ(FmtElm::C_STR, current.type_);
      const char* value = current.data_.u_c_str_;
      Add(value);
      break;
    }
    case 'o': {
      ASSERT_EQ(FmtElm::OBJ, current.type_);
      Object* obj = current.data_.u_obj_;
      PrintObject(obj);
      break;
    }
    case 'i': case 'd': case 'u': case 'x': case 'c': case 'p': {
      int value = current.data_.u_int_;
      char formatted[kTempSize];
#ifdef WIN32
      // This is not my idea of a good time.
      _snprintf(formatted, kTempSize, temp.start(), value);
#else
      snprintf(formatted, kTempSize, temp.start(), value);
#endif
      Add(formatted);
      break;
    }
    default:
      UNREACHABLE();
      break;
    }
  }

  // Verify that the buffer is 0-terminated and doesn't contain any
  // other 0-characters.
  ASSERT(buffer_[length_] == '\0');
  ASSERT(strlen(buffer_) == length_);
}


void StringStream::PrintObject(Object* o) {
  o->ShortPrint(this);
  if (o->IsString()) {
    if (String::cast(o)->length() <= String::kMaxMediumStringSize) {
      return;
    }
  } else if (o->IsNumber() || o->IsOddball()) {
    return;
  }
  if (o->IsHeapObject()) {
    for (int i = 0; i < debug_object_cache->length(); i++) {
      if ((*debug_object_cache)[i] == o) {
        Add("#%d#", i);
        return;
      }
    }
    if (debug_object_cache->length() < kMentionedObjectCacheMaxSize) {
      Add("#%d#", debug_object_cache->length());
      debug_object_cache->Add(HeapObject::cast(o));
    } else {
      Add("@%p", o);
    }
  }
}


void StringStream::Add(const char* format) {
  Add(format, Vector<FmtElm>::empty());
}


void StringStream::Add(const char* format, FmtElm arg0) {
  const char argc = 1;
  FmtElm argv[argc] = { arg0 };
  Add(format, Vector<FmtElm>(argv, argc));
}


void StringStream::Add(const char* format, FmtElm arg0, FmtElm arg1) {
  const char argc = 2;
  FmtElm argv[argc] = { arg0, arg1 };
  Add(format, Vector<FmtElm>(argv, argc));
}


void StringStream::Add(const char* format, FmtElm arg0, FmtElm arg1,
                       FmtElm arg2) {
  const char argc = 3;
  FmtElm argv[argc] = { arg0, arg1, arg2 };
  Add(format, Vector<FmtElm>(argv, argc));
}


SmartPointer<char> StringStream::ToCString() {
  char* str = NewArray<char>(length_ + 1);
  memcpy(str, buffer_, length_);
  str[length_] = '\0';
  return SmartPointer<char>(str);
}


void StringStream::Log() {
  LOG(StringEvent("StackDump", buffer_));
}


void StringStream::OutputToStdOut() {
  // Dump the output to stdout, but make sure to break it up into
  // manageable chunks to avoid losing parts of the output in the OS
  // printing code. This is a problem on Windows in particular; see
  // the VPrint() function implementations in platform-win32.cc.
  unsigned position = 0;
  for (unsigned next; (next = position + 2048) < length_; position = next) {
    char save = buffer_[next];
    buffer_[next] = '\0';
    internal::PrintF("%s", &buffer_[position]);
    buffer_[next] = save;
  }
  internal::PrintF("%s", &buffer_[position]);
}


Handle<String> StringStream::ToString() {
  return Factory::NewStringFromUtf8(Vector<const char>(buffer_, length_));
}


void StringStream::ClearMentionedObjectCache() {
  current_security_token = NULL;
  if (debug_object_cache == NULL) {
    debug_object_cache = new List<HeapObject*, PreallocatedStorage>(0);
  }
  debug_object_cache->Clear();
}


#ifdef DEBUG
bool StringStream::IsMentionedObjectCacheClear() {
  return (debug_object_cache->length() == 0);
}
#endif


bool StringStream::Put(String* str) {
  return Put(str, 0, str->length());
}


bool StringStream::Put(String* str, int start, int end) {
  StringInputBuffer name_buffer(str);
  name_buffer.Seek(start);
  for (int i = start; i < end && name_buffer.has_more(); i++) {
    int c = name_buffer.GetNext();
    if (c >= 127 || c < 32) {
      c = '?';
    }
    if (!Put(c)) {
      return false;  // Output was truncated.
    }
  }
  return true;
}


void StringStream::PrintName(Object* name) {
  if (name->IsString()) {
    String* str = String::cast(name);
    if (str->length() > 0) {
      Put(str);
    } else {
      Add("/* anonymous */");
    }
  } else {
    Add("%o", name);
  }
}


void StringStream::PrintUsingMap(JSObject* js_object) {
  Map* map = js_object->map();
  if (!Heap::Contains(map) ||
      !map->IsHeapObject() ||
      !map->IsMap()) {
    Add("<Invalid map>\n");
    return;
  }
  for (DescriptorReader r(map->instance_descriptors()); !r.eos(); r.advance()) {
    switch (r.type()) {
      case FIELD: {
        Object* key = r.GetKey();
        if (key->IsString() || key->IsNumber()) {
          int len = 3;
          if (key->IsString()) {
            len = String::cast(key)->length();
          }
          for (; len < 18; len++)
            Put(' ');
          if (key->IsString()) {
            Put(String::cast(key));
          } else {
            key->ShortPrint();
          }
          Add(": ");
          Object* value = js_object->properties()->get(r.GetFieldIndex());
          Add("%o\n", value);
        }
      }
      break;
      default:
      break;
    }
  }
}


void StringStream::PrintFixedArray(FixedArray* array, unsigned int limit) {
  for (unsigned int i = 0; i < 10 && i < limit; i++) {
    Object* element = array->get(i);
    if (element != Heap::the_hole_value()) {
      for (int len = 1; len < 18; len++)
        Put(' ');
      Add("%d: %o\n", i, array->get(i));
    }
  }
  if (limit >= 10) {
    Add("                  ...\n");
  }
}


void StringStream::PrintByteArray(ByteArray* byte_array) {
  unsigned int limit = byte_array->length();
  for (unsigned int i = 0; i < 10 && i < limit; i++) {
    byte b = byte_array->get(i);
    Add("             %d: %3d 0x%02x", i, b, b);
    if (b >= ' ' && b <= '~') {
      Add(" '%c'", b);
    } else if (b == '\n') {
      Add(" '\n'");
    } else if (b == '\r') {
      Add(" '\r'");
    } else if (b >= 1 && b <= 26) {
      Add(" ^%c", b + 'A' - 1);
    }
    Add("\n");
  }
  if (limit >= 10) {
    Add("                  ...\n");
  }
}


void StringStream::PrintMentionedObjectCache() {
  Add("==== Key         ============================================\n\n");
  for (int i = 0; i < debug_object_cache->length(); i++) {
    HeapObject* printee = (*debug_object_cache)[i];
    Add(" #%d# %p: ", i, printee);
    printee->ShortPrint(this);
    Add("\n");
    if (printee->IsJSObject()) {
      if (printee->IsJSValue()) {
        Add("           value(): %o\n", JSValue::cast(printee)->value());
      }
      PrintUsingMap(JSObject::cast(printee));
      if (printee->IsJSArray()) {
        JSArray* array = JSArray::cast(printee);
        if (array->HasFastElements()) {
          unsigned int limit = FixedArray::cast(array->elements())->length();
          unsigned int length =
            static_cast<uint32_t>(JSArray::cast(array)->length()->Number());
          if (length < limit) limit = length;
          PrintFixedArray(FixedArray::cast(array->elements()), limit);
        }
      }
    } else if (printee->IsByteArray()) {
      PrintByteArray(ByteArray::cast(printee));
    } else if (printee->IsFixedArray()) {
      unsigned int limit = FixedArray::cast(printee)->length();
      PrintFixedArray(FixedArray::cast(printee), limit);
    }
  }
}


void StringStream::PrintSecurityTokenIfChanged(Object* f) {
  if (!f->IsHeapObject() || !Heap::Contains(HeapObject::cast(f))) {
    return;
  }
  Map* map = HeapObject::cast(f)->map();
  if (!map->IsHeapObject() ||
      !Heap::Contains(map) ||
      !map->IsMap() ||
      !f->IsJSFunction()) {
    return;
  }

  JSFunction* fun = JSFunction::cast(f);
  Object* perhaps_context = fun->unchecked_context();
  if (perhaps_context->IsHeapObject() &&
      Heap::Contains(HeapObject::cast(perhaps_context)) &&
      perhaps_context->IsContext()) {
    Context* context = fun->context();
    if (!Heap::Contains(context)) {
      Add("(Function context is outside heap)\n");
      return;
    }
    GlobalObject* global = context->global();
    if (!Heap::Contains(global)) {
      Add("(Function context global is outside heap)\n");
      return;
    }
    if (global->IsJSGlobalObject()) {
      Object* token = JSGlobalObject::cast(global)->security_token();
      if (token != current_security_token) {
        Add("Security context: %o\n", token);
        current_security_token = token;
      }
    } else {
      Add("(No security context)\n");
    }
  } else {
    Add("(Function context is corrupt)\n");
  }
}


void StringStream::PrintFunction(Object* f, Object* receiver, Code** code) {
  if (f->IsHeapObject() &&
      Heap::Contains(HeapObject::cast(f)) &&
      Heap::Contains(HeapObject::cast(f)->map()) &&
      HeapObject::cast(f)->map()->IsMap()) {
    if (f->IsJSFunction()) {
      JSFunction* fun = JSFunction::cast(f);
      // Common case: on-stack function present and resolved.
      PrintPrototype(fun, receiver);
      *code = fun->code();
    } else if (f->IsSymbol()) {
      // Unresolved and megamorphic calls: Instead of the function
      // we have the function name on the stack.
      PrintName(f);
      Add("/* unresolved */ ");
    } else {
      // Unless this is the frame of a built-in function, we should always have
      // the callee function or name on the stack. If we don't, we have a
      // problem or a change of the stack frame layout.
      Add("%o", f);
      Add("/* warning: no JSFunction object or function name found */ ");
    }
    /* } else if (is_trampoline()) {
       Print("trampoline ");
    */
  } else {
    if (!f->IsHeapObject()) {
      Add("/* warning: 'function' was not a heap object */ ");
      return;
    }
    if (!Heap::Contains(HeapObject::cast(f))) {
      Add("/* warning: 'function' was not on the heap */ ");
      return;
    }
    if (!Heap::Contains(HeapObject::cast(f)->map())) {
      Add("/* warning: function's map was not on the heap */ ");
      return;
    }
    if (!HeapObject::cast(f)->map()->IsMap()) {
      Add("/* warning: function's map was not a valid map */ ");
      return;
    }
    Add("/* warning: Invalid JSFunction object found */ ");
  }
}


void StringStream::PrintPrototype(JSFunction* fun, Object* receiver) {
  Object* name = fun->shared()->name();
  bool print_name = false;
  for (Object* p = receiver; p != Heap::null_value(); p = p->GetPrototype()) {
    if (p->IsJSObject()) {
      Object* key = JSObject::cast(p)->SlowReverseLookup(fun);
      if (key != Heap::undefined_value()) {
        if (!name->IsString() ||
            !key->IsString() ||
            !String::cast(name)->Equals(String::cast(key))) {
          print_name = true;
        }
        if (name->IsString() && String::cast(name)->length() == 0) {
          print_name = false;
        }
        name = key;
      }
    } else {
      print_name = true;
    }
  }
  PrintName(name);
  // Also known as - if the name in the function doesn't match the name under
  // which it was looked up.
  if (print_name) {
    Add("(aka ");
    PrintName(fun->shared()->name());
    Put(')');
  }
}


char* HeapStringAllocator::grow(unsigned* bytes) {
  unsigned new_bytes = *bytes * 2;
  // Check for overflow.
  if (new_bytes <= *bytes) {
    return space_;
  }
  char* new_space = NewArray<char>(new_bytes);
  if (new_space == NULL) {
    return space_;
  }
  memcpy(new_space, space_, *bytes);
  *bytes = new_bytes;
  DeleteArray(space_);
  space_ = new_space;
  return new_space;
}


char* NoAllocationStringAllocator::grow(unsigned* bytes) {
  unsigned new_bytes = *bytes * 2;
  if (new_bytes > size_) {
    new_bytes = size_;
  }
  *bytes = new_bytes;
  return space_;
}


} }  // namespace v8::internal
