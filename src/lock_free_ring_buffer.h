/**
 * Lock-Free Ring Buffer for ISR-safe audio processing
 *
 * Simple single-producer, single-consumer lock-free ring buffer.
 * Uses atomic index operations for thread-safety between ISR and main loop.
 *
 * Writer (main loop) produces data, Reader (audio ISR) consumes data.
 */

#ifndef LOCK_FREE_RING_BUFFER_H
#define LOCK_FREE_RING_BUFFER_H

#include <Arduino.h>

template<typename T, size_t SIZE>
class LockFreeRingBuffer {
public:
    LockFreeRingBuffer() : readIndex_(0), writeIndex_(0) {
        static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
    }

    // === ISR-SAFE METHODS (called from audio update()) ===

    // Check how many elements are available to read (ISR-safe)
    size_t available() const {
        return (writeIndex_ - readIndex_) & MASK;
    }

    // Read single element (ISR-safe)
    // Returns true if element was read, false if buffer empty
    bool read(T& element) {
        uint32_t currentRead = readIndex_;
        uint32_t currentWrite = writeIndex_;

        if (currentRead == currentWrite) {
            return false;  // Buffer empty
        }

        element = buffer_[currentRead & MASK];
        readIndex_ = (currentRead + 1) & MASK;
        return true;
    }

    // Read multiple elements (ISR-safe)
    // Returns number of elements actually read
    size_t read(T* elements, size_t count) {
        size_t avail = available();
        size_t toRead = (count < avail) ? count : avail;

        for (size_t i = 0; i < toRead; i++) {
            elements[i] = buffer_[readIndex_ & MASK];
            readIndex_ = (readIndex_ + 1) & MASK;
        }

        return toRead;
    }

    // Peek at next element without consuming (ISR-safe)
    bool peek(T& element) const {
        uint32_t currentRead = readIndex_;
        uint32_t currentWrite = writeIndex_;

        if (currentRead == currentWrite) {
            return false;
        }

        element = buffer_[currentRead & MASK];
        return true;
    }

    // === MAIN LOOP METHODS (called from refill functions) ===

    // Check how much space is available for writing
    size_t space() const {
        return SIZE - available() - 1;  // Reserve 1 slot to distinguish full/empty
    }

    // Write single element
    // Returns true if element was written, false if buffer full
    bool write(const T& element) {
        uint32_t currentWrite = writeIndex_;
        uint32_t currentRead = readIndex_;
        uint32_t nextWrite = (currentWrite + 1) & MASK;

        if (nextWrite == (currentRead & MASK)) {
            return false;  // Buffer full
        }

        buffer_[currentWrite & MASK] = element;
        writeIndex_ = nextWrite;
        return true;
    }

    // Write multiple elements
    // Returns number of elements actually written
    size_t write(const T* elements, size_t count) {
        size_t avail = space();
        size_t toWrite = (count < avail) ? count : avail;

        for (size_t i = 0; i < toWrite; i++) {
            buffer_[writeIndex_ & MASK] = elements[i];
            writeIndex_ = (writeIndex_ + 1) & MASK;
        }

        return toWrite;
    }

    // Clear buffer (not ISR-safe, call only when audio stopped)
    void clear() {
        readIndex_ = 0;
        writeIndex_ = 0;
    }

    // Check if buffer is empty
    bool isEmpty() const {
        return readIndex_ == writeIndex_;
    }

    // Check if buffer is full
    bool isFull() const {
        return ((writeIndex_ + 1) & MASK) == (readIndex_ & MASK);
    }

private:
    static const size_t MASK = SIZE - 1;

    T buffer_[SIZE];
    volatile uint32_t readIndex_;   // Modified by reader (ISR)
    volatile uint32_t writeIndex_;  // Modified by writer (main loop)
};

#endif // LOCK_FREE_RING_BUFFER_H
