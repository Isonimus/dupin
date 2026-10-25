/**
 * @file ring_buffer.h
 * @brief Fixed-size, stack-allocated ring buffer
 * 
 * A generic, memory-efficient ring buffer for embedded use.
 * No dynamic allocation - size is fixed at compile time.
 * 
 * @tparam T Element type
 * @tparam Capacity Maximum number of elements
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace dupin {

/**
 * @brief Fixed-size ring buffer with FIFO semantics
 * 
 * When full, new elements overwrite the oldest.
 * Designed for memory-constrained embedded systems.
 * 
 * @tparam T Element type (should be trivially copyable for best performance)
 * @tparam Capacity Maximum number of elements (must be > 0)
 * 
 * @example
 *   RingBuffer<int, 10> buffer;
 *   buffer.push(42);
 *   int val = buffer.peek();
 */
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert(Capacity > 0, "RingBuffer capacity must be greater than 0");

public:
    /**
     * @brief Construct an empty ring buffer
     */
    RingBuffer() : m_head(0), m_tail(0), m_count(0) {}

    /**
     * @brief Push an element to the buffer
     * 
     * If the buffer is full, the oldest element is overwritten.
     * 
     * @param item Element to add
     * @return true if an element was overwritten, false otherwise
     */
    bool push(const T& item) {
        bool overwritten = false;
        
        if (m_count == Capacity) {
            // Buffer full - overwrite oldest
            m_tail = (m_tail + 1) % Capacity;
            overwritten = true;
        } else {
            m_count++;
        }
        
        m_buffer[m_head] = item;
        m_head = (m_head + 1) % Capacity;
        
        return overwritten;
    }

    /**
     * @brief Remove and return the oldest element
     * 
     * @param out Pointer to store the removed element
     * @return true if an element was removed, false if buffer was empty
     */
    bool pop(T* out) {
        if (m_count == 0) {
            return false;
        }
        
        if (out != nullptr) {
            *out = m_buffer[m_tail];
        }
        
        m_tail = (m_tail + 1) % Capacity;
        m_count--;
        
        return true;
    }

    /**
     * @brief Get the oldest element without removing it
     * 
     * @param out Pointer to store the element
     * @return true if buffer has elements, false if empty
     */
    bool peek(T* out) const {
        if (m_count == 0) {
            return false;
        }
        
        if (out != nullptr) {
            *out = m_buffer[m_tail];
        }
        
        return true;
    }

    /**
     * @brief Get element at index (0 = oldest)
     * 
     * @param index Index from oldest element
     * @param out Pointer to store the element
     * @return true if index is valid, false otherwise
     */
    bool at(size_t index, T* out) const {
        if (index >= m_count) {
            return false;
        }
        
        if (out != nullptr) {
            size_t actualIndex = (m_tail + index) % Capacity;
            *out = m_buffer[actualIndex];
        }
        
        return true;
    }

    /**
     * @brief Get the most recent element (newest)
     * 
     * @param out Pointer to store the element
     * @return true if buffer has elements, false if empty
     */
    bool peekNewest(T* out) const {
        if (m_count == 0) {
            return false;
        }
        
        if (out != nullptr) {
            // Head points to next write position, so newest is at head-1
            size_t newestIndex = (m_head + Capacity - 1) % Capacity;
            *out = m_buffer[newestIndex];
        }
        
        return true;
    }

    /**
     * @brief Clear all elements from the buffer
     */
    void clear() {
        m_head = 0;
        m_tail = 0;
        m_count = 0;
    }

    /**
     * @brief Get current number of elements
     */
    size_t size() const { return m_count; }

    /**
     * @brief Get maximum capacity
     */
    constexpr size_t capacity() const { return Capacity; }

    /**
     * @brief Check if buffer is empty
     */
    bool isEmpty() const { return m_count == 0; }

    /**
     * @brief Check if buffer is full
     */
    bool isFull() const { return m_count == Capacity; }

    /**
     * @brief Iterator for traversing buffer from oldest to newest
     */
    class Iterator {
    public:
        Iterator(const RingBuffer* buffer, size_t pos) 
            : m_buffer(buffer), m_pos(pos) {}

        const T& operator*() const {
            size_t actualIndex = (m_buffer->m_tail + m_pos) % Capacity;
            return m_buffer->m_buffer[actualIndex];
        }

        Iterator& operator++() {
            m_pos++;
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return m_pos != other.m_pos;
        }

    private:
        const RingBuffer* m_buffer;
        size_t m_pos;
    };

    Iterator begin() const { return Iterator(this, 0); }
    Iterator end() const { return Iterator(this, m_count); }

private:
    T m_buffer[Capacity];
    size_t m_head;  // Next write position
    size_t m_tail;  // Oldest element position
    size_t m_count; // Current number of elements
};

} // namespace dupin
