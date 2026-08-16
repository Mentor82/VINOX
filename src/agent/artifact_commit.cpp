#include "vinox/vinox_agent.h"
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace fs = std::filesystem;

// Path containment verifier preventing directory escape
static bool is_contained_path(const fs::path& root, const fs::path& file_path) {
    try {
        fs::path canonical_root = fs::weakly_canonical(root);
        fs::path canonical_file = fs::weakly_canonical(file_path);

        std::string root_str = canonical_root.string();
        std::string file_str = canonical_file.string();

        std::replace(root_str.begin(), root_str.end(), '\\', '/');
        std::replace(file_str.begin(), file_str.end(), '\\', '/');

        if (root_str.back() != '/') root_str.push_back('/');

        return (file_str.rfind(root_str, 0) == 0 || file_str == root_str.substr(0, root_str.length() - 1));
    } catch (...) {
        return false;
    }
}

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
                    std::ifstream in(entry.path(), std::ios::binary);
                    std::string line;
                    while (std::getline(in, line)) {
                        diff_stream << "+" << line << "\n";
                    }
                } else {
                    diff_stream << "@@ modified @@\n";
                    std::ifstream in(entry.path(), std::ios::binary);
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

    fs::path overlay_path(overlay_dir);
    fs::path target_path(target_dir);

    if (!fs::exists(overlay_path)) return VINOX_STATUS_OK;

    // Nephy Finding F.3 & F.4: Transactional Staged Workspace Takeover Commit
    fs::path staging_tmp = target_path.string() + "_staging_tmp";

    try {
        if (fs::exists(staging_tmp)) {
            fs::remove_all(staging_tmp);
        }
        fs::create_directories(staging_tmp);

        // 1. Copy existing target directory into staging directory
        if (fs::exists(target_path)) {
            for (const auto& entry : fs::recursive_directory_iterator(target_path)) {
                if (entry.is_regular_file()) {
                    fs::path rel = fs::relative(entry.path(), target_path);
                    fs::path dest = staging_tmp / rel;

                    if (!is_contained_path(staging_tmp, dest)) {
                        fs::remove_all(staging_tmp);
                        return VINOX_STATUS_INVALID_ARGUMENT; // Path escape rejected!
                    }

                    fs::create_directories(dest.parent_path());
                    fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
                }
            }
        }

        // 2. Apply overlay files into staging directory & verify path containment
        for (const auto& entry : fs::recursive_directory_iterator(overlay_path)) {
            if (entry.is_regular_file()) {
                fs::path rel = fs::relative(entry.path(), overlay_path);
                fs::path dest = staging_tmp / rel;

                if (!is_contained_path(staging_tmp, dest)) {
                    fs::remove_all(staging_tmp);
                    return VINOX_STATUS_INVALID_ARGUMENT; // Path escape rejected!
                }

                fs::create_directories(dest.parent_path());
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
            }
        }

        // 3. Perform atomic commit / swap into target directory
        if (fs::exists(target_path)) {
            fs::remove_all(target_path);
        }
        fs::rename(staging_tmp, target_path);

        return VINOX_STATUS_OK;
    } catch (...) {
        // Rollback staging folder on any error to ensure target directory remains 100% untouched!
        if (fs::exists(staging_tmp)) {
            try { fs::remove_all(staging_tmp); } catch (...) {}
        }
        return VINOX_STATUS_RUNTIME_ERROR;
    }
}

} // extern "C"
