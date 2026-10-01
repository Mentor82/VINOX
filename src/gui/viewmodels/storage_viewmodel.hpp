#pragma once

#include "../backend_interface.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vinox::gui {

class StorageViewModel {
public:
    explicit StorageViewModel(std::shared_ptr<IVinoxBackend> backend);

    void set_backend(std::shared_ptr<IVinoxBackend> backend);

    const std::vector<std::pair<std::string, std::string>>& conversations() const { return conversations_; }
    const std::vector<SearchMatch>& search_results() const { return search_results_; }
    const std::vector<EntityRelation>& relations() const { return relations_; }

    void refresh_conversations();
    std::string create_conversation(const std::string& title);
    void perform_search(const std::string& query, float alpha = 0.5f, uint32_t limit = 10);
    void inspect_relations(const std::string& source_id);

    std::function<void()> on_conversations_changed;
    std::function<void()> on_search_results_changed;
    std::function<void()> on_relations_changed;

private:
    std::shared_ptr<IVinoxBackend> backend_;
    std::vector<std::pair<std::string, std::string>> conversations_;
    std::vector<SearchMatch> search_results_;
    std::vector<EntityRelation> relations_;
};

} // namespace vinox::gui
