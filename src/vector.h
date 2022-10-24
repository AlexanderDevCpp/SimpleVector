#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>
#include <iterator>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory(RawMemory&& other) noexcept {
        buffer_ = std::move(other.buffer_);
        capacity_ = std::move(other.capacity_);
    }
    RawMemory& operator=(RawMemory&& rhs) noexcept {
        std::swap(capacity_, rhs.capacity_);
        std::swap(buffer_, rhs.buffer_);
        return *this;
    }

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }
    
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)  //
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    explicit Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        data_ = std::move(other.data_);
        size_ = std::move(other.size_);
        other.size_ = 0;
    }

    iterator begin() noexcept {
        return iterator{ data_.GetAddress() };
    }
    iterator end() noexcept {
        return iterator{ data_.GetAddress()+size_ };
    }
    const_iterator begin() const noexcept {
        return const_iterator{ data_.GetAddress() };
    }
    const_iterator end() const noexcept {
        return const_iterator{ data_.GetAddress() + size_ };
    }
    const_iterator cbegin() const noexcept {
        return const_iterator{ data_.GetAddress() };
    }
    const_iterator cend() const noexcept {
        return const_iterator{ data_.GetAddress() + size_ };
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        size_t offset = (pos - begin());

        if (size_ < data_.Capacity()) {
            if (pos == end()) {
                new (data_ + offset) T(std::forward<Args>(args)...);
            }
            else {
                auto temp_element = T(std::forward<Args>(args)...);

                new (data_ + size_) T(std::forward<T>(data_[size_ - 1]));
                std::move_backward((begin() + offset), end() - 1, &data_[size_]);

                data_[offset] = std::forward<T>(temp_element);
            }
        }
        else {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);

            new (new_data + offset) T(std::forward<Args>(args)...);

            try {
                CopyData(begin(), offset, new_data.GetAddress());
            }
            catch (...) {
                std::destroy_at(new_data + offset);
                throw;
            }

            try {
                CopyData((begin() + offset), (size_ - offset), (new_data.GetAddress() + offset + 1));
            }
            catch (...) {
                std::destroy_n(new_data.GetAddress(), offset + 1);
                throw;
            }

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);

        }

        size_++;
        return &data_[offset];
    }

    iterator Erase(const_iterator pos) {
        pos->~T();

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::move(&data_[(pos - begin() + 1)], end(), &data_[(pos - begin())]);
        }
        else {
            std::copy((pos + 1), cend(), &data_[(pos - begin())]);
        }

        size_--;
        return &data_[(pos - begin())];
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    void Resize(size_t new_size) {
        if (new_size != size_) {
            if (new_size < size_) {
                std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            }
            else {
                Reserve(new_size);
                std::uninitialized_value_construct_n(data_.GetAddress() + size_, (new_size - size_));
            }
            size_ = new_size;
        }
    }

    void PushBack(const T& value) {
        EmplaceBack(std::move(value));
    }

    template <typename S>
    void PushBack(S&& value) {
        EmplaceBack(std::forward<S>(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args&&>(args)...);
    }

    void PopBack() noexcept {
        std::destroy_at(data_.GetAddress() + size_ - 1);
        size_--;
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
            else {
                std::copy_n(rhs.data_.GetAddress(), std::min(rhs.size_,size_), data_.GetAddress());
                if (rhs.size_ < size_) {
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                }
                else {
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress());
                }

                this->size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        this->data_ = std::move(rhs.data_);
        this->size_ = std::move(rhs.size_);
        rhs.size_ = 0;
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        CopyData(data_.GetAddress(), size_, new_data.GetAddress());

        std::destroy_n(data_.GetAddress(), size_);

        data_.Swap(new_data);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    void CopyData(iterator from, size_t count, iterator to) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(from, count, to);
        }
        else {
            std::uninitialized_copy_n(from, count, to);
        }
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};
