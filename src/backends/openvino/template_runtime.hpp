#ifndef VINOX_BACKENDS_OPENVINO_TEMPLATE_RUNTIME_HPP
#define VINOX_BACKENDS_OPENVINO_TEMPLATE_RUNTIME_HPP

// Issue #21 rendering boundary: an implementation-neutral interface for
// executing a model package's own Hugging Face / Jinja chat_template.
//
// This header is an internal implementation detail of the OpenVINO backend's
// model-protocol compilation path. It is not part of the public VINOX C-ABI
// (include/vinox/*.h) and must never be exposed across a DLL boundary: it is
// free to use STL types because every translation unit that includes it is
// compiled into the same binary that consumes it.
//
// Core invariant (Issue #21 / #20): the model package owns its template
// syntax, VINOX owns the canonical contract. A concrete ITemplateRuntime may
// execute syntax, but it must never invent protocol semantics, never
// silently substitute, simplify, or drop unsupported constructs, and must
// enforce every resource bound present on the request. This header defines
// only the boundary types; it deliberately contains no rendering logic and
// no knowledge of any specific model family.

#include <cstddef>
#include <string>
#include <vector>

namespace vinox::model {

// One conversation message as seen by the template. `tool_calls_json` and
// `tool_call_id` are raw, caller-supplied JSON text (already validated
// against the canonical VINOX tool contract upstream of this boundary) and
// are passed through verbatim; the runtime does not interpret their content
// beyond what the template itself does.
struct TemplateMessage {
    std::string role;
    std::string content;
    std::string tool_calls_json;  // raw JSON array text; empty if none
    std::string tool_call_id;     // set when role == "tool"
};

// Explicit, caller-supplied resource bounds (Issue #21 "Security / Resource
// Bounds"). A value of 0 means "runtime default", not "unbounded" -- a
// concrete ITemplateRuntime must never treat 0 as unlimited.
struct TemplateRenderLimits {
    size_t max_template_size = 0;
    size_t max_output_size = 0;
    size_t max_iterations = 0;
    size_t max_nesting_depth = 0;
    size_t max_work_units = 0;      // deterministic execution-step budget
};

struct TemplateRenderRequest {
    std::string template_source;  // the package-supplied Jinja source, verbatim
    std::vector<TemplateMessage> messages;
    std::string tools_json;  // raw JSON array of tool definitions; empty if none
    bool add_generation_prompt = false;
    bool enable_thinking = true;
    std::string bos_token;
    std::string eos_token;
    TemplateRenderLimits limits;
};

// Half-open [begin, end) byte offsets into TemplateRenderResult::rendered_text
// produced by a single {% generation %} ... {% endgeneration %} block, in
// source order. Used by callers that need to know which spans of the
// rendered text correspond to assistant-generated turns.
struct TemplateSpan {
    size_t begin = 0;
    size_t end = 0;
};

enum class TemplateRenderStatus {
    Ok,
    Unsupported,       // construct/feature the runtime cannot safely execute
    Invalid,           // malformed template syntax
    ResourceExceeded,  // a declared bound in TemplateRenderLimits was hit
};

struct TemplateRenderResult {
    TemplateRenderStatus status = TemplateRenderStatus::Invalid;
    std::string rendered_text;
    std::vector<TemplateSpan> generation_spans;
    std::string error_detail;
};

// Implementation-neutral rendering boundary. A concrete implementation (e.g.
// a minja-backed runtime) executes `request.template_source` against the
// supplied conversation state and returns the rendered text plus any
// {% generation %} spans.
//
// Fail-closed contract: if the template uses a construct the implementation
// cannot execute faithfully, or a declared bound is exceeded, the
// implementation must return Unsupported / ResourceExceeded (with
// `rendered_text` left empty) rather than a partial or best-effort render.
// It must never know or branch on a model family, filename, or display name.
class ITemplateRuntime {
public:
    virtual ~ITemplateRuntime() = default;
    virtual TemplateRenderResult render(const TemplateRenderRequest& request) const = 0;
};

}  // namespace vinox::model

#endif  // VINOX_BACKENDS_OPENVINO_TEMPLATE_RUNTIME_HPP
