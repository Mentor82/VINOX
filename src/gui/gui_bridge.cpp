#include "gui_bridge.hpp"

#include <QMetaObject>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <chrono>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#endif

namespace vinox::gui {

GuiBridge::GuiBridge(std::shared_ptr<IVinoxBackend> backend, bool is_remote, QObject* parent)
    : QObject(parent), backend_(std::move(backend)), is_remote_(is_remote) {
    refreshStorageInfo();
    if (!is_remote_) {
        scanModels(models_path_);
        applyModelConfig(current_model_);
    }
    refreshConversations();
}

GuiBridge::~GuiBridge() {
    cancel();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (model_load_thread_.joinable()) {
        model_load_thread_.join();
    }
}

void GuiBridge::setCurrentModel(const QString& model) {
    if (current_model_ != model) {
        current_model_ = model;
        emit currentModelChanged();
    }
}

void GuiBridge::setCurrentDevice(const QString& device) {
    if (current_device_ != device) {
        current_device_ = device;
        emit currentDeviceChanged();
    }
}

void GuiBridge::setModelsPath(const QString& path) {
    if (models_path_ != path) {
        models_path_ = path;
        emit modelsPathChanged();
    }
}

void GuiBridge::scanModels(const QString& directoryPath) {
    setModelsPath(directoryPath);
    if (!backend_) return;

    std::string path_str = directoryPath.toStdString();
    backend_->scan_models(path_str);
    auto mlist = backend_->list_models();

    QVariantList list;
    for (const auto& m : mlist) {
        QVariantMap map;
        map["id"] = QString::fromStdString(m.id);
        map["name"] = QString::fromStdString(m.name);
        map["path"] = QString::fromStdString(m.id);
        map["device"] = QString::fromStdString(m.device);
        map["isLoaded"] = m.is_loaded;
        map["badgeInfo"] = QString::fromStdString(m.badge_info);
        map["architecture"] = QString::fromStdString(m.architecture);
        map["quantization"] = QString::fromStdString(m.quantization);
        map["contextWindow"] = static_cast<qulonglong>(m.context_window);
        map["defaultTemperature"] = m.default_temperature;
        list.append(map);
    }
    available_models_ = list;
    emit availableModelsChanged();
    emit modelConfigChanged();
}

QString GuiBridge::currentModelInfo() const {
    for (const auto& item : available_models_) {
        QVariantMap m = item.toMap();
        if (m["path"].toString() == current_model_ || m["name"].toString() == current_model_ || m["id"].toString() == current_model_) {
            QString b = m["badgeInfo"].toString();
            if (!b.isEmpty()) return b;
        }
    }
    if (!available_models_.isEmpty()) {
        QString b = available_models_.first().toMap()["badgeInfo"].toString();
        if (!b.isEmpty()) return b;
    }
    return "OpenVINO Model • Generic Transformer";
}

QString GuiBridge::chooseModelFolder(const QString& currentPath) {
#ifdef _WIN32
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    QString chosenPath;

    if (SUCCEEDED(hr)) {
        FILEOPENDIALOGOPTIONS opt;
        pFileOpen->GetOptions(&opt);
        pFileOpen->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        pFileOpen->SetTitle(L"Select OpenVINO Models Directory");

        QString initPath = currentPath.isEmpty() ? models_path_ : currentPath;
        if (!initPath.isEmpty()) {
            std::wstring winit = initPath.toStdWString();
            for (auto& c : winit) { if (c == L'/') c = L'\\'; }
            IShellItem* pFolder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(winit.c_str(), NULL, IID_PPV_ARGS(&pFolder)))) {
                pFileOpen->SetFolder(pFolder);
                pFolder->Release();
            }
        }

        if (SUCCEEDED(pFileOpen->Show(NULL))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    chosenPath = QString::fromWCharArray(pszFilePath);
                    chosenPath.replace('\\', '/');
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }

    if (!chosenPath.isEmpty()) {
        scanModels(chosenPath);
    }
    return chosenPath;
#else
    (void)currentPath;
    return "";
#endif
}

bool GuiBridge::loadModel(const QString& modelPath, const QString& device) {
    if (!backend_) return false;
    std::string path_str = modelPath.toStdString();
    std::string dev_str = device.toStdString();

    bool success = backend_->load_model(path_str, dev_str);
    if (success) {
        current_model_ = modelPath;
        current_device_ = device;
        model_load_status_ = "Ready";
        emit currentModelChanged();
        emit currentDeviceChanged();
        emit modelLoadStatusChanged();
        scanModels(models_path_);
    } else {
        last_error_ = "Failed to load model: " + modelPath;
        model_load_status_ = "Error";
        emit errorChanged();
        emit modelLoadStatusChanged();
    }
    return success;
}

void GuiBridge::loadModelAsync(const QString& modelPath, const QString& device) {
    if (is_model_loading_) return;
    if (model_load_thread_.joinable()) {
        model_load_thread_.join();
    }

    is_model_loading_ = true;
    model_load_status_ = "Loading model...";
    emit isModelLoadingChanged();
    emit modelLoadStatusChanged();

    model_load_thread_ = std::thread([this, modelPath, device]() {
        bool ok = backend_->load_model(modelPath.toStdString(), device.toStdString());

        QMetaObject::invokeMethod(this, [this, ok, modelPath, device]() {
            this->is_model_loading_ = false;
            if (ok) {
                this->current_model_ = modelPath;
                this->current_device_ = device;
                this->model_load_status_ = "Ready";
                emit currentModelChanged();
                emit currentDeviceChanged();
                this->applyModelConfig(modelPath);
                scanModels(this->models_path_);
            } else {
                this->model_load_status_ = "Error loading model";
                this->last_error_ = "Failed to load model from: " + modelPath;
                emit errorChanged();
            }
            emit isModelLoadingChanged();
            emit modelLoadStatusChanged();
        }, Qt::QueuedConnection);
    });
}

void GuiBridge::setEnableNvmeOpt(bool v) {
    if (enable_nvme_opt_ != v) {
        enable_nvme_opt_ = v;
        enable_mmap_ = v;
        enable_cache_ = v;
        emit nvmeSettingsChanged();
    }
}

void GuiBridge::setEnableMmap(bool v) {
    if (enable_mmap_ != v) {
        enable_mmap_ = v;
        emit nvmeSettingsChanged();
    }
}

void GuiBridge::setEnableCache(bool v) {
    if (enable_cache_ != v) {
        enable_cache_ = v;
        emit nvmeSettingsChanged();
    }
}

void GuiBridge::setCacheDir(const QString& v) {
    if (cache_dir_ != v) {
        cache_dir_ = v;
        emit nvmeSettingsChanged();
        refreshStorageInfo();
    }
}

void GuiBridge::refreshStorageInfo() {
    if (!backend_) return;
    StorageHardwareInfo hw = backend_->query_storage_hardware(models_path_.toStdString());
    is_nvme_storage_ = hw.is_nvme;
    is_ssd_storage_ = hw.is_ssd;
    storage_bus_type_ = QString::fromStdString(hw.bus_type);
    storage_device_name_ = QString::fromStdString(hw.device_name.empty() ? "Generic Drive" : hw.device_name);

    if (hw.total_bytes > 0) {
        uint64_t cap_gb = hw.total_bytes / (1024ULL * 1024ULL * 1024ULL);
        uint64_t free_gb = hw.free_bytes / (1024ULL * 1024ULL * 1024ULL);
        storage_capacity_text_ = QString("%1 GB (%2 GB free)").arg(cap_gb).arg(free_gb);
    } else {
        storage_capacity_text_ = "Unknown Capacity";
    }

    uint64_t c_size = backend_->get_cache_size(cache_dir_.toStdString());
    if (c_size >= 1024ULL * 1024ULL * 1024ULL) {
        double gb = static_cast<double>(c_size) / (1024.0 * 1024.0 * 1024.0);
        cache_size_text_ = QString("%1 GB").arg(gb, 0, 'f', 2);
    } else {
        double mb = static_cast<double>(c_size) / (1024.0 * 1024.0);
        cache_size_text_ = QString("%1 MB").arg(mb, 0, 'f', 1);
    }

    emit storageInfoChanged();
    emit cacheSizeChanged();
}

void GuiBridge::clearCache() {
    if (!backend_) return;
    backend_->clear_cache(cache_dir_.toStdString());
    refreshStorageInfo();
}

void GuiBridge::applyModelConfig(const QString& modelPath) {
    if (!backend_ || modelPath.isEmpty()) return;
    VinoxModelConfig cfg = backend_->get_model_config(modelPath.toStdString());
    this->temperature_ = cfg.temperature;
    this->top_p_ = cfg.top_p;
    this->repetition_penalty_ = cfg.repetition_penalty;
    this->max_tokens_ = cfg.max_tokens;
    this->has_custom_model_config_ = cfg.is_custom;
    this->enable_mmap_ = cfg.enable_mmap;
    this->enable_cache_ = cfg.enable_cache;
    if (!cfg.cache_dir.empty()) {
        this->cache_dir_ = QString::fromStdString(cfg.cache_dir);
    }
    if (!cfg.preferred_device.empty() && cfg.is_custom) {
        this->current_device_ = QString::fromStdString(cfg.preferred_device);
        emit currentDeviceChanged();
    }
    emit samplingChanged();
    emit nvmeSettingsChanged();
    emit modelConfigChanged();
    refreshStorageInfo();
}

bool GuiBridge::saveCurrentModelConfig() {
    if (!backend_ || current_model_.isEmpty()) return false;
    VinoxModelConfig cfg{};
    cfg.temperature = this->temperature_;
    cfg.top_p = this->top_p_;
    cfg.repetition_penalty = this->repetition_penalty_;
    cfg.max_tokens = this->max_tokens_;
    cfg.preferred_device = this->current_device_.toStdString();
    cfg.enable_mmap = this->enable_mmap_;
    cfg.enable_cache = this->enable_cache_;
    cfg.cache_dir = this->cache_dir_.toStdString();
    cfg.is_custom = true;
    bool ok = backend_->save_model_config(current_model_.toStdString(), cfg);
    if (ok) {
        this->has_custom_model_config_ = true;
        emit modelConfigChanged();
    }
    return ok;
}

bool GuiBridge::resetCurrentModelConfig() {
    if (!backend_ || current_model_.isEmpty()) return false;
    bool ok = backend_->reset_model_config(current_model_.toStdString());
    applyModelConfig(current_model_);
    return ok;
}

void GuiBridge::sendMessage(const QString& userText) {
    if (is_generating_) {
        return;
    }
    if (!backend_) {
        last_error_ = "No backend initialized";
        emit errorChanged();
        return;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    // Auto-create conversation if none is active
    if (current_conversation_id_.isEmpty()) {
        QString title = userText.trimmed();
        if (title.length() > 36) {
            title = title.left(33) + "...";
        }
        current_conversation_id_ = createConversation(title);
        emit currentConversationIdChanged();
        refreshConversations();
    }

    // Add user message to local history
    chat_history_.push_back(ChatMessage{"user", userText.toStdString(), ""});

    is_generating_ = true;
    emit isGeneratingChanged();
    emit messageStarted("assistant");

    double temp = this->temperature_;
    double top_p = this->top_p_;
    uint64_t max_tok = static_cast<uint64_t>(this->max_tokens_);
    std::string conv_id = current_conversation_id_.toStdString();

    worker_thread_ = std::thread([this, temp, top_p, max_tok, conv_id]() {
        auto start_time = std::chrono::steady_clock::now();
        uint64_t generated_count = 0;
        std::string accumulated_assistant;
        std::string accumulated_reasoning;
        std::string out_error;

        auto last_stat_publish = start_time;

        auto token_cb = [this, &generated_count, &start_time, &last_stat_publish, &accumulated_assistant, &accumulated_reasoning]
                        (const std::string& chunk, bool is_reasoning) {
            generated_count++;
            this->total_tokens_++;
            if (is_reasoning) {
                accumulated_reasoning += chunk;
            } else {
                accumulated_assistant += chunk;
            }

            auto now = std::chrono::steady_clock::now();
            double elapsed_sec = std::chrono::duration<double>(now - start_time).count();
            if (elapsed_sec > 0.05) {
                this->token_rate_ = static_cast<double>(generated_count) / elapsed_sec;
            }

            bool should_publish_stats = false;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stat_publish).count() >= 250) {
                should_publish_stats = true;
                last_stat_publish = now;
            }

            QString qchunk = QString::fromUtf8(chunk.data(), static_cast<int>(chunk.size()));
            QMetaObject::invokeMethod(this, [this, qchunk, is_reasoning, should_publish_stats]() {
                emit tokenReceived(qchunk, is_reasoning);
                if (should_publish_stats) {
                    emit statsChanged();
                }
            }, Qt::QueuedConnection);
        };

        bool ok = backend_->generate_chat_stream(
            chat_history_,
            static_cast<float>(temp),
            static_cast<float>(top_p),
            max_tok,
            token_cb,
            out_error,
            conv_id
        );

        if (ok) {
            chat_history_.push_back(ChatMessage{"assistant", accumulated_assistant, accumulated_reasoning});
        }

        this->is_generating_ = false;
        QString qerr = QString::fromStdString(out_error);

        QMetaObject::invokeMethod(this, [this, ok, qerr]() {
            emit statsChanged();
            emit isGeneratingChanged();
            emit generationFinished(ok, qerr);
            if (!ok && !qerr.isEmpty()) {
                this->last_error_ = qerr;
                emit errorChanged();
            }
            refreshConversations();
        }, Qt::QueuedConnection);
    });
}

void GuiBridge::cancel() {
    if (is_generating_ && backend_) {
        backend_->cancel_generation();
    }
}

void GuiBridge::clearChat() {
    startNewChat();
}

void GuiBridge::startNewChat() {
    cancel();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    current_conversation_id_ = "";
    emit currentConversationIdChanged();
    chat_history_.clear();
    emit conversationLoaded(QVariantList{});
}

void GuiBridge::selectConversation(const QString& conversationId) {
    cancel();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    current_conversation_id_ = conversationId;
    emit currentConversationIdChanged();

    if (!backend_ || conversationId.isEmpty()) {
        chat_history_.clear();
        emit conversationLoaded(QVariantList{});
        return;
    }

    auto msgs = backend_->get_conversation_messages(conversationId.toStdString());
    chat_history_ = msgs;

    QVariantList qmsgs;
    for (const auto& m : msgs) {
        QVariantMap map;
        map["role"] = QString::fromStdString(m.role);
        map["text"] = QString::fromStdString(m.content);
        map["reasoning"] = QString::fromStdString(m.reasoning_content);
        qmsgs.append(map);
    }
    emit conversationLoaded(qmsgs);
}

bool GuiBridge::deleteConversation(const QString& conversationId) {
    if (!backend_ || conversationId.isEmpty()) return false;
    bool ok = backend_->delete_conversation(conversationId.toStdString());
    if (current_conversation_id_ == conversationId) {
        startNewChat();
    }
    refreshConversations();
    return ok;
}

void GuiBridge::refreshConversations() {
    if (!backend_) return;
    auto convs = backend_->list_conversations_detailed();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    QVariantList list;
    for (const auto& c : convs) {
        QVariantMap map;
        map["id"] = QString::fromStdString(c.id);
        map["title"] = QString::fromStdString(c.title);
        map["createdAt"] = static_cast<qint64>(c.created_at_ms);
        map["updatedAt"] = static_cast<qint64>(c.updated_at_ms);
        map["messageCount"] = static_cast<qint64>(c.message_count);

        QString time_str = "gerade eben";
        uint64_t ts = (c.updated_at_ms > 0) ? c.updated_at_ms : c.created_at_ms;
        if (ts > 0 && now_ms >= static_cast<int64_t>(ts)) {
            int64_t diff_s = (now_ms - static_cast<int64_t>(ts)) / 1000;
            if (diff_s < 60) {
                time_str = "gerade eben";
            } else if (diff_s < 3600) {
                time_str = QString("vor %1m").arg(diff_s / 60);
            } else if (diff_s < 86400) {
                time_str = QString("vor %1h").arg(diff_s / 3600);
            } else {
                time_str = QString("vor %1d").arg(diff_s / 86400);
            }
        }
        map["timeText"] = time_str;
        list.append(map);
    }
    conversations_ = list;
    emit conversationsChanged();
}

QVariantList GuiBridge::listConversations() {
    QVariantList list;
    if (!backend_) return list;
    auto convs = backend_->list_conversations();
    for (const auto& c : convs) {
        QVariantMap map;
        map["id"] = QString::fromStdString(c.first);
        map["title"] = QString::fromStdString(c.second);
        list.append(map);
    }
    return list;
}

QString GuiBridge::createConversation(const QString& title) {
    if (!backend_) return "";
    std::string cid = backend_->create_conversation(title.toStdString());
    return QString::fromStdString(cid);
}

bool GuiBridge::loadEmbeddingModel(const QString& modelPath, const QString& device) {
    if (!backend_) return false;
    bool ok = backend_->load_embedding_model(modelPath.toStdString(), device.toStdString());
    if (ok) {
        embedding_model_ = modelPath;
        embedding_device_ = device;
        is_embedding_loaded_ = true;
        embedding_status_text_ = QString("Loaded on %1").arg(device);
        emit embeddingSettingsChanged();
        return true;
    } else {
        is_embedding_loaded_ = false;
        embedding_status_text_ = "Failed to load embedding model";
        emit embeddingSettingsChanged();
        return false;
    }
}

QVariantList GuiBridge::searchHybrid(const QString& query, double alpha, int limit) {
    QVariantList list;
    if (!backend_ || query.trimmed().isEmpty()) return list;
    auto matches = backend_->search_hybrid(query.toStdString(), static_cast<float>(alpha), static_cast<uint32_t>(limit));
    for (const auto& m : matches) {
        QVariantMap item;
        item["documentId"] = QString::fromStdString(m.id);
        item["title"] = QString::fromStdString(m.title);
        item["snippet"] = QString::fromStdString(m.snippet);
        item["score"] = m.score;
        item["matchType"] = QString::fromStdString(m.match_type);
        list.append(item);
    }
    return list;
}

bool GuiBridge::ingestDocument(const QString& title, const QString& content) {
    if (!backend_ || title.trimmed().isEmpty() || content.trimmed().isEmpty()) return false;
    return backend_->ingest_document(title.toStdString(), content.toStdString());
}

QVariantList GuiBridge::getRelations(const QString& sourceId) {
    QVariantList list;
    if (!backend_ || sourceId.trimmed().isEmpty()) return list;
    auto rels = backend_->get_relations(sourceId.toStdString());
    for (const auto& r : rels) {
        QVariantMap item;
        item["sourceId"] = QString::fromStdString(r.source_id);
        item["targetId"] = QString::fromStdString(r.target_id);
        item["type"] = QString::fromStdString(r.relation_type);
        item["depth"] = r.depth;
        list.append(item);
    }
    return list;
}

void GuiBridge::copyToClipboard(const QString& text) {
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(text);
    }
}

} // namespace vinox::gui


