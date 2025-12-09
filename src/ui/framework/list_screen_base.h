#ifndef LIST_SCREEN_BASE_H
#define LIST_SCREEN_BASE_H

#include "../screen_new.h"
#include "../../dos_colors.h"
#include <Arduino.h>

/**
 * ListScreenBase - Base class for list-based screens using new framework
 *
 * Provides:
 * - Automatic scrolling and navigation
 * - Selection highlighting
 * - Action cycling (LEFT/RIGHT)
 * - Incremental rendering (no blanking)
 * - Contextual LCD updates
 *
 * Derived classes implement:
 * - getItemCount() - Number of items
 * - drawItem(index, row, selected) - Draw single item
 * - onItemSelected(index) - Action when item is selected
 *
 * Example:
 *   class MyListScreen : public ListScreenBase {
 *       int getItemCount() override { return items.size(); }
 *       void drawItem(int idx, int row, bool selected) override {
 *           // Draw items[idx] at row
 *       }
 *       ScreenResult onItemSelected(int idx) override {
 *           return ScreenResult::navigateTo(SCREEN_DETAILS, items[idx]);
 *       }
 *   };
 */
class ListScreenBase : public Screen {
protected:
    int selectedIndex_;      // Currently highlighted item
    int previousIndex_;      // Previous selection (for incremental updates)
    int scrollOffset_;       // First visible item index
    int previousScrollOffset_; // Previous scroll (for detecting scroll changes)
    int visibleItems_;       // How many items fit on screen
    int startRow_;           // First row for item drawing
    int itemSpacing_;        // Rows between items (1=compact, 2=spaced)

public:
    /**
     * Create a list screen
     * @param context - Screen context
     * @param visibleCount - Number of items visible at once
     * @param startRowNum - First row to draw items
     * @param spacing - Space between items (1-3)
     */
    ListScreenBase(ScreenContext* context,
                   int visibleCount = 20,
                   int startRowNum = 5,
                   int spacing = 1)
        : Screen(context),
          selectedIndex_(0),
          scrollOffset_(0),
          visibleItems_(visibleCount),
          startRow_(startRowNum),
          itemSpacing_(spacing) {}

    virtual ~ListScreenBase() {}

    // ============================================
    // PURE VIRTUAL - Must implement
    // ============================================

    /**
     * Get number of items in the list
     */
    virtual int getItemCount() = 0;

    /**
     * Draw a single item
     * @param itemIndex - Index in the data (0 to getItemCount()-1)
     * @param row - Screen row to draw at
     * @param selected - Is this item highlighted?
     */
    virtual void drawItem(int itemIndex, int row, bool selected) = 0;

    /**
     * Called when SELECT is pressed on an item
     * @param itemIndex - Index of selected item
     * @return ScreenResult for navigation
     */
    virtual ScreenResult onItemSelected(int itemIndex) = 0;

    // ============================================
    // OPTIONAL OVERRIDES
    // ============================================

    /**
     * Draw the screen header (optional)
     */
    virtual void drawHeader() {
        // Default: no header
    }

    /**
     * Draw the screen footer (optional)
     */
    virtual void drawFooter() {
        // Default: no footer
    }

    /**
     * Called when LEFT is pressed (optional)
     */
    virtual ScreenResult onLeft() {
        return ScreenResult::stay();
    }

    /**
     * Called when RIGHT is pressed (optional)
     */
    virtual ScreenResult onRight() {
        return ScreenResult::stay();
    }

    // ============================================
    // SCREEN OVERRIDES
    // ============================================

    void draw() override {
        drawHeader();
        drawList();
        drawFooter();
    }

    void updateLCD() override {
        if (!context_->lcdManager) return;

        int count = getItemCount();
        if (count == 0) {
            context_->lcdManager->setLine(0, "No items");
            context_->lcdManager->clearLine(1);
            return;
        }

        // Line 1: Item count
        context_->lcdManager->setLineF(0, "Item %d/%d", selectedIndex_ + 1, count);

        // Line 2: Button legend
        context_->lcdManager->setLine(1, "Sel:Choose");
    }

    ScreenResult onButton(uint8_t button) override {
        int count = getItemCount();
        if (count == 0) {
            if (button == BUTTON_DOWN) {
                return ScreenResult::goBack();
            }
            return ScreenResult::stay();
        }

        switch (button) {
            case BUTTON_UP:
                navigateUp();
                return ScreenResult::stay();

            case BUTTON_DOWN:
                navigateDown();
                return ScreenResult::stay();

            case BUTTON_LEFT:
                return onLeft();

            case BUTTON_RIGHT:
                return onRight();

            case BUTTON_SELECT:
                return onItemSelected(selectedIndex_);

            default:
                return ScreenResult::stay();
        }
    }

protected:
    // ============================================
    // NAVIGATION HELPERS
    // ============================================

    /**
     * Navigate up in the list
     */
    void navigateUp() {
        int count = getItemCount();
        if (count == 0) return;

        int oldIndex = selectedIndex_;
        int oldScroll = scrollOffset_;

        selectedIndex_--;
        if (selectedIndex_ < 0) {
            selectedIndex_ = count - 1;  // Wrap to bottom
            scrollOffset_ = max(0, count - visibleItems_);
        } else if (selectedIndex_ < scrollOffset_) {
            scrollOffset_ = selectedIndex_;
        }

        // Incremental update: only redraw if something changed
        if (oldScroll != scrollOffset_) {
            // Scroll changed - redraw entire list
            drawList();
        } else {
            // Just selection changed - redraw only affected items
            redrawSelectionChange(oldIndex, selectedIndex_);
        }

        updateLCD();
    }

    /**
     * Navigate down in the list
     */
    void navigateDown() {
        int count = getItemCount();
        if (count == 0) return;

        int oldIndex = selectedIndex_;
        int oldScroll = scrollOffset_;

        selectedIndex_++;
        if (selectedIndex_ >= count) {
            selectedIndex_ = 0;  // Wrap to top
            scrollOffset_ = 0;
        } else if (selectedIndex_ >= scrollOffset_ + visibleItems_) {
            scrollOffset_ = selectedIndex_ - visibleItems_ + 1;
        }

        // Incremental update: only redraw if something changed
        if (oldScroll != scrollOffset_) {
            // Scroll changed - redraw entire list
            drawList();
        } else {
            // Just selection changed - redraw only affected items
            redrawSelectionChange(oldIndex, selectedIndex_);
        }

        updateLCD();
    }

    /**
     * Jump to a specific item
     */
    void jumpToItem(int itemIndex) {
        int count = getItemCount();
        if (count == 0) return;

        // Clamp to valid range
        selectedIndex_ = max(0, min(itemIndex, count - 1));

        // Adjust scroll to keep selection visible
        if (selectedIndex_ < scrollOffset_) {
            scrollOffset_ = selectedIndex_;
        } else if (selectedIndex_ >= scrollOffset_ + visibleItems_) {
            scrollOffset_ = selectedIndex_ - visibleItems_ + 1;
        }

        draw();
        updateLCD();
    }

    /**
     * Draw the list of items
     */
    void drawList() {
        int count = getItemCount();
        if (count == 0) {
            context_->ui->drawText(10, startRow_ + 5, "No items to display",
                                  DOS_LIGHT_GRAY, DOS_BLUE);
            return;
        }

        int row = startRow_;
        int endIndex = min(scrollOffset_ + visibleItems_, count);

        for (int i = scrollOffset_; i < endIndex; i++) {
            bool selected = (i == selectedIndex_);
            drawItem(i, row, selected);
            row += itemSpacing_;
        }
    }

    /**
     * Get currently selected item index
     */
    int getSelectedIndex() const {
        return selectedIndex_;
    }

    /**
     * Get scroll offset (first visible item)
     */
    int getScrollOffset() const {
        return scrollOffset_;
    }

private:
    /**
     * Redraw only the items affected by selection change (incremental update)
     * This avoids blanking the entire screen when just the cursor moved
     */
    void redrawSelectionChange(int oldIndex, int newIndex) {
        // If either index is out of visible range, do full list redraw
        if (oldIndex < scrollOffset_ || oldIndex >= scrollOffset_ + visibleItems_ ||
            newIndex < scrollOffset_ || newIndex >= scrollOffset_ + visibleItems_) {
            drawList();
            return;
        }

        // Redraw old item as unselected (only if it's visible)
        if (oldIndex >= scrollOffset_ && oldIndex < scrollOffset_ + visibleItems_) {
            int oldRow = startRow_ + (oldIndex - scrollOffset_) * itemSpacing_;
            drawItem(oldIndex, oldRow, false);
        }

        // Redraw new item as selected (only if it's visible)
        if (newIndex >= scrollOffset_ && newIndex < scrollOffset_ + visibleItems_) {
            int newRow = startRow_ + (newIndex - scrollOffset_) * itemSpacing_;
            drawItem(newIndex, newRow, true);
        }
    }
};

#endif // LIST_SCREEN_BASE_H
