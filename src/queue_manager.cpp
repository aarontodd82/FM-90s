#include "queue_manager.h"

QueueManager::QueueManager()
    : eventManager_(nullptr)
{
    queue_.reserve(20);  // Pre-allocate for typical queue size
}

// ============================================
// QUEUE OPERATIONS
// ============================================

void QueueManager::addToQueue(const char* filePath) {
    if (!filePath || strlen(filePath) == 0) {
        Serial.println("[QueueManager] Error: Empty file path");
        return;
    }

    queue_.push_back(String(filePath));

    Serial.printf("[QueueManager] Added to queue: %s (position %d)\n",
                  filePath, queue_.size() - 1);

    fireEvent(EventManager::EVENT_QUEUE_TRACK_ADDED);
    fireEvent(EventManager::EVENT_QUEUE_CHANGED);
}

void QueueManager::insertNext(const char* filePath) {
    if (!filePath || strlen(filePath) == 0) {
        Serial.println("[QueueManager] Error: Empty file path");
        return;
    }

    if (queue_.empty()) {
        // Queue empty, just add normally
        addToQueue(filePath);
        return;
    }

    // Insert at position 1 (after current track)
    queue_.insert(queue_.begin() + 1, String(filePath));

    Serial.printf("[QueueManager] Inserted next: %s (position 1)\n", filePath);

    fireEvent(EventManager::EVENT_QUEUE_TRACK_ADDED);
    fireEvent(EventManager::EVENT_QUEUE_CHANGED);
}

void QueueManager::clear() {
    int oldSize = queue_.size();
    queue_.clear();

    Serial.printf("[QueueManager] Queue cleared (%d tracks removed)\n", oldSize);

    if (oldSize > 0) {
        fireEvent(EventManager::EVENT_QUEUE_CLEARED);
        fireEvent(EventManager::EVENT_QUEUE_CHANGED);
    }
}

bool QueueManager::removeAt(int index) {
    if (index < 0 || index >= (int)queue_.size()) {
        Serial.printf("[QueueManager] Error: Invalid index %d (queue size: %d)\n",
                     index, queue_.size());
        return false;
    }

    String removed = queue_[index];
    queue_.erase(queue_.begin() + index);

    Serial.printf("[QueueManager] Removed track at index %d: %s\n",
                  index, removed.c_str());

    fireEventInt(EventManager::EVENT_QUEUE_TRACK_REMOVED, index);
    fireEvent(EventManager::EVENT_QUEUE_CHANGED);
    return true;
}


// ============================================
// NAVIGATION
// ============================================

const char* QueueManager::playNext(const char* currentTrack) {
    if (queue_.empty()) {
        Serial.println("[QueueManager] No next track in queue");
        return nullptr;
    }

    // Get next track from queue[0]
    String nextTrack = queue_[0];

    // Remove it from queue
    queue_.erase(queue_.begin());

    Serial.printf("[QueueManager] Advanced to next track: %s (queue size: %d)\n",
                  nextTrack.c_str(), queue_.size());

    // Note: currentTrack will be added to history by coordinator when playback starts
    (void)currentTrack;  // Unused in this version

    fireEventInt(EventManager::EVENT_QUEUE_TRACK_CHANGED, 0);
    fireEvent(EventManager::EVENT_QUEUE_CHANGED);

    // Return pointer to internal string (valid until next queue operation)
    static String returnPath;
    returnPath = nextTrack;
    return returnPath.c_str();
}


const char* QueueManager::getCurrentTrack() {
    if (queue_.empty()) {
        return nullptr;
    }
    return queue_[0].c_str();
}

const char* QueueManager::getNextTrack() {
    if (queue_.size() < 2) {
        return nullptr;
    }
    return queue_[1].c_str();
}

const char* QueueManager::getTrackAt(int index) {
    if (index < 0 || index >= (int)queue_.size()) {
        return nullptr;
    }
    return queue_[index].c_str();
}

// ============================================
// QUEUE INFO
// ============================================

int QueueManager::getQueueSize() const {
    return queue_.size();
}

int QueueManager::getCurrentIndex() const {
    return queue_.empty() ? -1 : 0;  // Always 0 for simple queue
}

bool QueueManager::isEmpty() const {
    return queue_.empty();
}

bool QueueManager::hasNext() const {
    return !queue_.empty();  // Queue has upcoming tracks
}

// ============================================
// INTEGRATION
// ============================================

void QueueManager::setEventManager(EventManager* em) {
    eventManager_ = em;
    Serial.println("[QueueManager] EventManager connected");
}

// ============================================
// PRIVATE HELPERS
// ============================================

void QueueManager::fireEvent(EventManager::EventType type) {
    if (eventManager_) {
        eventManager_->fire(type);
    }
}

void QueueManager::fireEventInt(EventManager::EventType type, int value) {
    if (eventManager_) {
        eventManager_->fireInt(type, value);
    }
}
