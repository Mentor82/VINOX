#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "vinox/serving.h"

namespace vinox::serving {

enum class OverflowStrategy {
    Reject,
    TruncateOldest
};

struct ContextBudgetOptions {
    uint64_t max_context_length{4096};
    uint64_t system_tokens{0};
    uint64_t user_prompt_tokens{0};
    uint64_t reserved_answer_tokens{256};
    OverflowStrategy strategy{OverflowStrategy::Reject};
};

struct ContextEvaluationResult {
    bool fits{true};
    uint64_t total_tokens{0};
    size_t truncated_history_items{0};
};

ContextEvaluationResult evaluate_context_budget(
    const ContextBudgetOptions& options,
    const std::vector<uint64_t>& history_item_token_counts
) {
    ContextEvaluationResult result;
    uint64_t history_total = 0;
    for (uint64_t count : history_item_token_counts) {
        history_total += count;
    }

    result.total_tokens = options.system_tokens + history_total + options.user_prompt_tokens + options.reserved_answer_tokens;

    if (result.total_tokens <= options.max_context_length) {
        result.fits = true;
        result.truncated_history_items = 0;
        return result;
    }

    if (options.strategy == OverflowStrategy::Reject) {
        result.fits = false;
        result.truncated_history_items = 0;
        return result;
    }

    // Truncate oldest history items until it fits
    size_t truncated_count = 0;
    uint64_t current_history = history_total;

    for (size_t i = 0; i < history_item_token_counts.size(); ++i) {
        if (options.system_tokens + current_history + options.user_prompt_tokens + options.reserved_answer_tokens <= options.max_context_length) {
            break;
        }
        current_history -= history_item_token_counts[i];
        truncated_count++;
    }

    result.total_tokens = options.system_tokens + current_history + options.user_prompt_tokens + options.reserved_answer_tokens;
    result.fits = result.total_tokens <= options.max_context_length;
    result.truncated_history_items = truncated_count;
    return result;
}

}  // namespace vinox::serving
