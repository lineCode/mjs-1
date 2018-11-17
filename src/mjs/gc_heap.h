#ifndef MJS_GC_HEAP_H
#define MJS_GC_HEAP_H

// TODO: gc_type_info_registration: Make sure all known classes are (thread-safely) registered.

#include <string>
#include <vector>
#include <algorithm>
#include <ostream>
#include <typeinfo>
#include <cstdlib>
#include <cassert>
#include <cstddef>
#include <cstring>

namespace mjs {

class value;
class object;
class gc_heap;
class gc_heap_ptr_untyped;
template<typename T>
class gc_heap_ptr;
template<typename T>
class gc_heap_ptr_untracked;

class gc_type_info {
public:
    // Destroy the object at 'p'
    void destroy(void* p) const {
        if (destroy_) {
            destroy_(p);
        }
    }

    // Move the object from 'from' to 'to'
    void move(void* to, void* from) const {
        move_(to, from);
    }

    // Handle fixup of untacked pointers (happens after the object has been otherwise moved to avoid infite recursion)
    void fixup(void* p) const {
        if (fixup_) {
            fixup_(p);
        }
    }

    // For debugging purposes only
    const char* name() const {
        return name_;
    }

    // Is the type convertible to object?
    bool is_convertible_to_object() const {
        return convertible_to_object_;
    }
protected:
    using destroy_function = void (*)(void*);
    using move_function = void (*)(void*, void*);
    using fixup_function = void (*)(void*);

    explicit gc_type_info(destroy_function destroy, move_function move, fixup_function fixup, bool convertible_to_object, const char* name)
        : destroy_(destroy)
        , move_(move)
        , fixup_(fixup)
        , convertible_to_object_(convertible_to_object)
        , name_(name) {
        types_.push_back(this);
        assert(move_);
        assert(name_);
    }

private:
    destroy_function destroy_;
    move_function move_;
    fixup_function fixup_;
    bool convertible_to_object_;
    const char* name_;
    friend gc_heap;

    gc_type_info(gc_type_info&) = delete;
    gc_type_info& operator=(gc_type_info&) = delete;

    static std::vector<const gc_type_info*> types_;

    uint32_t get_index() const {
        auto it = std::find(types_.begin(), types_.end(), this);
        if (it == types_.end()) {
            std::abort();
        }
        return static_cast<uint32_t>(it - types_.begin());
    }
};

template<typename T>
class gc_type_info_registration : public gc_type_info {
public:
    static const gc_type_info_registration& get() {
        static const gc_type_info_registration reg;
        return reg;
    }

    bool is_convertible(const gc_type_info& t) const {
        return &t == this || (std::is_same_v<object, T> && t.is_convertible_to_object());
    }

private:
    explicit gc_type_info_registration() : gc_type_info(std::is_trivially_destructible_v<T>?nullptr:&destroy, &move, fixup_func(), std::is_convertible_v<T*, object*>, typeid(T).name()) {}

    friend gc_heap;

    // Helper so gc_*** classes don't have to friend both gc_heap and gc_type_info_registration
    template<typename... Args>
    static void construct(void* p, Args&&... args) {
        new (p) T(std::forward<Args>(args)...);
    }

    static void destroy(void* p) {
        static_cast<T*>(p)->~T();
    }

    // Detectors
    template<typename U, typename=void>
    struct has_fixup_t : std::false_type{};

    template<typename U>
    struct has_fixup_t<U, std::void_t<decltype(std::declval<U>().fixup())>> : std::true_type{};

    static void move(void* to, void* from) {
        new (to) T (std::move(*static_cast<T*>(from)));
    }

    static fixup_function fixup_func() {
        if constexpr(has_fixup_t<T>::value) {
            return [](void* p) { static_cast<T*>(p)->fixup(); };
        } else {
            return nullptr;
        }
    }
};

class value_representation {
public:
    value_representation() = default;
    explicit value_representation(const value& v) : repr_(to_representation(v)) {}
    value_representation& operator=(const value& v) {
        repr_ = to_representation(v);
        return *this;
    }
    value get_value(gc_heap& heap) const;
    void fixup_after_move(gc_heap& old_heap);
private:
    uint64_t repr_;

    static uint64_t to_representation(const value& v);
};
static_assert(sizeof(value_representation) == sizeof(uint64_t));

class scoped_gc_heap;
class gc_heap {
public:
    friend gc_heap_ptr_untyped;
    friend scoped_gc_heap;
    friend value_representation;
    template<typename> friend class gc_heap_ptr_untracked;

    static constexpr uint32_t slot_size = sizeof(uint64_t);
    static constexpr uint32_t bytes_to_slots(size_t bytes) { return static_cast<uint32_t>((bytes + slot_size - 1) / slot_size); }

    explicit gc_heap(uint32_t capacity);
    ~gc_heap();

    void debug_print(std::wostream& os) const;
    uint32_t calc_used() const;

    void garbage_collect();

    template<typename T, typename... Args>
    gc_heap_ptr<T> allocate_and_construct(size_t num_bytes, Args&&... args);

    template<typename T, typename... Args>
    gc_heap_ptr<T> make(Args&&... args) {
        return allocate_and_construct<T>(sizeof(T), std::forward<Args>(args)...);
    }

private:
    static constexpr uint32_t unallocated_type_index = UINT32_MAX;
    static constexpr uint32_t gc_moved_type_index    = unallocated_type_index-1;

    struct slot_allocation_header {
        uint32_t size;
        uint32_t type;

        constexpr bool active() const {
            return type != unallocated_type_index && type != gc_moved_type_index;
        }

        const gc_type_info& type_info() const {
            assert(active() && type < gc_type_info::types_.size());
            return *gc_type_info::types_[type];
        }
    };

    union slot {
        uint64_t               representation;
        uint32_t               new_position; // Only valid during garbage collection
        slot_allocation_header allocation;
    };
    static_assert(sizeof(slot) == slot_size);

    class pointer_set {
        std::vector<gc_heap_ptr_untyped*> set_;
    public:
        bool empty() const { return set_.empty(); }
        uint32_t size() const { return static_cast<uint32_t>(set_.size()); }
        auto begin() { return set_.begin(); }
        auto end() { return set_.end(); }
        auto begin() const { return set_.cbegin(); }
        auto end() const { return set_.cend(); }

        gc_heap_ptr_untyped** data() { return set_.data(); }

        void insert(gc_heap_ptr_untyped& p) {
            // Note: garbage_collect() assumes nodes are added to the back
            // assert(std::find(begin(), end(), &p) == end()); // Other asserts should catch this
            set_.push_back(&p);
        }

        void erase(const gc_heap_ptr_untyped& p
#ifndef NDEBUG
            ,uint32_t ptr_keep_count
#endif
        ) {
            // Search from the back since objects tend to be short lived
            for (size_t i = set_.size(); i--;) {
                if (set_[i] == &p) {
                    set_.erase(set_.begin() + i);
                    assert(i >= ptr_keep_count);
                    return;
                }
            }
            assert(!"Pointer not found in set!");
        }
    };

    pointer_set pointers_;
    slot* storage_;
    uint32_t capacity_;
    uint32_t next_free_ = 0;

    // Only valid during GC
    struct gc_state {
        uint32_t ptr_keep_count = 0;            // active if <> 0
        gc_heap* new_heap;                      // the "new_heap" is only kept for allocation purposes, no references to it should be kept
        uint32_t level;                         // recursion depth
        std::vector<uint32_t*> pending_fixpus;
    } gc_state_;

    void run_destructors();

    void attach(gc_heap_ptr_untyped& p);
    void detach(gc_heap_ptr_untyped& p);

    bool is_internal(const void* p) const {
        return reinterpret_cast<uintptr_t>(p) >= reinterpret_cast<uintptr_t>(storage_) && reinterpret_cast<uintptr_t>(p) < reinterpret_cast<uintptr_t>(storage_ + capacity_);
    }

    // Allocate at least 'num_bytes' of storage, returns the offset (in slots) of the allocation (header) inside 'storage_'
    // The object must be constructed one slot beyond the allocation header and the type field of the allocation header updated
    uint32_t allocate(size_t num_bytes);

    uint32_t gc_move(uint32_t pos);
    void gc_move(gc_heap_ptr_untyped& p);
    void fixup(uint32_t& pos);

    template<typename T>
    gc_heap_ptr<T> unsafe_create_from_position(uint32_t pos);
};

class gc_heap_ptr_untyped {
public:
    friend gc_heap;
    friend value_representation;
    template<typename> friend class gc_heap_ptr_untracked;

    gc_heap_ptr_untyped() : heap_(nullptr), pos_(0) {
    }
    gc_heap_ptr_untyped(const gc_heap_ptr_untyped& p) : heap_(p.heap_), pos_(p.pos_) {
        if (heap_) {
            heap_->attach(*this);
        }
    }
    ~gc_heap_ptr_untyped() {
        if (heap_) {
            heap_->detach(*this);
        }
    }
    gc_heap_ptr_untyped& operator=(const gc_heap_ptr_untyped& p) {
        if (this != &p) {
            if (heap_) {
                heap_->detach(*this);
            }
            heap_ = p.heap_;
            pos_ = p.pos_;
            if (heap_) {
                heap_->attach(*this);
            }
        }
        return *this;
    }

    gc_heap& heap() const {
        return const_cast<gc_heap&>(*heap_);
    }

    explicit operator bool() const { return heap_; }

    void* get() const {
        assert(heap_);
        return const_cast<void*>(static_cast<const void*>(&heap_->storage_[pos_]));
    }

protected:
    explicit gc_heap_ptr_untyped(gc_heap& heap, uint32_t pos) : heap_(&heap), pos_(pos) {
        heap_->attach(*this);
    }

private:
    gc_heap* heap_;
    uint32_t pos_;
};

template<typename T>
class gc_heap_ptr : public gc_heap_ptr_untyped {
public:
    friend gc_heap;

    gc_heap_ptr() = default;
    gc_heap_ptr(std::nullptr_t) : gc_heap_ptr_untyped() {}
    template<typename U, typename = typename std::enable_if<std::is_convertible_v<U*, T*>>::type>
    gc_heap_ptr(const gc_heap_ptr<U>& p) : gc_heap_ptr_untyped(p) {}

    T* get() const { return static_cast<T*>(gc_heap_ptr_untyped::get()); }
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }

private:
    explicit gc_heap_ptr(gc_heap& heap, uint32_t pos) : gc_heap_ptr_untyped(heap, pos) {}
    explicit gc_heap_ptr(const gc_heap_ptr_untyped& p) : gc_heap_ptr_untyped(p) {}
};

template<typename T>
class gc_heap_ptr_untracked {
    // TODO: Add debug mode where e.g. the MSB of pos_ is set when the pointer is copied
    //       Then check that 1) it is set in fixup_after_move 2) NOT set in the destructor
    //       Should also do something similar for value_representation
public:
    gc_heap_ptr_untracked() : pos_(0) {}
    gc_heap_ptr_untracked(const gc_heap_ptr<T>& p) : pos_(p.pos_) {}
    gc_heap_ptr_untracked(const gc_heap_ptr_untracked&) = default;
    gc_heap_ptr_untracked& operator=(const gc_heap_ptr_untracked&) = default;

    explicit operator bool() const { return pos_; }

    T& dereference(gc_heap& h) const {
        assert(pos_ > 0 && pos_ < h.next_free_ && gc_type_info_registration<T>::get().is_convertible(h.storage_[pos_-1].allocation.type_info()));
        return *reinterpret_cast<T*>(&h.storage_[pos_]);
    }

    gc_heap_ptr<T> track(gc_heap& h) const {
        assert(pos_);
        return h.unsafe_create_from_position<T>(pos_);
    }

    void fixup_after_move(gc_heap& old_heap) {
        if (pos_) {
            old_heap.fixup(pos_);
        }
    }

protected:
    explicit gc_heap_ptr_untracked(uint32_t pos) : pos_(pos) {}

private:
    uint32_t pos_;
};

template<typename T, typename... Args>
gc_heap_ptr<T> gc_heap::allocate_and_construct(size_t num_bytes, Args&&... args) {
    const auto pos = allocate(num_bytes);
    auto& a = storage_[pos].allocation;
    assert(a.type == unallocated_type_index);
    gc_type_info_registration<T>::construct(&storage_[pos+1], std::forward<Args>(args)...);
    a.type = gc_type_info_registration<T>::get().get_index();
    return gc_heap_ptr<T>{*this, pos+1};
}

template<typename T>
gc_heap_ptr<T> gc_heap::unsafe_create_from_position(uint32_t pos) {
    assert(pos > 0 && pos < next_free_ && gc_type_info_registration<T>::get().is_convertible(storage_[pos-1].allocation.type_info()));
    return gc_heap_ptr<T>{*this, pos};
}

} // namespace mjs

#endif
