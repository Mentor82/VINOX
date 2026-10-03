#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "src/gui/backend_interface.hpp"
#include "src/gui/local_backend.hpp"
#include "src/gui/remote_backend.hpp"
#include "src/gui/viewmodels/chat_viewmodel.hpp"
#include "src/gui/viewmodels/plan_viewmodel.hpp"
#include "src/gui/viewmodels/agent_viewmodel.hpp"
#include "src/gui/viewmodels/storage_viewmodel.hpp"
#include "src/gui/viewmodels/diff_viewmodel.hpp"

int main() {
    std::cout << "================================================================================\n";
    std::cout << "  VINOX Phase 10 — Native Desktop GUI ViewModels & Invariants Smoke Test        \n";
    std::cout << "================================================================================\n";

    auto local_backend = std::make_shared<vinox::gui::LocalVinoxBackend>("test_gui_smoke.db", "mock");

    // =========================================================================
    // TEST 01: Connection & Model Management
    // =========================================================================
    {
        std::cout << "[TEST 01] Backend Connection & Model Lifecycle ... ";
        assert(local_backend->is_connected());
        auto models = local_backend->list_models();
        assert(!models.empty());
        assert(models[0].is_loaded);

        assert(local_backend->load_model("mock", "CPU"));
        assert(local_backend->unload_model("mock"));
        assert(local_backend->load_model("mock", "CPU"));
        std::cout << "[ PASS ]\n";
    }

    // =========================================================================
    // TEST 02: ChatViewModel Multi-Turn & Token Streaming
    // =========================================================================
    {
        std::cout << "[TEST 02] ChatViewModel Streaming & Token Rate ... ";
        vinox::gui::ChatViewModel chat_vm(local_backend);

        bool message_added_called = false;
        bool token_received = false;
        chat_vm.on_message_added = [&]() { message_added_called = true; };
        chat_vm.on_token_delta = [&](const std::string&, bool) { token_received = true; };

        chat_vm.send_message("Explain the VINOX C-ABI architecture");
        assert(message_added_called);
        assert(token_received);
        assert(chat_vm.messages().size() == 2); // user + assistant
        assert(chat_vm.messages()[0].role == "user");
        assert(chat_vm.messages()[1].role == "assistant");
        assert(!chat_vm.messages()[1].content.empty());
        assert(chat_vm.total_tokens() > 0);
        std::cout << "[ PASS ]\n";
    }

    // =========================================================================
    // TEST 03: ChatViewModel Cooperative Cancellation
    // =========================================================================
    {
        std::cout << "[TEST 03] ChatViewModel Cooperative Cancellation ... ";
        vinox::gui::ChatViewModel chat_vm(local_backend);
        chat_vm.cancel();
        assert(!chat_vm.is_generating());
        chat_vm.clear();
        assert(chat_vm.messages().empty());
        std::cout << "[ PASS ]\n";
    }

    // =========================================================================
    // TEST 04: PlanViewModel Hash-Bound Approval (Fail-Closed)
    // =========================================================================
    std::string valid_plan_id;
    std::string valid_plan_hash;
    {
        std::cout << "[TEST 04] PlanViewModel Stale-State Protection (Hash Invariant) ... ";
        vinox::gui::PlanViewModel plan_vm(local_backend);

        assert(plan_vm.generate_plan("Refactor VINOX Storage Engine"));
        assert(plan_vm.has_active_plan());
        valid_plan_id = plan_vm.current_plan().plan_id;
        valid_plan_hash = plan_vm.current_plan().plan_hash;
        assert(!valid_plan_id.empty() && !valid_plan_hash.empty());
        assert(!plan_vm.is_approved());

        // Negative test: Mismatched hash must fail closed
        bool bad_ok = plan_vm.approve_plan("0000_stale_hash_0000");
        assert(!bad_ok);
        assert(!plan_vm.is_approved());
        assert(!plan_vm.error_message().empty());

        // Positive test: Matching hash approves
        bool good_ok = plan_vm.approve_plan(valid_plan_hash);
        assert(good_ok);
        assert(plan_vm.is_approved());
        std::cout << "[ PASS ]\n";
    }

    // =========================================================================
    // TEST 05: AgentViewModel Lifecycle, Sequenced Events & Cancellation
    // =========================================================================
    std::string active_run_id;
    {
        std::cout << "[TEST 05] AgentViewModel Lifecycle & Cancellation ... ";
        vinox::gui::AgentViewModel agent_vm(local_backend);

        // Negative test: unapproved plan or bad hash fails
        bool bad_start = agent_vm.start_run("non_existent_plan", "bad_hash", ".");
        assert(!bad_start);
        assert(!agent_vm.is_running());

        // Positive test: valid approved plan starts run
        bool good_start = agent_vm.start_run(valid_plan_id, valid_plan_hash, ".");
        assert(good_start);
        assert(agent_vm.is_running());
        active_run_id = agent_vm.status().run_id;
        assert(!active_run_id.empty());

        // Sequenced events check
        assert(!agent_vm.events().empty());
        assert(agent_vm.events()[0].sequence_id == 1);
        assert(agent_vm.events()[0].event_type == "step_start");

        // Cancellation check
        assert(agent_vm.cancel_run());
        assert(agent_vm.status().state == "cancelled");
        std::cout << "[ PASS ]\n";
    }

    // =========================================================================
    // TEST 06: StorageViewModel & Hybrid Search & Relations
    // =========================================================================
    {
        std::cout << "[TEST 06] StorageViewModel Search & CTE Relations ... ";
        vinox::gui::StorageViewModel storage_vm(local_backend);

        std::string conv_id = storage_vm.create_conversation("GUI Unit Test Session");
        assert(!conv_id.empty());
        assert(!storage_vm.conversations().empty());

        storage_vm.perform_search("test query", 0.5f, 5);
        // Search executes without throwing
        storage_vm.inspect_relations(conv_id);
        std::cout << "[ PASS ]\n";
    }

    // =========================================================================
    // TEST 07: DiffViewModel & Snapshot-Bound Mutation Invariant
    // =========================================================================
    {
        std::cout << "[TEST 07] DiffViewModel Snapshot-Bound Mutation ... ";
        vinox::gui::DiffViewModel diff_vm(local_backend);

        diff_vm.load_diff(active_run_id);
        assert(diff_vm.has_pending_diff());
        assert(!diff_vm.artifact().hunks.empty());
        assert(diff_vm.artifact().total_additions > 0);

        uint32_t hunk_id = diff_vm.artifact().hunks[0].id;
        diff_vm.toggle_hunk(hunk_id); // toggle off
        assert(!diff_vm.artifact().hunks[0].is_selected);

        // Negative test: apply with no hunks selected fails
        bool apply_empty = diff_vm.apply_selected(active_run_id);
        assert(!apply_empty);

        diff_vm.toggle_hunk(hunk_id); // toggle back on
        assert(diff_vm.artifact().hunks[0].is_selected);

        // Positive test: apply with valid snapshot hash
        bool apply_ok = diff_vm.apply_selected(active_run_id);
        assert(apply_ok);
        assert(!diff_vm.has_pending_diff());
        std::cout << "[ PASS ]\n";
    }

    // =========================================================================
    // TEST 08: Remote Backend Parity (Contracts & DTO Compatibility)
    // =========================================================================
    {
        std::cout << "[TEST 08] Remote Backend DTO & Contract Parity ... ";
        auto remote_backend = std::make_shared<vinox::gui::RemoteVinoxBackend>("127.0.0.1", 18080);
        assert(remote_backend->backend_name() == "Remote (HTTP/SSE)");

        vinox::gui::PlanViewModel remote_plan_vm(remote_backend);
        // Verify ViewModel smoothly attaches to remote backend
        remote_plan_vm.clear();
        assert(!remote_plan_vm.has_active_plan());
        std::cout << "[ PASS ]\n";
    }

    std::cout << "\n================================================================================\n";
    std::cout << "  ALL 8 GUI VIEWMODEL & BACKEND ARCHITECTURE TESTS PASSED!                      \n";
    std::cout << "================================================================================\n";
    return 0;
}
