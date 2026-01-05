#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>

template <typename T, size_t N>
class SmallVector {
 public:
  using value_type = T;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;

  SmallVector() noexcept : size_(0), capacity_(N), heap_data_(nullptr) {}

  explicit SmallVector(size_type count) : size_(0), capacity_(N), heap_data_(nullptr) {
    resize(count);
  }

  SmallVector(size_type count, const T& value) : size_(0), capacity_(N), heap_data_(nullptr) {
    resize(count, value);
  }

  SmallVector(std::initializer_list<T> init) : size_(0), capacity_(N), heap_data_(nullptr) {
    reserve(init.size());
    for (const auto& elem : init) {
      push_back(elem);
    }
  }

  SmallVector(const SmallVector& other) : size_(0), capacity_(N), heap_data_(nullptr) {
    reserve(other.size_);
    for (const auto& elem : other) {
      push_back(elem);
    }
  }

  SmallVector(SmallVector&& other) noexcept : size_(0), capacity_(N), heap_data_(nullptr) {
    if (other.is_using_heap()) {
      heap_data_ = other.heap_data_;
      capacity_ = other.capacity_;
      size_ = other.size_;
      other.heap_data_ = nullptr;
      other.size_ = 0;
      other.capacity_ = N;
    } else {
      T* this_data = stack_ptr();
      T* other_data = other.stack_ptr();
      for (size_t i = 0; i < other.size_; ++i) {
        ::new (static_cast<void*>(&this_data[i])) T(std::move(other_data[i]));
        other_data[i].~T();
      }
      size_ = other.size_;
      other.size_ = 0;
    }
  }

  ~SmallVector() { clear_and_deallocate(); }

  SmallVector& operator=(const SmallVector& other) {
    if (this != &other) {
      clear();
      reserve(other.size_);
      for (const auto& elem : other) {
        push_back(elem);
      }
    }
    return *this;
  }

  SmallVector& operator=(SmallVector&& other) noexcept {
    if (this != &other) {
      clear_and_deallocate();
      if (other.is_using_heap()) {
        heap_data_ = other.heap_data_;
        capacity_ = other.capacity_;
        size_ = other.size_;
        other.heap_data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = N;
      } else {
        T* this_data = stack_ptr();
        T* other_data = other.stack_ptr();
        for (size_t i = 0; i < other.size_; ++i) {
          ::new (static_cast<void*>(&this_data[i])) T(std::move(other_data[i]));
          other_data[i].~T();
        }
        size_ = other.size_;
        other.size_ = 0;
      }
    }
    return *this;
  }

  reference operator[](size_type pos) noexcept { return data()[pos]; }
  const_reference operator[](size_type pos) const noexcept { return data()[pos]; }

  reference at(size_type pos) {
    if (pos >= size_) throw std::out_of_range("SmallVector::at");
    return data()[pos];
  }

  const_reference at(size_type pos) const {
    if (pos >= size_) throw std::out_of_range("SmallVector::at");
    return data()[pos];
  }

  reference front() noexcept { return data()[0]; }
  const_reference front() const noexcept { return data()[0]; }

  reference back() noexcept { return data()[size_ - 1]; }
  const_reference back() const noexcept { return data()[size_ - 1]; }

  pointer data() noexcept { return is_using_heap() ? heap_data_ : stack_ptr(); }
  const_pointer data() const noexcept { return is_using_heap() ? heap_data_ : stack_ptr(); }

  iterator begin() noexcept { return data(); }
  const_iterator begin() const noexcept { return data(); }
  const_iterator cbegin() const noexcept { return data(); }

  iterator end() noexcept { return data() + size_; }
  const_iterator end() const noexcept { return data() + size_; }
  const_iterator cend() const noexcept { return data() + size_; }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
  [[nodiscard]] static constexpr size_type inline_capacity() noexcept { return N; }

  void reserve(size_type new_cap) {
    if (new_cap <= capacity_) return;

    T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));

    T* old_data = data();
    for (size_t i = 0; i < size_; ++i) {
      ::new (static_cast<void*>(&new_data[i])) T(std::move(old_data[i]));
      old_data[i].~T();
    }

    // Free old heap buffer if any
    if (is_using_heap()) {
      ::operator delete(heap_data_);
    }

    heap_data_ = new_data;
    capacity_ = new_cap;
  }

  void shrink_to_fit() {
    if (is_using_heap() && size_ <= N) {
      T* old_heap = heap_data_;
      T* stack_data = stack_ptr();

      for (size_t i = 0; i < size_; ++i) {
        ::new (static_cast<void*>(&stack_data[i])) T(std::move(old_heap[i]));
        old_heap[i].~T();
      }

      ::operator delete(old_heap);
      heap_data_ = nullptr;
      capacity_ = N;
    }
  }

  void clear() noexcept {
    T* ptr = data();
    for (size_t i = 0; i < size_; ++i) {
      ptr[i].~T();
    }
    size_ = 0;
  }

  void push_back(const T& value) {
    if (size_ >= capacity_) {
      reserve(capacity_ * 2);
    }
    ::new (static_cast<void*>(&data()[size_])) T(value);
    ++size_;
  }

  void push_back(T&& value) {
    if (size_ >= capacity_) {
      reserve(capacity_ * 2);
    }
    ::new (static_cast<void*>(&data()[size_])) T(std::move(value));
    ++size_;
  }

  template <typename... Args>
  reference emplace_back(Args&&... args) {
    if (size_ >= capacity_) {
      reserve(capacity_ * 2);
    }
    ::new (static_cast<void*>(&data()[size_])) T(std::forward<Args>(args)...);
    return data()[size_++];
  }

  void pop_back() noexcept {
    if (size_ > 0) {
      data()[--size_].~T();
    }
  }

  void resize(size_type count) {
    if (count > size_) {
      reserve(count);
      for (size_t i = size_; i < count; ++i) {
        ::new (static_cast<void*>(&data()[i])) T();
      }
    } else {
      for (size_t i = count; i < size_; ++i) {
        data()[i].~T();
      }
    }
    size_ = count;
  }

  void resize(size_type count, const T& value) {
    if (count > size_) {
      reserve(count);
      for (size_t i = size_; i < count; ++i) {
        ::new (static_cast<void*>(&data()[i])) T(value);
      }
    } else {
      for (size_t i = count; i < size_; ++i) {
        data()[i].~T();
      }
    }
    size_ = count;
  }

 private:
  [[nodiscard]] bool is_using_heap() const noexcept { return heap_data_ != nullptr; }

  T* stack_ptr() noexcept { return std::launder(reinterpret_cast<T*>(stack_data_.data())); }
  const T* stack_ptr() const noexcept {
    return std::launder(reinterpret_cast<const T*>(stack_data_.data()));
  }

  void clear_and_deallocate() {
    clear();
    if (is_using_heap()) {
      ::operator delete(heap_data_);
      heap_data_ = nullptr;
      capacity_ = N;
    }
  }

  alignas(T) std::array<std::byte, N * sizeof(T)> stack_data_;

  size_type size_;
  size_type capacity_;
  T* heap_data_;
};
