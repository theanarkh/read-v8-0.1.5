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

#include <stdarg.h>

#include "v8.h"

#include "platform.h"

#include "sys/stat.h"

namespace v8 { namespace internal {


int32_t NextPowerOf2(uint32_t x) {
  x = x - 1;
  x = x | (x >> 1);
  x = x | (x >> 2);
  x = x | (x >> 4);
  x = x | (x >> 8);
  x = x | (x >> 16);
  return x + 1;
}


byte* EncodeInt(byte* p, int x) {
  while (x < -64 || x >= 64) {
    *p++ = static_cast<byte>(x & 127);
    x = ArithmeticShiftRight(x, 7);
  }
  // -64 <= x && x < 64
  *p++ = static_cast<byte>(x + 192);
  return p;
}


byte* DecodeInt(byte* p, int* x) {
  int r = 0;
  unsigned int s = 0;
  byte b = *p++;
  while (b < 128) {
    r |= static_cast<int>(b) << s;
    s += 7;
    b = *p++;
  }
  // b >= 128
  *x = r | ((static_cast<int>(b) - 192) << s);
  return p;
}


byte* EncodeUnsignedIntBackward(byte* p, unsigned int x) {
  while (x >= 128) {
    *--p = static_cast<byte>(x & 127);
    x = x >> 7;
  }
  // x < 128
  *--p = static_cast<byte>(x + 128);
  return p;
}


void PrintF(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  OS::VPrint(format, arguments);
  va_end(arguments);
}


void Flush() {
  fflush(stdout);
}


char* ReadLine(const char* prompt) {
  char* result = NULL;
  char line_buf[256];
  int offset = 0;
  bool keep_going = true;
  fprintf(stdout, prompt);
  fflush(stdout);
  while (keep_going) {
    if (fgets(line_buf, sizeof(line_buf), stdin) == NULL) {
      // fgets got an error. Just give up.
      if (result != NULL) {
        DeleteArray(result);
      }
      return NULL;
    }
    int len = strlen(line_buf);
    if (len > 1 &&
        line_buf[len - 2] == '\\' &&
        line_buf[len - 1] == '\n') {
      // When we read a line that ends with a "\" we remove the escape and
      // append the remainder.
      line_buf[len - 2] = '\n';
      line_buf[len - 1] = 0;
      len -= 1;
    } else if ((len > 0) && (line_buf[len - 1] == '\n')) {
      // Since we read a new line we are done reading the line. This
      // will exit the loop after copying this buffer into the result.
      keep_going = false;
    }
    if (result == NULL) {
      // Allocate the initial result and make room for the terminating '\0'
      result = NewArray<char>(len + 1);
    } else {
      // Allocate a new result with enough room for the new addition.
      int new_len = offset + len + 1;
      char* new_result = NewArray<char>(new_len);
      // Copy the existing input into the new array and set the new
      // array as the result.
      memcpy(new_result, result, offset * kCharSize);
      DeleteArray(result);
      result = new_result;
    }
    // Copy the newly read line into the result.
    memcpy(result + offset, line_buf, len * kCharSize);
    offset += len;
  }
  ASSERT(result != NULL);
  result[offset] = '\0';
  return result;
}


char* ReadCharsFromFile(const char* filename,
                        int* size,
                        int extra_space,
                        bool verbose) {
  FILE* file = fopen(filename, "rb");
  if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
    if (verbose) {
      OS::PrintError("Cannot read from file %s.\n", filename);
    }
    return NULL;
  }

  // Get the size of the file and rewind it.
  *size = ftell(file);
  rewind(file);

  char* result = NewArray<char>(*size + extra_space);
  for (int i = 0; i < *size;) {
    int read = fread(&result[i], 1, *size - i, file);
    if (read <= 0) {
      fclose(file);
      DeleteArray(result);
      return NULL;
    }
    i += read;
  }
  fclose(file);
  return result;
}


char* ReadChars(const char* filename, int* size, bool verbose) {
  return ReadCharsFromFile(filename, size, 0, verbose);
}


Vector<const char> ReadFile(const char* filename,
                            bool* exists,
                            bool verbose) {
  int size;
  char* result = ReadCharsFromFile(filename, &size, 1, verbose);
  if (!result) {
    *exists = false;
    return Vector<const char>::empty();
  }
  result[size] = '\0';
  *exists = true;
  return Vector<const char>(result, size);
}


int WriteCharsToFile(const char* str, int size, FILE* f) {
  int total = 0;
  while (total < size) {
    int write = fwrite(str, 1, size - total, f);
    if (write == 0) {
      return total;
    }
    total += write;
    str += write;
  }
  return total;
}


int WriteChars(const char* filename,
               const char* str,
               int size,
               bool verbose) {
  FILE* f = fopen(filename, "wb");
  if (f == NULL) {
    if (verbose) {
      OS::PrintError("Cannot open file %s for reading.\n", filename);
    }
    return 0;
  }
  int written = WriteCharsToFile(str, size, f);
  fclose(f);
  return written;
}


} }  // namespace v8::internal
