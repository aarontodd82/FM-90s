#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

/**
 * Event Callback Types - Function signatures for event handlers
 *
 * Part of GUI Framework Redesign - Phase 1: Event System
 * See docs/GUI_FRAMEWORK_REDESIGN.md for architecture details
 */

/**
 * Generic event callback signature
 * @param context - User data passed during registration (typically 'this' pointer)
 */
typedef void (*EventCallback)(void* context);

/**
 * Event callback with integer parameter
 * @param value - Event-specific integer value (e.g., device index, count)
 * @param context - User data
 */
typedef void (*EventCallbackInt)(int value, void* context);

/**
 * Event callback with string parameter
 * @param message - Event-specific message (e.g., error message, device name)
 * @param context - User data
 */
typedef void (*EventCallbackStr)(const char* message, void* context);

#endif // EVENT_TYPES_H
