#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <thread>
#include <atomic>

#include "backend_interface.hpp"
#include "viewmodels/chat_viewmodel.hpp"
#include "viewmodels/plan_viewmodel.hpp"
#include "viewmodels/agent_viewmodel.hpp"
#include "viewmodels/storage_viewmodel.hpp"
#include "viewmodels/diff_viewmodel.hpp"

namespace vinox::gui {

class GuiBridge : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isGenerating READ isGenerating NOTIFY isGeneratingChanged)
    Q_PROPERTY(QString currentConversationId READ currentConversationId NOTIFY currentConversationIdChanged)
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    Q_PROPERTY(double tokenRate READ tokenRate NOTIFY statsChanged)
    Q_PROPERTY(int totalTokens READ totalTokens NOTIFY statsChanged)
    Q_PROPERTY(QString currentModel READ currentModel WRITE setCurrentModel NOTIFY currentModelChanged)
    Q_PROPERTY(QString currentDevice READ currentDevice WRITE setCurrentDevice NOTIFY currentDeviceChanged)
    Q_PROPERTY(QString connectionMode READ connectionMode NOTIFY connectionModeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString modelsPath READ modelsPath WRITE setModelsPath NOTIFY modelsPathChanged)
    Q_PROPERTY(QVariantList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(bool isModelLoading READ isModelLoading NOTIFY isModelLoadingChanged)
    Q_PROPERTY(QString modelLoadStatus READ modelLoadStatus NOTIFY modelLoadStatusChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY samplingChanged)
    Q_PROPERTY(double topP READ topP WRITE setTopP NOTIFY samplingChanged)
    Q_PROPERTY(double repetitionPenalty READ repetitionPenalty WRITE setRepetitionPenalty NOTIFY samplingChanged)
    Q_PROPERTY(int maxTokens READ maxTokens WRITE setMaxTokens NOTIFY samplingChanged)
    Q_PROPERTY(bool hasCustomModelConfig READ hasCustomModelConfig NOTIFY modelConfigChanged)
    Q_PROPERTY(QString currentModelInfo READ currentModelInfo NOTIFY modelConfigChanged)
    Q_PROPERTY(bool isNvmeStorage READ isNvmeStorage NOTIFY storageInfoChanged)
    Q_PROPERTY(bool isSsdStorage READ isSsdStorage NOTIFY storageInfoChanged)
    Q_PROPERTY(QString storageBusType READ storageBusType NOTIFY storageInfoChanged)
    Q_PROPERTY(QString storageDeviceName READ storageDeviceName NOTIFY storageInfoChanged)
    Q_PROPERTY(QString storageCapacityText READ storageCapacityText NOTIFY storageInfoChanged)
    Q_PROPERTY(bool enableNvmeOpt READ enableNvmeOpt WRITE setEnableNvmeOpt NOTIFY nvmeSettingsChanged)
    Q_PROPERTY(bool enableMmap READ enableMmap WRITE setEnableMmap NOTIFY nvmeSettingsChanged)
    Q_PROPERTY(bool enableCache READ enableCache WRITE setEnableCache NOTIFY nvmeSettingsChanged)
    Q_PROPERTY(QString cacheDir READ cacheDir WRITE setCacheDir NOTIFY nvmeSettingsChanged)
    Q_PROPERTY(QString cacheSizeText READ cacheSizeText NOTIFY cacheSizeChanged)
    Q_PROPERTY(bool enableToolCalling READ enableToolCalling WRITE setEnableToolCalling NOTIFY toolSettingsChanged)
    Q_PROPERTY(bool enableStructuredOutput READ enableStructuredOutput WRITE setEnableStructuredOutput NOTIFY toolSettingsChanged)
    Q_PROPERTY(int toolSecurityTier READ toolSecurityTier WRITE setToolSecurityTier NOTIFY toolSettingsChanged)
    Q_PROPERTY(int toolsCount READ toolsCount NOTIFY toolsInfoChanged)
    Q_PROPERTY(QString toolsSummaryText READ toolsSummaryText NOTIFY toolsInfoChanged)
    Q_PROPERTY(QString embeddingModel READ embeddingModel WRITE setEmbeddingModel NOTIFY embeddingSettingsChanged)
    Q_PROPERTY(QString embeddingDevice READ embeddingDevice WRITE setEmbeddingDevice NOTIFY embeddingSettingsChanged)
    Q_PROPERTY(bool isEmbeddingLoaded READ isEmbeddingLoaded NOTIFY embeddingSettingsChanged)
    Q_PROPERTY(QString embeddingStatusText READ embeddingStatusText NOTIFY embeddingSettingsChanged)

public:
    explicit GuiBridge(std::shared_ptr<IVinoxBackend> backend, bool is_remote = false, QObject* parent = nullptr);
    ~GuiBridge() override;

    bool isGenerating() const { return is_generating_; }
    QString currentConversationId() const { return current_conversation_id_; }
    QVariantList conversations() const { return conversations_; }
    double tokenRate() const { return token_rate_; }
    int totalTokens() const { return static_cast<int>(total_tokens_); }
    QString currentModel() const { return current_model_; }
    void setCurrentModel(const QString& model);
    QString currentDevice() const { return current_device_; }
    void setCurrentDevice(const QString& device);
    QString connectionMode() const { return is_remote_ ? "Remote (127.0.0.1:8080)" : "Local (C-ABI)"; }
    QString lastError() const { return last_error_; }

    QString modelsPath() const { return models_path_; }
    void setModelsPath(const QString& path);
    QVariantList availableModels() const { return available_models_; }
    bool isModelLoading() const { return is_model_loading_; }
    QString modelLoadStatus() const { return model_load_status_; }
    bool hasCustomModelConfig() const { return has_custom_model_config_; }
    QString currentModelInfo() const;

    double temperature() const { return temperature_; }
    void setTemperature(double v) { if (temperature_ != v) { temperature_ = v; emit samplingChanged(); } }
    double topP() const { return top_p_; }
    void setTopP(double v) { if (top_p_ != v) { top_p_ = v; emit samplingChanged(); } }
    double repetitionPenalty() const { return repetition_penalty_; }
    void setRepetitionPenalty(double v) { if (repetition_penalty_ != v) { repetition_penalty_ = v; emit samplingChanged(); } }
    int maxTokens() const { return max_tokens_; }
    void setMaxTokens(int v) { if (max_tokens_ != v) { max_tokens_ = v; emit samplingChanged(); } }

    bool isNvmeStorage() const { return is_nvme_storage_; }
    bool isSsdStorage() const { return is_ssd_storage_; }
    QString storageBusType() const { return storage_bus_type_; }
    QString storageDeviceName() const { return storage_device_name_; }
    QString storageCapacityText() const { return storage_capacity_text_; }

    bool enableNvmeOpt() const { return enable_nvme_opt_; }
    void setEnableNvmeOpt(bool v);
    bool enableMmap() const { return enable_mmap_; }
    void setEnableMmap(bool v);
    bool enableCache() const { return enable_cache_; }
    void setEnableCache(bool v);
    QString cacheDir() const { return cache_dir_; }
    void setCacheDir(const QString& v);
    QString cacheSizeText() const { return cache_size_text_; }

    bool enableToolCalling() const { return enable_tool_calling_; }
    void setEnableToolCalling(bool v) { if (enable_tool_calling_ != v) { enable_tool_calling_ = v; emit toolSettingsChanged(); } }
    bool enableStructuredOutput() const { return enable_structured_output_; }
    void setEnableStructuredOutput(bool v) { if (enable_structured_output_ != v) { enable_structured_output_ = v; emit toolSettingsChanged(); } }
    int toolSecurityTier() const { return tool_security_tier_; }
    void setToolSecurityTier(int v) { if (tool_security_tier_ != v) { tool_security_tier_ = v; emit toolSettingsChanged(); } }
    int toolsCount() const { return 5; }
    QString toolsSummaryText() const { return "5 canonical tools: vinox.search, conversation_get, document_ingest, relations_query, relation_create"; }

    QString embeddingModel() const { return embedding_model_; }
    void setEmbeddingModel(const QString& v) { if (embedding_model_ != v) { embedding_model_ = v; emit embeddingSettingsChanged(); } }
    QString embeddingDevice() const { return embedding_device_; }
    void setEmbeddingDevice(const QString& v) { if (embedding_device_ != v) { embedding_device_ = v; emit embeddingSettingsChanged(); } }
    bool isEmbeddingLoaded() const { return is_embedding_loaded_; }
    QString embeddingStatusText() const { return embedding_status_text_; }

    Q_INVOKABLE void sendMessage(const QString& userText);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearChat();
    Q_INVOKABLE bool loadModel(const QString& modelPath, const QString& device = "CPU");
    Q_INVOKABLE void loadModelAsync(const QString& modelPath, const QString& device = "CPU");
    Q_INVOKABLE bool loadEmbeddingModel(const QString& modelPath, const QString& device = "CPU");
    Q_INVOKABLE void scanModels(const QString& directoryPath);
    Q_INVOKABLE QString chooseModelFolder(const QString& currentFolder = "");
    Q_INVOKABLE bool saveCurrentModelConfig();
    Q_INVOKABLE bool resetCurrentModelConfig();
    Q_INVOKABLE void applyModelConfig(const QString& modelPath);
    Q_INVOKABLE void refreshStorageInfo();
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE QVariantList listConversations();
    Q_INVOKABLE void refreshConversations();
    Q_INVOKABLE void selectConversation(const QString& conversationId);
    Q_INVOKABLE void startNewChat();
    Q_INVOKABLE bool deleteConversation(const QString& conversationId);
    Q_INVOKABLE QString createConversation(const QString& title);
    Q_INVOKABLE QVariantList searchHybrid(const QString& query, double alpha = 0.5, int limit = 10);
    Q_INVOKABLE bool ingestDocument(const QString& title, const QString& content);
    Q_INVOKABLE QVariantList getRelations(const QString& sourceId);
    Q_INVOKABLE void copyToClipboard(const QString& text);

signals:
    void isGeneratingChanged();
    void currentConversationIdChanged();
    void conversationsChanged();
    void conversationLoaded(const QVariantList& messages);
    void statsChanged();
    void currentModelChanged();
    void currentDeviceChanged();
    void connectionModeChanged();
    void errorChanged();
    void modelsPathChanged();
    void availableModelsChanged();
    void isModelLoadingChanged();
    void modelLoadStatusChanged();
    void samplingChanged();
    void modelConfigChanged();
    void storageInfoChanged();
    void nvmeSettingsChanged();
    void cacheSizeChanged();
    void toolSettingsChanged();
    void toolsInfoChanged();
    void embeddingSettingsChanged();

    // Streaming signals for QML
    void tokenReceived(const QString& chunk, bool isReasoning);
    void messageStarted(const QString& role);
    void generationFinished(bool success, const QString& errorMessage);

private:
    std::shared_ptr<IVinoxBackend> backend_;
    bool is_remote_{false};

    std::atomic<bool> is_generating_{false};
    double token_rate_{0.0};
    uint64_t total_tokens_{0};
    QString current_model_{"ov_deepseek_tools_v4"};
    QString current_device_{"NPU"};
    QString last_error_;

    double temperature_{0.7};
    double top_p_{0.9};
    double repetition_penalty_{1.15};
    int max_tokens_{512};

    bool is_nvme_storage_{false};
    bool is_ssd_storage_{false};
    QString storage_bus_type_{"Unknown"};
    QString storage_device_name_{"Detecting..."};
    QString storage_capacity_text_;
    bool enable_nvme_opt_{true};
    bool enable_mmap_{true};
    bool enable_cache_{true};
    QString cache_dir_{"C:\\ai\\openvino\\cache\\blobs"};
    QString cache_size_text_{"0 MB"};

    bool enable_tool_calling_{true};
    bool enable_structured_output_{true};
    int tool_security_tier_{0};

    QString embedding_model_{"C:/ai/models/OpenVINO/Qwen3-Embedding-0.6B"};
    QString embedding_device_{"CPU"};
    bool is_embedding_loaded_{false};
    QString embedding_status_text_{"Unloaded"};

    std::vector<ChatMessage> chat_history_;
    QString current_conversation_id_;
    QVariantList conversations_;
    std::thread worker_thread_;
    std::thread model_load_thread_;

    QString models_path_{"C:/ai/models/OpenVINO"};
    QVariantList available_models_;
    std::atomic<bool> is_model_loading_{false};
    QString model_load_status_{"Ready"};
    bool has_custom_model_config_{false};
};

} // namespace vinox::gui
