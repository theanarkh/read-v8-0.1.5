// Copyright 2007-2008 Google Inc. All Rights Reserved.
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

#ifndef V8_V8_COUNTERS_H_
#define V8_V8_COUNTERS_H_

#include "counters.h"

namespace v8 { namespace internal {

#define STATS_RATE_LIST(SR)                                \
  SR(gc_compactor, V8.GCCompactor) /* GC Compactor time */ \
  SR(gc_scavenger, V8.GCScavenger) /* GC Scavenger time */ \
  SR(compile, V8.Compile)          /* Compile time*/       \
  SR(compile_eval, V8.CompileEval) /* Eval compile time */ \
  SR(compile_lazy, V8.CompileLazy) /* Lazy compile time */ \
  SR(parse, V8.Parse)              /* Parse time */        \
  SR(parse_lazy, V8.ParseLazy)     /* Lazy parse time */   \
  SR(pre_parse, V8.PreParse)       /* Pre-parse time */

// WARNING: STATS_COUNTER_LIST_* is a very large macro that is causing MSVC
// Intellisense to crash.  It was broken into two macros (each of length 40
// lines) rather than one macro (of length about 80 lines) to work around
// this problem.  Please avoid using recursive macros of this length when
// possible.
#define STATS_COUNTER_LIST_1(SC)                                 \
  /* Global Handle Count*/                                       \
  SC(global_handles, V8.GlobalHandles)                           \
  /* Global Object Count */                                      \
  SC(global_objects, V8.GlobalObjects)                           \
  /* Mallocs from PCRE */                                        \
  SC(pcre_mallocs, V8.PcreMallocCount)                           \
  /* OS Memory allocated */                                      \
  SC(memory_allocated, V8.OsMemoryAllocated)                     \
  SC(props_to_dictionary, V8.ObjectPropertiesToDictionary)       \
  SC(elements_to_dictionary, V8.ObjectElementsToDictionary)      \
  SC(alive_after_last_gc, V8.AliveAfterLastGC)                   \
  SC(objs_since_last_young, V8.ObjsSinceLastYoung)               \
  SC(objs_since_last_full, V8.ObjsSinceLastFull)                 \
  SC(symbol_table_capacity, V8.SymbolTableCapacity)              \
  SC(number_of_symbols, V8.NumberOfSymbols)                      \
  /* Current amount of memory in external string buffers. */     \
  SC(total_external_string_memory, V8.TotalExternalStringMemory) \
  SC(script_wrappers, V8.ScriptWrappers)                         \
  SC(call_initialize_stubs, V8.CallInitializeStubs)              \
  SC(call_premonomorphic_stubs, V8.CallPreMonomorphicStubs)      \
  SC(call_normal_stubs, V8.CallNormalStubs)                      \
  SC(call_megamorphic_stubs, V8.CallMegamorphicStubs)            \
  SC(arguments_adaptors, V8.ArgumentsAdaptors)                   \
  /* Amount of evaled source code. */                            \
  SC(total_eval_size, V8.TotalEvalSize)                          \
  /* Amount of loaded source code. */                            \
  SC(total_load_size, V8.TotalLoadSize)                          \
  /* Amount of parsed source code. */                            \
  SC(total_parse_size, V8.TotalParseSize)                        \
  /* Amount of source code skipped over using preparsing. */     \
  SC(total_preparse_skipped, V8.TotalPreparseSkipped)            \
  /* Amount of compiled source code. */                          \
  SC(total_compile_size, V8.TotalCompileSize)


#define STATS_COUNTER_LIST_2(SC)                                    \
  /* Number of code stubs. */                                       \
  SC(code_stubs, V8.CodeStubs)                                      \
  /* Amount of stub code. */                                        \
  SC(total_stubs_code_size, V8.TotalStubsCodeSize)                  \
  /* Amount of (JS) compiled code. */                               \
  SC(total_compiled_code_size, V8.TotalCompiledCodeSize)            \
  SC(gc_compactor_caused_by_request, V8.GCCompactorCausedByRequest) \
  SC(gc_compactor_caused_by_promoted_data,                          \
     V8.GCCompactorCausedByPromotedData)                            \
  SC(gc_compactor_caused_by_oldspace_exhaustion,                    \
     V8.GCCompactorCausedByOldspaceExhaustion)                      \
  SC(gc_compactor_caused_by_weak_handles,                           \
     V8.GCCompactorCausedByWeakHandles)                             \
  /* How is the generic keyed-load stub used? */                    \
  SC(keyed_load_generic_smi, V8.KeyedLoadGenericSmi)                \
  SC(keyed_load_generic_symbol, V8.KeyedLoadGenericSymbol)          \
  SC(keyed_load_generic_slow, V8.KeyedLoadGenericSlow)              \
  /* Count how much the monomorphic keyed-load stubs are hit. */    \
  SC(keyed_load_function_prototype, V8.KeyedLoadFunctionPrototype)  \
  SC(keyed_load_string_length, V8.KeyedLoadStringLength)            \
  SC(keyed_load_array_length, V8.KeyedLoadArrayLength)              \
  SC(keyed_load_constant_function, V8.KeyedLoadConstantFunction)    \
  SC(keyed_load_field, V8.KeyedLoadField)                           \
  SC(keyed_load_callback, V8.KeyedLoadCallback)                     \
  SC(keyed_load_interceptor, V8.KeyedLoadInterceptor)               \
  SC(keyed_store_field, V8.KeyedStoreField)                         \
  SC(for_in, V8.ForIn)                                              \
  SC(enum_cache_hits, V8.EnumCacheHits)                             \
  SC(enum_cache_misses, V8.EnumCacheMisses)                         \
  SC(reloc_info_count, V8.RelocInfoCount)                           \
  SC(reloc_info_size, V8.RelocInfoSize)


// This file contains all the v8 counters that are in use.
class Counters : AllStatic {
 public:
#define SR(name, caption) static StatsRate name;
  STATS_RATE_LIST(SR)
#undef SR

#define SC(name, caption) static StatsCounter name;
  STATS_COUNTER_LIST_1(SC)
  STATS_COUNTER_LIST_2(SC)
#undef SC

  enum Id {
#define RATE_ID(name, caption) k_##name,
    STATS_RATE_LIST(RATE_ID)
#undef RATE_ID
#define COUNTER_ID(name, caption) k_##name,
  STATS_COUNTER_LIST_1(COUNTER_ID)
  STATS_COUNTER_LIST_2(COUNTER_ID)
#undef COUNTER_ID
#define COUNTER_ID(name) k_##name,
  STATE_TAG_LIST(COUNTER_ID)
#undef COUNTER_ID
    stats_counter_count
  };

  // Sliding state window counters.
  static StatsCounter state_counters[];
};

} }  // namespace v8::internal

#endif  // V8_COUNTERS_H_
