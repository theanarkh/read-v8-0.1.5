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
  // 指针的地址
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
    // 更新统计数据
    if (state_ != WEAK && !IsNearDeath()) {
      GlobalHandles::number_of_weak_handles_++;
      if (object_->IsJSGlobalObject()) {
        GlobalHandles::number_of_global_object_weak_handles_++;
      }
    }
    // 修改状态和销毁时执行的回调和参数
    state_ = WEAK;
    set_parameter(parameter);
    callback_ = callback;
    }
  // 和上面的函数相反
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

// 一个handle对应一个Node
Handle<Object> GlobalHandles::Create(Object* value) {
  Counters::global_handles.Increment();
  Node* result;
  /*
    有一个free_list，保存着DESTROYED状态但还没有被释放的Node，
    first_free指向第一个节点，为NULL说明没有待回收的节点，即没有可重用的节点 
  */
  if (first_free() == NULL) {
    // Allocate a new node.
    // 没有可重用的节点则分配一个新的
    result = new Node(value);
    // 头插法，设置新增的node的下一个节点是当前头结点
    result->set_next(head());
    // 头指针指向新增的node
    set_head(result);
  } else {
    // Take the first node in the free list.
    // 获取一个可以重用的节点
    result = first_free();
    // 获取重用节点在free_list中的第一个节点，first_free指向新的可重用节点 
    set_first_free(result->next_free());
    // 重新初始化该节点
    result->Initialize(value);
  }
  // 返回一个handle对象
  return result->handle();
}

// 销毁一个节点
void GlobalHandles::Destroy(Object** location) {
  Counters::global_handles.Decrement();
  if (location == NULL) return;
  Node* node = Node::FromLocation(location);
  node->Destroy();
  // Link the destroyed.
  // 设置待销毁节点在free_list链表里的下一个节点是当前的头结点
  node->set_next_free(first_free());
  // 头指针指向待销毁的节点，
  set_first_free(node);
}

// 更新node的状态
void GlobalHandles::MakeWeak(Object** location, void* parameter,
                             WeakReferenceCallback callback) {
  ASSERT(callback != NULL);
  Node::FromLocation(location)->MakeWeak(parameter, callback);
}

// 恢复node的状态
void GlobalHandles::ClearWeakness(Object** location) {
  Node::FromLocation(location)->ClearWeakness();
}


bool GlobalHandles::IsNearDeath(Object** location) {
  return Node::FromLocation(location)->IsNearDeath();
}


bool GlobalHandles::IsWeak(Object** location) {
  return Node::FromLocation(location)->IsWeak();
}

// 遍历链表，ObjectVisitor是基类，传进来的是具体的子类（多态）
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

// 垃圾回收的时候处理已经销毁的handle
void GlobalHandles::PostGarbageCollectionProcessing() {
  // Process weak global handle callbacks. This must be done after the
  // GC is completely done, because the callbacks may invoke arbitrary
  // API functions.
  // At the same time deallocate all DESTORYED nodes
  // 垃圾回收完之后
  ASSERT(Heap::gc_state() == Heap::NOT_IN_GC);
  Node** p = &head_;
  while (*p != NULL) {
    // 调用Node的函数
    (*p)->PostGarbageCollectionProcessing();
    // 销毁了则清理内存
    if ((*p)->state_ == Node::DESTROYED) {
      // Delete the link.
      Node* node = *p;
      // 指向下一个节点，即从链表中删除该节点
      *p = node->next();  // Update the link.
      delete node;
    } else {
      p = (*p)->next_addr();
    }
  }
  // 重置free队列的头指针
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
// 销毁所有节点，重置头指针
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

// 初始化这两个头指针
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
