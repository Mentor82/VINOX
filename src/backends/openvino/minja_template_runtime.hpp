#ifndef VINOX_BACKENDS_OPENVINO_MINJA_TEMPLATE_RUNTIME_HPP
#define VINOX_BACKENDS_OPENVINO_MINJA_TEMPLATE_RUNTIME_HPP

// Issue #21 concrete ITemplateRuntime backed by the vendored minja Jinja
// engine (src/thirdparty/minja/). Deliberately declared with no minja types
// in this header -- minja.hpp is only included from the .cpp, so translation
// units that just need to construct/hold a runtime don't pay for its (large,
// warning-heavy) template machinery.

#include "template_runtime.hpp"

namespace vinox::model {

class MinjaTemplateRuntime final : public ITemplateRuntime {
public:
    TemplateRenderResult render(const TemplateRenderRequest& request) const override;
};

}  // namespace vinox::model

#endif  // VINOX_BACKENDS_OPENVINO_MINJA_TEMPLATE_RUNTIME_HPP
