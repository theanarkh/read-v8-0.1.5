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

#include "v8.h"

#include "api.h"
#include "global-handles.h"

namespace v8 { namespace internal {

class GlobalHandles::Node : public Malloced {
 public:

  void Initialize(Object* object) {
    // Set the initial value of the handle.
    object_ = object;
    state_  = NORMAL;
    parameter_or_next_free_.parameter = NULL;
    callback_ = NULL;
  }

  explicit Node(Object* object) {
    Initialize(object);
    // Initialize link structure.
    next_ = NULL;
  }

  ~Node() {
    if (state_ != DESTROYED) Destroy();
#ifdef DEBUG
    // Zap the values for eager trapping.
    object_ = NULL;
    next_ = NULL;
    parameter_or_next_free_.next_free = NULL;
#endif
  }

  void Destroy() {
    if (state_ == WEAK || IsNearDeath()) {
      GlobalHandles::number_of_weak_handles_--;
      if (object_->IsJSGlobalObject()) {
        GlobalHandles::number_of_global_object_weak_handles_--;
      }
    }
    state_ = DESTROYED;
  }

  // Accessors for next_.
  Node* next() { return next_; }
  void set_next(Node* value) { next_ = value; }
  Node** next_addr() { return &next_; }

  // Accessors for next free node in the free list.
  Node* next_free() {
    ASSERT(state_ == DESTROYED);
    return parameter_or_next_free_.next_free;
  }
  void set_next_free(Node* value) {
    ASSERT(state_ == DESTROYED);
    parameter_or_next_free_.next_free = value;
  }

  // Returns a link from the handle.
  static Node* FromLocation(Object** location) {
    ASSERT(OFFSET_OF(Node, object_) == 0);
    return reinterpret_cast<Node*>(location);
  }

  // Returns the handle.
  Handle<Object> handle() { return Handle<Object>(&object_); }

  // Make this handle weak.
  void MakeWeak(void* parameter, WeakReferenceCallback callback) {
    LOG(HandleEvent("GlobalHandle::MakeWeak", handle().location()));
    if (state_ != WEAK && !IsNearDeath()) {
      GlobalHandles::number_of_weak_handles_++;
      if (object_->IsJSGlobalObject()) {
        GlobalHandles::number_of_global_object_weak_handles_++;
      }
    }
    state_ = WEAK;
    set_parameter(parameter);
    callback_ = callback;
  }

  void ClearWeakness() {
    LOG(HandleEvent("GlobalHandle::ClearWeakness", handle().location()));
    if (state_ == WEAK || IsNearDeath()) {
      GlobalHandles::number_of_weak_handles_--;
      if (object_->IsJSGlobalObject()) {
        GlobalHandles::number_of_global_object_weak_handles_--;
      }
    }
    state_ = NORMAL;
    set_parameter(NULL);
  }

  bool IsNearDeath() {
    // Check for PENDING to ensure correct answer when processing callbacks.
    return state_ == PENDING || state_ == NEAR_DEATH;
  }

  bool IsWeak() {
    return state_ == WEAK;
  }

  // Returns the id for this weak handle.
  void set_parameter(void* parameter) {
    ASSERT(state_ != DESTROYED);
    parameter_or_next_free_.parameter = parameter;
  }
  void* parameter() {
    ASSERT(state_ != DESTROYED);
    return parameter_or_next_free_.parameter;
  }

  // Returns the callback for this weak handle.
  WeakReferenceCallback callback() { return callback_; }

  void PostGarbageCollectionProcessing() {
    if (state_ != Node::PENDING) return;
    LOG(HandleEvent("GlobalHandle::Processing", handle().location()));
    void* par = parameter();
    state_ = NEAR_DEATH;
    set_parameter(NULL);
    // The callback function is resolved as late as possible to preserve old
    // behavior.
    WeakReferenceCallback func = callback();
    if (func != NULL) {
      func(v8::Persistent<v8::Object>(ToApi<v8::Object>(handle())), par);
    }
  }

  // Place the handle address first to avoid offset computation.
  Object* object_;  // Storage for object pointer.

  // Transition diagram:
  // NORMAL <-> WEAK -> PENDING -> NEAR_DEATH -> { NORMAL, WEAK, DESTROYED }
  enum State {
    NORMAL,      // Normal global handle.
    WEAK,        // Flagged as weak but not yet finalized.
    PENDING,     // Has been recognized as only reachable by weak handles.
    NEAR_DEATH,  // Callback has informed the handle is near death.
    DESTROYED
  };
  State state_;

 private:
  // Handle specific callback.
  WeakReferenceCallback callback_;
  // Provided data for callback.  In DESTROYED state, this is used for
  // the free list link.
  union {
    void* parameter;
    Node* next_free;
  } parameter_or_next_free_;

  // Linkage for the list.
  Node* next_;

 public:
  TRACK_MEMORY("GlobalHandles::Node")
};


Handle<Object> GlobalHandles::Create(Object* value) {
  Counters::global_handles.Increment();
  Node* result;
  if (first_free() == NULL) {
    // Allocate a new node.
    result = new Node(value);
    result->set_next(head());
    set_head(result);
  } else {
    // Take the first node in the free list.
    result = first_free();
    set_first_free(result->next_free());
    result->Initialize(value);
  }
  return result->handle();
}


void GlobalHandles::Destroy(Object** location) {
  Counters::global_handles.Decrement();
  if (location == NULL) return;
  Node* node = Node::FromLocation(location);
  node->Destroy();
  // Link the destroyed.
  node->set_next_free(first_free());
  set_first_free(node);
}


void GlobalHandles::MakeWeak(Object** location, void* parameter,
                             WeakReferenceCallback callback) {
  ASSERT(callback != NULL);
  Node::FromLocation(location)->MakeWeak(parameter, callback);
}


void GlobalHandles::ClearWeakness(Object** location) {
  Node::FromLocation(location)->ClearWeakness();
}


bool GlobalHandles::IsNearDeath(Object** location) {
  return Node::FromLocation(location)->IsNearDeath();
}


bool GlobalHandles::IsWeak(Object** location) {
  return Node::FromLocation(location)->IsWeak();
}


void GlobalHandles::IterateWeakRoots(ObjectVisitor* v) {
  // Traversal of GC roots in the global handle list that are marked as
  // WEAK or PENDING.
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->state_ == Node::WEAK
      || current->state_ == Node::PENDING
      || current->state_ == Node::NEAR_DEATH) {
      v->VisitPointer(&current->object_);
    }
  }
}


void GlobalHandles::MarkWeakRoots(WeakSlotCallback f) {
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->state_ == Node::WEAK) {
      if (f(&current->object_)) {
        current->state_ = Node::PENDING;
        LOG(HandleEvent("GlobalHandle::Pending", current->handle().location()));
      }
    }
  }
}


void GlobalHandles::PostGarbageCollectionProcessing() {
  // Process weak global handle callbacks. This must be done after the
  // GC is completely done, because the callbacks may invoke arbitrary
  // API functions.
  // At the same time deallocate all DESTORYED nodes
  ASSERT(Heap::gc_state() == Heap::NOT_IN_GC);
  Node** p = &head_;
  while (*p != NULL) {
    (*p)->PostGarbageCollectionProcessing();
    if ((*p)->state_ == Node::DESTROYED) {
      // Delete the link.
      Node* node = *p;
      *p = node->next();  // Update the link.
      delete node;
    } else {
      p = (*p)->next_addr();
    }
  }
  set_first_free(NULL);
}


void GlobalHandles::IterateRoots(ObjectVisitor* v) {
  // Traversal of global handles marked as NORMAL or NEAR_DEATH.
  for (Node* current = head_; current != NULL; current = current->next()) {
    if (current->state_ == Node::NORMAL) {
      v->VisitPointer(&current->object_);
    }
  }
}

void GlobalHandles::TearDown() {
  // Delete all the nodes in the linked list.
  Node* current = head_;
  while (current != NULL) {
    Node* n = current;
    current = current->next();
    delete n;
  }
  // Reset the head and free_list.
  set_head(NULL);
  set_first_free(NULL);
}


int GlobalHandles::number_of_weak_handles_ = 0;
int GlobalHandles::number_of_global_object_weak_handles_ = 0;

GlobalHandles::Node* GlobalHandles::head_ = NULL;
GlobalHandles::Node* GlobalHandles::first_free_ = NULL;

#ifdef DEBUG

void GlobalHandles::PrintStats() {
  int total = 0;
  int weak = 0;
  int pending = 0;
  int near_death = 0;
  int destroyed = 0;

  for (Node* current = head_; current != NULL; current = current->next()) {
    total++;
    if (current->state_ == Node::WEAK) weak++;
    if (current->state_ == Node::PENDING) pending++;
    if (current->state_ == Node::NEAR_DEATH) near_death++;
    if (current->state_ == Node::DESTROYED) destroyed++;
  }

  PrintF("Global Handle Statistics:\n");
  PrintF("  allocated memory = %dB\n", sizeof(Node) * total);
  PrintF("  # weak       = %d\n", weak);
  PrintF("  # pending    = %d\n", pending);
  PrintF("  # near_death = %d\n", near_death);
  PrintF("  # destroyed  = %d\n", destroyed);
  PrintF("  # total      = %d\n", total);
}

void GlobalHandles::Print() {
  PrintF("Global handles:\n");
  for (Node* current = head_; current != NULL; current = current->next()) {
    PrintF("  handle %p to %p (weak=%d)\n", current->handle().location(),
           *current->handle(), current->state_ == Node::WEAK);
  }
}

#endif

List<ObjectGroup*> GlobalHandles::object_groups_(4);

void GlobalHandles::AddToGroup(void* id, Object** handle) {
  for (int i = 0; i < object_groups_.length(); i++) {
    ObjectGroup* entry = object_groups_[i];
    if (entry->id_ == id) {
      entry->objects_.Add(handle);
      return;
    }
  }

  // not found
  ObjectGroup* new_entry = new ObjectGroup(id);
  new_entry->objects_.Add(handle);
  object_groups_.Add(new_entry);
}


void GlobalHandles::RemoveObjectGroups() {
  for (int i = 0; i< object_groups_.length(); i++) {
    delete object_groups_[i];
  }
  object_groups_.Clear();
}


} }  // namespace v8::internal
