/**
 * @file test_ring_buffer.cpp
 * @brief Unit tests for RingBuffer template class
 */

#include <unity.h>
#include "storage/ring_buffer.h"

using namespace dupin;

void setUp(void) {}
void tearDown(void) {}

// ============================================
// Basic Operations
// ============================================

void test_ring_buffer_starts_empty(void) {
    RingBuffer<int, 10> buffer;
    
    TEST_ASSERT_TRUE(buffer.isEmpty());
    TEST_ASSERT_FALSE(buffer.isFull());
    TEST_ASSERT_EQUAL(0, buffer.size());
    TEST_ASSERT_EQUAL(10, buffer.capacity());
}

void test_ring_buffer_push_increases_size(void) {
    RingBuffer<int, 10> buffer;
    
    buffer.push(42);
    TEST_ASSERT_EQUAL(1, buffer.size());
    TEST_ASSERT_FALSE(buffer.isEmpty());
    
    buffer.push(43);
    TEST_ASSERT_EQUAL(2, buffer.size());
}

void test_ring_buffer_pop_decreases_size(void) {
    RingBuffer<int, 10> buffer;
    buffer.push(42);
    buffer.push(43);
    
    int value;
    bool result = buffer.pop(&value);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(42, value);
    TEST_ASSERT_EQUAL(1, buffer.size());
}

void test_ring_buffer_pop_empty_returns_false(void) {
    RingBuffer<int, 10> buffer;
    
    int value = 999;
    bool result = buffer.pop(&value);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(999, value);  // Unchanged
}

void test_ring_buffer_peek_does_not_remove(void) {
    RingBuffer<int, 10> buffer;
    buffer.push(42);
    
    int value;
    bool result = buffer.peek(&value);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(42, value);
    TEST_ASSERT_EQUAL(1, buffer.size());  // Still there
}

void test_ring_buffer_peek_empty_returns_false(void) {
    RingBuffer<int, 10> buffer;
    
    int value = 999;
    bool result = buffer.peek(&value);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(999, value);  // Unchanged
}

// ============================================
// FIFO Order
// ============================================

void test_ring_buffer_fifo_order(void) {
    RingBuffer<int, 10> buffer;
    
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    
    int value;
    buffer.pop(&value);
    TEST_ASSERT_EQUAL(1, value);
    
    buffer.pop(&value);
    TEST_ASSERT_EQUAL(2, value);
    
    buffer.pop(&value);
    TEST_ASSERT_EQUAL(3, value);
}

// ============================================
// Overflow Behavior
// ============================================

void test_ring_buffer_overflow_overwrites_oldest(void) {
    RingBuffer<int, 3> buffer;
    
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    TEST_ASSERT_TRUE(buffer.isFull());
    
    // This should overwrite '1'
    bool overwritten = buffer.push(4);
    TEST_ASSERT_TRUE(overwritten);
    TEST_ASSERT_EQUAL(3, buffer.size());  // Still 3
    
    int value;
    buffer.pop(&value);
    TEST_ASSERT_EQUAL(2, value);  // '1' was overwritten
    
    buffer.pop(&value);
    TEST_ASSERT_EQUAL(3, value);
    
    buffer.pop(&value);
    TEST_ASSERT_EQUAL(4, value);
}

void test_ring_buffer_push_returns_overwrite_status(void) {
    RingBuffer<int, 2> buffer;
    
    bool overwritten1 = buffer.push(1);
    bool overwritten2 = buffer.push(2);
    bool overwritten3 = buffer.push(3);  // Overwrites '1'
    
    TEST_ASSERT_FALSE(overwritten1);
    TEST_ASSERT_FALSE(overwritten2);
    TEST_ASSERT_TRUE(overwritten3);
}

// ============================================
// Indexed Access
// ============================================

void test_ring_buffer_at_returns_by_index(void) {
    RingBuffer<int, 10> buffer;
    buffer.push(10);
    buffer.push(20);
    buffer.push(30);
    
    int value;
    
    buffer.at(0, &value);
    TEST_ASSERT_EQUAL(10, value);  // Oldest
    
    buffer.at(1, &value);
    TEST_ASSERT_EQUAL(20, value);
    
    buffer.at(2, &value);
    TEST_ASSERT_EQUAL(30, value);  // Newest
}

void test_ring_buffer_at_invalid_index_returns_false(void) {
    RingBuffer<int, 10> buffer;
    buffer.push(42);
    
    int value = 999;
    bool result = buffer.at(5, &value);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL(999, value);  // Unchanged
}

void test_ring_buffer_peek_newest(void) {
    RingBuffer<int, 10> buffer;
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    
    int value;
    bool result = buffer.peekNewest(&value);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(3, value);
}

// ============================================
// Clear
// ============================================

void test_ring_buffer_clear_empties_buffer(void) {
    RingBuffer<int, 10> buffer;
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    
    buffer.clear();
    
    TEST_ASSERT_TRUE(buffer.isEmpty());
    TEST_ASSERT_EQUAL(0, buffer.size());
}

// ============================================
// Iterator
// ============================================

void test_ring_buffer_iterator_traverses_in_order(void) {
    RingBuffer<int, 10> buffer;
    buffer.push(10);
    buffer.push(20);
    buffer.push(30);
    
    int expected[] = {10, 20, 30};
    int i = 0;
    
    for (const int& val : buffer) {
        TEST_ASSERT_EQUAL(expected[i], val);
        i++;
    }
    
    TEST_ASSERT_EQUAL(3, i);
}

void test_ring_buffer_iterator_after_wrap(void) {
    RingBuffer<int, 3> buffer;
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    buffer.push(4);  // Wraps, overwrites 1
    buffer.push(5);  // Wraps, overwrites 2
    
    int expected[] = {3, 4, 5};
    int i = 0;
    
    for (const int& val : buffer) {
        TEST_ASSERT_EQUAL(expected[i], val);
        i++;
    }
    
    TEST_ASSERT_EQUAL(3, i);
}

// ============================================
// Edge Cases
// ============================================

void test_ring_buffer_capacity_one(void) {
    RingBuffer<int, 1> buffer;
    
    buffer.push(42);
    TEST_ASSERT_TRUE(buffer.isFull());
    TEST_ASSERT_EQUAL(1, buffer.size());
    
    buffer.push(99);  // Overwrites 42
    TEST_ASSERT_EQUAL(1, buffer.size());
    
    int value;
    buffer.pop(&value);
    TEST_ASSERT_EQUAL(99, value);
}

void test_ring_buffer_with_struct(void) {
    struct TestData {
        int id;
        float value;
    };
    
    RingBuffer<TestData, 5> buffer;
    
    buffer.push({1, 1.5f});
    buffer.push({2, 2.5f});
    
    TestData out;
    buffer.pop(&out);
    
    TEST_ASSERT_EQUAL(1, out.id);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, out.value);
}

// ============================================
// Test Runner
// ============================================

#ifdef NATIVE_BUILD
int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    // Basic operations
    RUN_TEST(test_ring_buffer_starts_empty);
    RUN_TEST(test_ring_buffer_push_increases_size);
    RUN_TEST(test_ring_buffer_pop_decreases_size);
    RUN_TEST(test_ring_buffer_pop_empty_returns_false);
    RUN_TEST(test_ring_buffer_peek_does_not_remove);
    RUN_TEST(test_ring_buffer_peek_empty_returns_false);
    
    // FIFO order
    RUN_TEST(test_ring_buffer_fifo_order);
    
    // Overflow
    RUN_TEST(test_ring_buffer_overflow_overwrites_oldest);
    RUN_TEST(test_ring_buffer_push_returns_overwrite_status);
    
    // Indexed access
    RUN_TEST(test_ring_buffer_at_returns_by_index);
    RUN_TEST(test_ring_buffer_at_invalid_index_returns_false);
    RUN_TEST(test_ring_buffer_peek_newest);
    
    // Clear
    RUN_TEST(test_ring_buffer_clear_empties_buffer);
    
    // Iterator
    RUN_TEST(test_ring_buffer_iterator_traverses_in_order);
    RUN_TEST(test_ring_buffer_iterator_after_wrap);
    
    // Edge cases
    RUN_TEST(test_ring_buffer_capacity_one);
    RUN_TEST(test_ring_buffer_with_struct);
    
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    
    RUN_TEST(test_ring_buffer_starts_empty);
    RUN_TEST(test_ring_buffer_push_increases_size);
    RUN_TEST(test_ring_buffer_pop_decreases_size);
    RUN_TEST(test_ring_buffer_pop_empty_returns_false);
    RUN_TEST(test_ring_buffer_peek_does_not_remove);
    RUN_TEST(test_ring_buffer_peek_empty_returns_false);
    RUN_TEST(test_ring_buffer_fifo_order);
    RUN_TEST(test_ring_buffer_overflow_overwrites_oldest);
    RUN_TEST(test_ring_buffer_push_returns_overwrite_status);
    RUN_TEST(test_ring_buffer_at_returns_by_index);
    RUN_TEST(test_ring_buffer_at_invalid_index_returns_false);
    RUN_TEST(test_ring_buffer_peek_newest);
    RUN_TEST(test_ring_buffer_clear_empties_buffer);
    RUN_TEST(test_ring_buffer_iterator_traverses_in_order);
    RUN_TEST(test_ring_buffer_iterator_after_wrap);
    RUN_TEST(test_ring_buffer_capacity_one);
    RUN_TEST(test_ring_buffer_with_struct);
    
    UNITY_END();
}
void loop() {}
#endif
