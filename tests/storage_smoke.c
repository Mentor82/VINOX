#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vinox/storage.h"

int main(void) {
    vinox_storage_engine* engine = NULL;
    vinox_conversation_info conv_info = {0};
    vinox_message_info msg_in = {0};
    vinox_message_info msg_out = {0};
    size_t count = 0;
    size_t match_count = 0;

    const char* db_file = "test_vinox_storage.db";
    remove(db_file);

    // 1. Open database
    if (vinox_storage_engine_open(db_file, &engine) != VINOX_STATUS_OK || engine == NULL) {
        return 1;
    }

    // 2. Count on empty database
    if (vinox_storage_get_conversation_count(engine, &count) != VINOX_STATUS_OK || count != 0) {
        vinox_storage_engine_close(engine);
        return 2;
    }

    // 3. Create conversation with minimum struct_size
    conv_info.struct_size = VINOX_CONVERSATION_INFO_MIN_SIZE;
    if (vinox_storage_create_conversation(engine, "Phase 5 Test Conversation", &conv_info) != VINOX_STATUS_OK) {
        vinox_storage_engine_close(engine);
        return 3;
    }
    if (conv_info.id == NULL || strlen(conv_info.id) == 0 || strcmp(conv_info.title, "Phase 5 Test Conversation") != 0) {
        vinox_storage_engine_close(engine);
        return 4;
    }

    // 4. Verify conversation count
    if (vinox_storage_get_conversation_count(engine, &count) != VINOX_STATUS_OK || count != 1) {
        vinox_storage_engine_close(engine);
        return 5;
    }

    // 5. Add message to conversation
    msg_in.struct_size = sizeof(msg_in);
    msg_in.conversation_id = conv_info.id;
    msg_in.role = "user";
    msg_in.content = "VINOX Phase 5 Storage Test Message";
    msg_in.provenance_kind = VINOX_PROVENANCE_SOURCE_LITERAL;

    msg_out.struct_size = sizeof(msg_out);
    if (vinox_storage_add_message(engine, &msg_in, &msg_out) != VINOX_STATUS_OK) {
        vinox_storage_engine_close(engine);
        return 6;
    }
    if (msg_out.id == NULL || strcmp(msg_out.role, "user") != 0 || strcmp(msg_out.content, "VINOX Phase 5 Storage Test Message") != 0) {
        vinox_storage_engine_close(engine);
        return 7;
    }

    // 6. Search messages via FTS/LIKE
    if (vinox_storage_search_messages_fts(engine, "VINOX", 10, &match_count) != VINOX_STATUS_OK || match_count < 1) {
        vinox_storage_engine_close(engine);
        return 8;
    }

    vinox_storage_engine_close(engine);
    remove(db_file);
    return 0;
}
