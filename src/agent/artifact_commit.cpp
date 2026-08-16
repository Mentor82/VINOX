#include "vinox/vinox_agent.h"
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>

namespace fs = std::filesystem;

extern "C" {

VINOX_API vinox_status VINOX_CALL vinox_artifact_commit_diff(const char* overlay_dir, const char* target_dir, char* diff_buf, size_t diff_buf_sz) {
    if (!overlay_dir || !target_dir || !diff_buf || diff_buf_sz < 1) return VINOX_STATUS_INVALID_ARGUMENT;

    std::ostringstream diff_stream;
    fs::path overlay_path(overlay_dir);
    fs::path target_path(target_dir);

    if (!fs::exists(overlay_path)) {
        diff_stream << "No changes in overlay directory.\n";
    } else {
        for (const auto& entry : fs::recursive_directory_iterator(overlay_path)) {
            if (entry.is_regular_file()) {
                fs::path rel = fs::relative(entry.path(), overlay_path);
                fs::path target_file = target_path / rel;

                diff_stream << "--- a/" << rel.string() << "\n";
                diff_stream << "+++ b/" << rel.string() << "\n";

                if (!fs::exists(target_file)) {
                    diff_stream << "@@ -0,0 +1 @@\n";
                    std::ifstream in(entry.path());
                    std::string line;
                    while (std::getline(in, line)) {
                        diff_stream << "+" << line << "\n";
                    }
                } else {
                    diff_stream << "@@ modified @@\n";
                    std::ifstream in(entry.path());
                    std::string line;
                    while (std::getline(in, line)) {
                        diff_stream << "+" << line << "\n";
                    }
                }
            }
        }
    }

    std::string res = diff_stream.str();
#if defined(_WIN32)
    strncpy_s(diff_buf, diff_buf_sz, res.c_str(), _TRUNCATE);
#else
    strncpy(diff_buf, res.c_str(), diff_buf_sz - 1);
    diff_buf[diff_buf_sz - 1] = '\0';
#endif

    return VINOX_STATUS_OK;
}

VINOX_API vinox_status VINOX_CALL vinox_artifact_commit_apply(const char* overlay_dir, const char* target_dir) {
    if (!overlay_dir || !target_dir) return VINOX_STATUS_INVALID_ARGUMENT;

    try {
        fs::path overlay_path(overlay_dir);
        fs::path target_path(target_dir);

        if (!fs::exists(overlay_path)) return VINOX_STATUS_OK;

        for (const auto& entry : fs::recursive_directory_iterator(overlay_path)) {
            if (entry.is_regular_file()) {
                fs::path rel = fs::relative(entry.path(), overlay_path);
                fs::path dest = target_path / rel;

                fs::create_directories(dest.parent_path());
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
            }
        }

        return VINOX_STATUS_OK;
    } catch (...) {
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

} // extern "C"
