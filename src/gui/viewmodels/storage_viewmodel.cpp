#include "storage_viewmodel.hpp"

namespace vinox::gui {

StorageViewModel::StorageViewModel(std::shared_ptr<IVinoxBackend> backend)
    : backend_(std::move(backend)) {}

void StorageViewModel::set_backend(std::shared_ptr<IVinoxBackend> backend) {
    backend_ = std::move(backend);
}

void StorageViewModel::refresh_conversations() {
    if (!backend_) return;
    conversations_ = backend_->list_conversations();
    if (on_conversations_changed) on_conversations_changed();
}

std::string StorageViewModel::create_conversation(const std::string& title) {
    if (!backend_) return "";
    std::string id = backend_->create_conversation(title);
    refresh_conversations();
    return id;
}

void StorageViewModel::perform_search(const std::string& query, float alpha, uint32_t limit) {
    if (!backend_ || query.empty()) {
        search_results_.clear();
        if (on_search_results_changed) on_search_results_changed();
        return;
    }
    search_results_ = backend_->search_hybrid(query, alpha, limit);
    if (on_search_results_changed) on_search_results_changed();
}

void StorageViewModel::inspect_relations(const std::string& source_id) {
    if (!backend_ || source_id.empty()) {
        relations_.clear();
        if (on_relations_changed) on_relations_changed();
        return;
    }
    relations_ = backend_->get_relations(source_id);
    if (on_relations_changed) on_relations_changed();
}

} // namespace vinox::gui
