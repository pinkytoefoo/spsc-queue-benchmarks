#pragma once

#include <atomic>
#include <new>
#include <memory>
#include <utility>
#include <bit>
#include <optional>
#include <cassert>
#include <type_traits>
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <iostream>
#include <thread>
#include <iomanip>

#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
#   define NO_UNIQUE_ADDRESS [[no_unique_address]]
#elif _MSC_VER >= 1929
#   define NO_UNIQUE_ADDRESS [[msvc::no_unique_address]] // https://devblogs.microsoft.com/cppblog/msvc-cpp20-and-the-std-cpp20-switch/
#else
#   define NO_UNIQUE_ADDRESS
#endif

#if defined(__has_cpp_attribute) && __has_cpp_attribute(nodiscard)
#   define NODISCARD [[nodiscard]]
#else
#   define NODISCARD
#endif

namespace hft
{
/**
 * @brief Bounded single-producer, single-consumer queue.
 *
 * Thread-safety contract:
 * - Exactly one thread may call producer operations.
 * - Exactly one thread may call consumer operations.
 * - Producer and consumer operations may execute concurrently.
 * - Destruction requires both threads to have stopped accessing the queue.
 * 
 * @note Due to manual heap allocations, this queue cannot be fully constexpr
 * 
 * @note The API is aware of T's exception guarentees. However, it does not account
 *       for the allocator's exception guarentees. For example, if the allocator throws
 *       a `std::bad_alloc{}`, the queue does not account for it and therefore `std::terminate()` would be called
 */
template<typename T, typename Allocator = std::allocator<T>>
class spsc_queue
{
    using alloc_traits = std::allocator_traits<Allocator>;
    static_assert(
        std::same_as<typename alloc_traits::value_type, T>,
        "Allocator::value_type must match T"
    );

public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;

public:
    explicit spsc_queue(size_t capacity, const Allocator& allocator = Allocator())
        : capacity_{ capacity }
        , allocator_{ allocator }
    {
        if(capacity_ == 0) {
            capacity_ = 1;
        }
        if(capacity_ >= alloc_traits::max_size(allocator_)) {
            throw std::length_error("Capacity too large");
        }
        ++capacity_;  // slack slot

        buffer_ = alloc_traits::allocate(allocator_, capacity_);
    }

    ~spsc_queue() noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            auto read = readIdx_.load(std::memory_order_relaxed);
            const auto write = writeIdx_.load(std::memory_order_relaxed);

            while (read != write) {
                std::destroy_at(buffer_ + read);

                if (++read == capacity_)
                    read = 0;
            }
        }

        alloc_traits::deallocate(allocator_, buffer_, capacity_);
    }

    // non-copyable and non-movable
    spsc_queue(const spsc_queue&) = delete;
    void operator=(const spsc_queue&) = delete;

    // producer API calls
    template<typename ...Args>
    requires std::constructible_from<T, Args&&...>
    bool try_emplace(Args&&... args)
    {
        auto const currentWrite = writeIdx_.load(std::memory_order_relaxed);
        auto nextWrite = currentWrite + 1;

        if(nextWrite == capacity_) {
            nextWrite = 0;
        }
        
        if(nextWrite == readCacheIdx_) {
            readCacheIdx_ = readIdx_.load(std::memory_order_acquire);

            if(nextWrite == readCacheIdx_) {
                return false;
            }
        }

        alloc_traits::construct(
            allocator_,
            buffer_ + currentWrite,
            std::forward<Args>(args)...
        );

        writeIdx_.store(nextWrite, std::memory_order_release);

        return true;
    }

    template<typename ...Args>
    requires std::constructible_from<T, Args&&...>
    void emplace(Args&&... args)
    {
        auto const currentWrite = writeIdx_.load(std::memory_order_relaxed);
        auto nextWrite = currentWrite + 1;
        if(nextWrite == capacity_) {
            nextWrite = 0;
        }

        while(nextWrite == readCacheIdx_) {
            readCacheIdx_ = readIdx_.load(std::memory_order_acquire);
        }

        alloc_traits::construct(
            allocator_,
            buffer_ + currentWrite,
            std::forward<Args>(args)...
        );

        writeIdx_.store(nextWrite, std::memory_order_release);
    }

    void push(const T& value) {
        emplace(value);
    }

    void push(T&& value) {
        emplace(std::move(value));
    }

    bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }

    bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return try_emplace(std::move(value));
    }

    // consumer API calls
    /**
     * @brief Returns a pointer to the next element in the queue without removing it.
     *
     * @return Pointer to the front element, or nullptr if the queue is empty.
     *
     * @note Consumer-thread only. Must not be called concurrently by multiple
     *       consumers or by the producer.
     *
     * @note The returned pointer remains valid until the consumer removes
     *       the element using try_pop() or try_pop_get().
     */
    pointer front() noexcept {
        auto const currentRead = readIdx_.load(std::memory_order_relaxed);
        
        if(currentRead == writeCacheIdx_) {
            writeCacheIdx_ = writeIdx_.load(std::memory_order_acquire);

            if(currentRead == writeCacheIdx_) {
                return nullptr;
            }
        }

        return buffer_ + currentRead;
    }

    // const_pointer front() const noexcept {
    //     auto const currentRead = readIdx_.load(std::memory_order_relaxed);
        
    //     if(currentRead == writeIdx_.load(std::memory_order_acquire)) {
    //         return nullptr;
    //     }

    //     return buffer_ + currentRead;
    // }

    /**
     * @brief Returns a boolean representing whether or not popping the front element was successful
     *
     * @return Boolean representing whether or not the front element was popped
     *
     * @note Consumer-thread only. Must not be called concurrently by multiple
     *       consumers or by the producer.
     */
    bool try_pop() noexcept(std::is_nothrow_destructible_v<T>) {
        auto const currentRead = readIdx_.load(std::memory_order_relaxed);
        
        if(currentRead == writeCacheIdx_) {
            writeCacheIdx_ = writeIdx_.load(std::memory_order_acquire);
            
            if(currentRead == writeCacheIdx_) {
                return false;
            }
        }

        alloc_traits::destroy(allocator_, buffer_ + currentRead);

        auto nextRead = currentRead + 1;
        if(nextRead == capacity_) {
            nextRead = 0;
        }

        readIdx_.store(nextRead, std::memory_order_release);
        return true;
    }

    bool try_pop(T& out) noexcept(std::is_nothrow_destructible_v<T>)
    {
        auto const currentRead = readIdx_.load(std::memory_order_relaxed);
        
        if(currentRead == writeCacheIdx_) {
            writeCacheIdx_ = writeIdx_.load(std::memory_order_acquire);
            
            if(currentRead == writeCacheIdx_) {
                return false;
            }
        }

        out = buffer_[currentRead];
        alloc_traits::destroy(allocator_, buffer_ + currentRead);

        auto nextRead = currentRead + 1;
        if(nextRead == capacity_) {
            nextRead = 0;
        }

        readIdx_.store(nextRead, std::memory_order_release);
        return true;
    }


    void pop() noexcept(std::is_nothrow_destructible_v<T>) {
        auto const currentRead = readIdx_.load(std::memory_order_relaxed);
        
        while(currentRead == writeCacheIdx_) {
            writeCacheIdx_ = writeIdx_.load(std::memory_order_acquire);
        }

        alloc_traits::destroy(allocator_, buffer_ + currentRead);

        auto nextRead = currentRead + 1;
        if(nextRead == capacity_) {
            nextRead = 0;
        }

        readIdx_.store(nextRead, std::memory_order_release);
    }

    NODISCARD std::optional<T> try_pop_get()
        noexcept(
            std::is_nothrow_move_constructible_v<T> &&
            std::is_nothrow_destructible_v<T>
        )
    {
        auto const currentRead = readIdx_.load(std::memory_order_relaxed);
        
        if(currentRead == writeCacheIdx_) {
            writeCacheIdx_ = writeIdx_.load(std::memory_order_acquire);
            
            if(currentRead == writeCacheIdx_) {
                return std::nullopt;
            }
        }

        std::optional<T> popped{ std::in_place, std::move(buffer_[currentRead]) };
        alloc_traits::destroy(allocator_, buffer_ + currentRead);

        auto nextRead = currentRead + 1;
        if(nextRead == capacity_) {
            nextRead = 0;
        }
        readIdx_.store(nextRead, std::memory_order_release);

        return popped;
    }

    NODISCARD size_type size() const noexcept {
        auto const write = writeIdx_.load(std::memory_order_acquire);
        auto const read = readIdx_.load(std::memory_order_acquire);

        return (write > read) ? write - read : capacity_ - write + read;
    }

    NODISCARD bool empty() const noexcept { return writeIdx_.load(std::memory_order_acquire) == readIdx_.load(std::memory_order_acquire); }
    NODISCARD constexpr size_type capacity() const noexcept { return capacity_ - 1; }

private:
    size_type capacity_;
    Allocator allocator_ NO_UNIQUE_ADDRESS;
    pointer buffer_;

#ifdef __cpp_lib_hardware_interference_size
    constexpr static size_type kCachelineSize = std::hardware_destructive_interference_size;
#else
    constexpr static size_type kCachelineSize = 64;
#endif

    alignas(kCachelineSize) std::atomic<size_type> writeIdx_{ 0 };
    size_type readCacheIdx_{ 0 };

    alignas(kCachelineSize) std::atomic<size_type> readIdx_{ 0 };
    size_type writeCacheIdx_{ 0 };
};
} // namespace hft
