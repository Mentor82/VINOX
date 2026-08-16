#include "vinox/vinox_agent.h"
#include <atomic>
#include <new>

struct vinox_mode_controller {
    std::atomic<vinox_mode> current_mode{VINOX_MODE_CHAT};
};

extern "C" {

VINOX_API vinox_mode_controller* VINOX_CALL vinox_mode_controller_create(void) {
    return new (std::nothrow) vinox_mode_controller();
}

VINOX_API void VINOX_CALL vinox_mode_controller_destroy(vinox_mode_controller* controller) {
    if (controller) {
        delete controller;
    }
}

VINOX_API vinox_mode VINOX_CALL vinox_mode_controller_get_mode(const vinox_mode_controller* controller) {
    if (!controller) return VINOX_MODE_CHAT;
    return controller->current_mode.load(std::memory_order_relaxed);
}

VINOX_API vinox_status VINOX_CALL vinox_mode_controller_set_mode(vinox_mode_controller* controller, vinox_mode new_mode) {
    if (!controller) return VINOX_STATUS_INVALID_ARGUMENT;
    if (new_mode < VINOX_MODE_CHAT || new_mode > VINOX_MODE_AGENT) return VINOX_STATUS_INVALID_ARGUMENT;
    controller->current_mode.store(new_mode, std::memory_order_relaxed);
    return VINOX_STATUS_OK;
}

VINOX_API int VINOX_CALL vinox_mode_controller_can_execute_mutating_tool(const vinox_mode_controller* controller) {
    if (!controller) return 0;
    return (controller->current_mode.load(std::memory_order_relaxed) == VINOX_MODE_AGENT) ? 1 : 0;
}

} // extern "C"
