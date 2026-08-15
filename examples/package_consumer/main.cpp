#include <iostream>

#include "vinox/openvino.h"
#include "vinox/vinox.hpp"

int main() {
    const auto version = vinox::version();
    std::cout << "consumer: vinox " << version.text << '\n';
    if (version.abi != VINOX_ABI_VERSION) {
        return 1;
    }

    vinox_model* model = nullptr;
    return vinox_model_load(nullptr, &model) == VINOX_STATUS_INVALID_ARGUMENT
        ? 0
        : 2;
}
