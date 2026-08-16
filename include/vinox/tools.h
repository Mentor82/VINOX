#ifndef VINOX_TOOLS_H
#define VINOX_TOOLS_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/logging.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

// Security classification tiers for tools
typedef enum vinox_security_class {
    VINOX_SECURITY_CLASS_READ_ONLY = 0,
    VINOX_SECURITY_CLASS_LOCAL_WRITE = 1,
    VINOX_SECURITY_CLASS_PROCESS = 2,
    VINOX_SECURITY_CLASS_NETWORK = 3,
    VINOX_SECURITY_CLASS_ADMIN = 4
} vinox_security_class;

// Approval modes for policy engine decisions
typedef enum vinox_approval_mode {
    VINOX_APPROVAL_DENIED = 0,
    VINOX_APPROVAL_AUTO_ALLOWED = 1,
    VINOX_APPROVAL_APPROVED_ONCE = 2,
    VINOX_APPROVAL_APPROVED_SESSION = 3,
    VINOX_APPROVAL_APPROVED_PERMANENT = 4
} vinox_approval_mode;

// Tool definition struct (Prefix-Layout ABI)
typedef struct vinox_tool_definition {
    uint32_t struct_size;               // Size of struct for ABI forward/backward compatibility
    const char* name;                   // Unique tool identifier e.g. "vinox.search"
    const char* description;            // Human-readable tool description
    const char* parameters_json_schema; // JSON Schema string defining valid arguments
    uint32_t security_class;            // vinox_security_class tier
} vinox_tool_definition;

#define VINOX_TOOL_DEFINITION_MIN_SIZE (sizeof(uint32_t) + sizeof(const char*) * 3 + sizeof(uint32_t))

// Tool call request struct (Prefix-Layout ABI)
typedef struct vinox_tool_call_request {
    uint32_t struct_size;                       // Size of struct for ABI compatibility
    const char* call_id;                        // Unique call invocation ID (e.g. "call_abc123")
    const char* tool_name;                      // Target tool identifier
    const char* arguments_json;                 // Input arguments JSON string
    const vinox_correlation_context* correlation; // Optional correlation context
} vinox_tool_call_request;

#define VINOX_TOOL_CALL_REQUEST_MIN_SIZE (sizeof(uint32_t) + sizeof(const char*) * 3 + sizeof(const vinox_correlation_context*))

// Policy decision result struct (Prefix-Layout ABI)
typedef struct vinox_policy_decision {
    uint32_t struct_size;   // Size of struct for ABI compatibility
    int32_t allowed;        // 1 if tool execution is permitted, 0 if denied
    uint32_t approval_mode; // vinox_approval_mode
    const char* reason;     // Human-readable policy evaluation decision reason
} vinox_policy_decision;

#define VINOX_POLICY_DECISION_MIN_SIZE (sizeof(uint32_t) + sizeof(int32_t) + sizeof(uint32_t) + sizeof(const char*))

// Tool call execution result struct (Prefix-Layout ABI)
typedef struct vinox_tool_call_result {
    uint32_t struct_size;           // Size of struct for ABI compatibility
    const char* call_id;            // Call invocation ID matching request
    int32_t status_code;            // 0 = Success, non-zero = Execution error
    const char* result_json;        // Return output JSON string
    const char* error_message;      // Diagnostic error message if status_code != 0
    uint64_t execution_duration_ms; // Execution duration in milliseconds
} vinox_tool_call_result;

#define VINOX_TOOL_CALL_RESULT_MIN_SIZE (sizeof(uint32_t) + sizeof(const char*) + sizeof(int32_t) + sizeof(const char*) * 2 + sizeof(uint64_t))

// Opaque registry & policy engine handles
typedef struct vinox_tool_registry vinox_tool_registry;
typedef struct vinox_policy_engine vinox_policy_engine;

// Tool Registry C-ABI API
VINOX_API vinox_status vinox_tool_registry_create(vinox_tool_registry** registry_out);
VINOX_API vinox_status vinox_tool_registry_destroy(vinox_tool_registry* registry);
VINOX_API vinox_status vinox_tool_registry_register_tool(vinox_tool_registry* registry, const vinox_tool_definition* tool_def);
VINOX_API vinox_status vinox_tool_registry_unregister_tool(vinox_tool_registry* registry, const char* tool_name);
VINOX_API vinox_status vinox_tool_registry_find_tool(const vinox_tool_registry* registry, const char* tool_name, vinox_tool_definition* tool_def_out, char* pool_buf, size_t pool_buf_size);
VINOX_API vinox_status vinox_tool_registry_validate_arguments(const vinox_tool_registry* registry, const char* tool_name, const char* args_json, char* err_buf, size_t err_buf_size);

// Policy Engine C-ABI API
VINOX_API vinox_status vinox_policy_engine_create(vinox_policy_engine** engine_out);
VINOX_API vinox_status vinox_policy_engine_destroy(vinox_policy_engine* engine);
VINOX_API vinox_status vinox_policy_engine_set_rule(vinox_policy_engine* engine, const char* tool_name_pattern, uint32_t max_security_class, uint32_t approval_mode);
VINOX_API vinox_status vinox_policy_engine_evaluate(const vinox_policy_engine* engine, const vinox_tool_call_request* request, const vinox_tool_definition* tool_def, vinox_policy_decision* decision_out, char* reason_buf, size_t reason_buf_size);

// OpenAI Tool Calling Format Mapping
VINOX_API vinox_status vinox_tools_format_openai_schema(const vinox_tool_registry* registry, char* output_buf, size_t output_buf_size, size_t* required_size_out);
VINOX_API vinox_status vinox_tools_parse_openai_tool_call(const char* openai_tool_call_json, vinox_tool_call_request* request_out, char* pool_buf, size_t pool_buf_size);

// Diagnostic Last Error API for Tools Module
VINOX_API const char* vinox_tools_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // VINOX_TOOLS_H
