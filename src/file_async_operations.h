#ifndef FILE_ASYNC_OPERATIONS_H
#define FILE_ASYNC_OPERATIONS_H

#include "ui/framework/async_operation.h"
#include <SD.h>

/**
 * FileLoadOperation - Async wrapper for file loading
 *
 * Usage:
 *   FileLoadOperation* load = new FileLoadOperation("/path/to/file.vgm");
 *   load->start();
 *
 *   void loop() {
 *       load->update();
 *       if (load->isDone()) {
 *           if (load->isSuccess()) {
 *               File f = load->getFile();
 *               // ... use file ...
 *               f.close();
 *           }
 *           delete load;
 *       }
 *   }
 */
class FileLoadOperation : public AsyncOperation {
public:
    /**
     * Create a file load operation
     * @param path - Full path to file
     * @param timeoutMs - Load timeout (default: 10 seconds)
     */
    FileLoadOperation(const char* path, unsigned long timeoutMs = 10000)
        : AsyncOperation("Loading file", timeoutMs),
          fileOpened_(false),
          fileSize_(0) {
        strncpy(filePath_, path, sizeof(filePath_) - 1);
        filePath_[sizeof(filePath_) - 1] = '\0';
    }

    void start() override {
        AsyncOperation::start();

        // Attempt to open file
        file_ = SD.open(filePath_);
        if (!file_) {
            setError("Failed to open file");
            return;
        }

        fileOpened_ = true;
        fileSize_ = file_.size();

        // Update label with filename
        const char* filename = strrchr(filePath_, '/');
        if (filename) {
            filename++;  // Skip the '/'
            snprintf(label_, sizeof(label_), "Loading %s", filename);
        }

        // // Serial.print("[FileLoad] Opened: ");
        // // Serial.print(filePath_);
        // // Serial.print(" (");
        // // Serial.print(fileSize_);
        // // Serial.println(" bytes)");
    }

    /**
     * Get the opened file (only valid after isSuccess())
     */
    File& getFile() { return file_; }

    /**
     * Get file size
     */
    size_t getFileSize() const { return fileSize_; }

protected:
    bool poll() override {
        // For simple file opening, we're done immediately after start()
        // For actual loading into RAM, subclass could implement chunked reading
        return fileOpened_;
    }

    void onComplete() override {
        // // Serial.print("[FileLoad] Completed: ");
        // // Serial.println(filePath_);
    }

    void onFailed() override {
        if (file_) {
            file_.close();
        }
    }

    void onCancel() override {
        if (file_) {
            file_.close();
        }
    }

private:
    char filePath_[256];
    File file_;
    bool fileOpened_;
    size_t fileSize_;
};

/**
 * FileBufferLoadOperation - Async wrapper for loading file into RAM buffer
 *
 * Loads file in chunks to avoid blocking for large files
 */
class FileBufferLoadOperation : public AsyncOperation {
public:
    /**
     * Create a file buffer load operation
     * @param path - Full path to file
     * @param buffer - Pre-allocated buffer to load into
     * @param bufferSize - Size of buffer
     * @param timeoutMs - Load timeout (default: 30 seconds)
     */
    FileBufferLoadOperation(const char* path, uint8_t* buffer,
                           size_t bufferSize, unsigned long timeoutMs = 30000)
        : AsyncOperation("Loading file to RAM", timeoutMs),
          buffer_(buffer),
          bufferSize_(bufferSize),
          bytesRead_(0),
          fileSize_(0) {
        strncpy(filePath_, path, sizeof(filePath_) - 1);
        filePath_[sizeof(filePath_) - 1] = '\0';
    }

    void start() override {
        AsyncOperation::start();

        // Open file
        file_ = SD.open(filePath_);
        if (!file_) {
            setError("Failed to open file");
            return;
        }

        fileSize_ = file_.size();

        if (fileSize_ > bufferSize_) {
            setError("File too large for buffer");
            file_.close();
            return;
        }

        // Update label with filename
        const char* filename = strrchr(filePath_, '/');
        if (filename) {
            filename++;
            snprintf(label_, sizeof(label_), "Loading %s", filename);
        }

        // // Serial.print("[FileBufferLoad] Started: ");
        // // Serial.print(filePath_);
        // // Serial.print(" (");
        // // Serial.print(fileSize_);
        // // Serial.println(" bytes)");
    }

    /**
     * Get bytes read so far
     */
    size_t getBytesRead() const { return bytesRead_; }

    /**
     * Get total file size
     */
    size_t getFileSize() const { return fileSize_; }

    /**
     * Get progress override (based on bytes read, not time)
     */
    float getProgress() const {
        if (fileSize_ == 0) return 0.0f;
        if (bytesRead_ >= fileSize_) return 1.0f;
        return (float)bytesRead_ / (float)fileSize_;
    }

protected:
    bool poll() override {
        if (!file_) {
            return true;  // Failed, already handled in start()
        }

        // Read a chunk (4KB at a time to avoid blocking)
        const size_t CHUNK_SIZE = 4096;
        size_t toRead = min(CHUNK_SIZE, fileSize_ - bytesRead_);

        if (toRead > 0) {
            size_t read = file_.read(buffer_ + bytesRead_, toRead);
            bytesRead_ += read;

            // Update progress indicator if attached
            if (progressIndicator_) {
                progressIndicator_->setProgress(getProgress());
            }

            if (read != toRead) {
                setError("File read error");
                file_.close();
                return true;
            }
        }

        // Check if complete
        if (bytesRead_ >= fileSize_) {
            file_.close();
            return true;
        }

        return false;  // Still reading
    }

    void onComplete() override {
        // // Serial.print("[FileBufferLoad] Completed: ");
        // // Serial.print(filePath_);
        // // Serial.print(" (");
        // // Serial.print(bytesRead_);
        // // Serial.println(" bytes)");
    }

    void onFailed() override {
        if (file_) {
            file_.close();
        }
    }

    void onCancel() override {
        if (file_) {
            file_.close();
        }
    }

private:
    char filePath_[256];
    File file_;
    uint8_t* buffer_;
    size_t bufferSize_;
    size_t bytesRead_;
    size_t fileSize_;
};

#endif // FILE_ASYNC_OPERATIONS_H
