#include "Import/DjehutiImportWatcher.h"

#include <optional>

#include <creation/assets/AssetCatalog.h>
#include <creation/assets/DjehutiExportContract.h>
#include <creation/assets/ProjectManifest.h>
#include <creation/assets/ProjectSession.h>
#include <creation/services/SuiteProcessRegistry.h>
#include <creation/services/SuiteVfsServiceClient.h>

#include "Import/AssetImporter.h"
#include "Render/Import/GltfLoader.h"
#include "Render/ViewportComponent.h"
#include "Scene/ObjectDefinitions.h"

namespace ce {
namespace {

constexpr const char* kVfsServiceAppId = "CreationSuiteVfsService";

std::optional<int> DiscoverVfsHttpPort() {
    for (const auto& record : creation::services::SuiteProcessRegistry::EnumerateLiveProcesses()) {
        if (record.appId == kVfsServiceAppId && record.httpPort > 0) return record.httpPort;
    }
    return std::nullopt;
}

// Only glTF/GLB is accepted (the format hard-gate) -- this is the one place
// that decision is made, deliberately not delegated to ImporterRegistry's
// generic extension match, since a future importer format registered there
// for some OTHER reason should not silently become droppable here too.
bool IsAcceptedImportExtension(const juce::File& file) {
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".gltf" || ext == ".glb";
}

} // namespace

DjehutiImportWatcher::DjehutiImportWatcher(engine::World& world, ViewportComponent& viewport,
                                           scene::ObjectDefinitionCatalog& objectDefinitions)
    : world_(&world), viewport_(&viewport), objectDefinitions_(&objectDefinitions) {
    registry_.RegisterBuiltins();
}

DjehutiImportWatcher::~DjehutiImportWatcher() {
    if (socket_) socket_->stop();
}

void DjehutiImportWatcher::SetProjectContent(creation::assets::ProjectSession* session, const juce::String& projectId) {
    projectSession_ = session;
    projectId_ = projectId;

    if (socket_) {
        socket_->stop();
        socket_.reset();
    }
    if (session == nullptr) return;

    const auto httpPort = DiscoverVfsHttpPort();
    if (!httpPort) {
        if (onLogLine) onLogLine("Djehuti Bridge: VFS service not found; import watching disabled for this session.");
        return;
    }

    // Ensure the global export contract default exists, using its own
    // freshly-discovered connection -- independent of the WS subscription
    // below, but the same discovery mechanism.
    creation::services::SuiteVfsServiceClient client;
    if (client.discover(2000)) {
        juce::String contractError;
        creation::assets::EnsureDjehutiExportContractDefaultExists(client, contractError);
        if (contractError.isNotEmpty() && onLogLine) onLogLine("Djehuti Bridge: " + contractError);
    }

    socket_ = std::make_unique<ix::WebSocket>();
    socket_->setUrl(("ws://127.0.0.1:" + juce::String(*httpPort + 1)).toStdString());
    socket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
        if (message->type != ix::WebSocketMessageType::Message) return;
        const auto parsed = juce::JSON::parse(juce::String(message->str));
        const auto* object = parsed.getDynamicObject();
        if (object == nullptr || object->getProperty("event").toString() != "entryChanged") return;

        // VfsService's broadcastEntryChanged encodes a PROJECT-scoped
        // entry's path as "<projectId>:<path>" (Main.cpp's own
        // "projectId + \":\" + path" call) -- a bare logical path alone
        // can't distinguish which project changed, so this must split on
        // the first ':' rather than compare the raw field directly.
        const auto combined = object->getProperty("path").toString();
        const auto separator = combined.indexOfChar(':');
        if (separator < 0) return; // A suite-level (non-project) entry -- not for us.
        const auto eventProjectId = combined.substring(0, separator);
        const auto path = combined.substring(separator + 1);
        if (eventProjectId != projectId_) return; // A different project's change.
        if (!path.startsWith(creation::assets::ProjectContainerPaths::importsRoot)) return;
        if (path.contains("/.results/")) return; // Don't react to our own result writes.

        const juce::ScopedLock lock(pendingPathsLock_);
        pendingPaths_.add(path);
        triggerAsyncUpdate();
    });
    socket_->start();
}

void DjehutiImportWatcher::handleAsyncUpdate() {
    juce::StringArray paths;
    {
        const juce::ScopedLock lock(pendingPathsLock_);
        paths.swapWith(pendingPaths_);
    }
    for (const auto& path : paths) ProcessImportFile(path);
}

void DjehutiImportWatcher::ProcessImportFile(const juce::String& logicalPath) {
    if (projectSession_ == nullptr) return;
    const auto originalName = juce::File(logicalPath).getFileName();

    juce::MemoryBlock data;
    if (!projectSession_->readEntry(logicalPath, data)) {
        WriteResult(originalName, false, "Could not read the dropped file back from the project VFS.");
        return;
    }

    const auto tempFile = juce::File::createTempFile(juce::File(logicalPath).getFileExtension());
    if (!tempFile.replaceWithData(data.getData(), data.getSize())) {
        WriteResult(originalName, false, "Could not stage the dropped file to a temp location.");
        return;
    }

    if (!IsAcceptedImportExtension(tempFile)) {
        WriteResult(originalName, false, "Unsupported format -- only glTF/GLB are accepted.");
        tempFile.deleteFile();
        return;
    }
    import::AssetImporter* importer = registry_.FindFor(tempFile);
    if (importer == nullptr) {
        WriteResult(originalName, false, "No registered importer for this file.");
        tempFile.deleteFile();
        return;
    }

    // Peek the stable id out of the glTF's own extras, separate from the
    // real load the importer itself performs below -- a small, deliberate
    // double-parse rather than threading a glTF-specific field through the
    // generic AssetImporter/ImportResult interface every future importer
    // would then also carry.
    LoadedModel peekedModel;
    juce::String djehutiAssetId;
    if (LoadGltf(tempFile, peekedModel)) djehutiAssetId = peekedModel.djehutiAssetId;

    const creation::assets::AssetDescriptor* existing =
        djehutiAssetId.isNotEmpty() ? projectSession_->getManifest().assetCatalog.findById(djehutiAssetId) : nullptr;

    import::ImportResult result;
    if (existing != nullptr) {
        import::ImportContext context; // Matches ContentBrowserPanel::RunReimport's own pattern -- no `world`, Reimport() never places a scene entity.
        context.catalog = &viewport_->Catalog();
        context.viewport = viewport_;
        context.projectSession = projectSession_;
        context.objectDefinitions = objectDefinitions_;
        result = importer->Reimport(tempFile, *existing, context);
    } else {
        import::ImportContext context; // Matches MainComponent::PlaceStarterContent's pattern -- `world` included, so the new asset actually gets placed.
        context.world = world_;
        context.catalog = &viewport_->Catalog();
        context.viewport = viewport_;
        context.projectSession = projectSession_;
        context.objectDefinitions = objectDefinitions_;
        result = importer->Import(tempFile, context);
    }

    tempFile.deleteFile();
    WriteResult(originalName, result.success, result.success ? result.createdAssetId : result.message);
    if (onLogLine) onLogLine("Djehuti Bridge: " + logicalPath + " -> " + result.message);
}

void DjehutiImportWatcher::WriteResult(const juce::String& originalName, bool ok, const juce::String& assetIdOrError) {
    if (projectSession_ == nullptr) return;

    auto* object = new juce::DynamicObject();
    object->setProperty("ok", ok);
    object->setProperty(ok ? "assetId" : "error", assetIdOrError);
    const auto json = juce::JSON::toString(juce::var(object));

    juce::MemoryBlock data(json.toRawUTF8(), static_cast<size_t>(json.getNumBytesAsUTF8()));
    const juce::String resultPath = juce::String(creation::assets::ProjectContainerPaths::importsRoot) +
                                    ".results/" + originalName + ".json";
    projectSession_->writeEntry(resultPath, data);
}

} // namespace ce
