#pragma once

#include <memory>

#include <JuceHeader.h>
#include <ixwebsocket/IXWebSocket.h>

#include "Import/ImporterRegistry.h"
#include "engine/world.h"

namespace ce::scene { class ObjectDefinitionCatalog; }
namespace creation::assets { class ProjectSession; }

namespace ce {

class ViewportComponent;

// Djehuti Bridge engine-side handoff: while a project is open in the
// running editor, listens for an external tool (the Blender add-on)
// dropping a file into that project's VFS "Imports/" folder --
// services/VfsService's existing entryChanged WebSocket broadcast -- pulls
// it in via ProjectSession (already an HTTP client of that same VfsService,
// see ProjectSession.h's own doc comment), and runs it through the SAME
// ImporterRegistry/AssetImporter path drag-and-drop already uses. Reuses
// the existing pipeline end to end; adds no new HTTP endpoints.
//
// Format is a hard gate (only glTF/GLB accepted, per DjehutiExportContract);
// up-axis/units are advisory only, never checked here. A dropped file whose
// glTF asset.extras carries a previously-assigned "djehuti_asset_id"
// (GltfLoader.h's LoadedModel::djehutiAssetId) found in the project's asset
// catalog is Reimport()'d onto that existing asset instead of creating a
// duplicate; otherwise it's a fresh Import(). The result -- {ok, assetId,
// error} -- is written back into the same VFS entries the request came in
// through, at "Imports/.results/<name>.json", for the external tool to poll.
class DjehutiImportWatcher final : private juce::AsyncUpdater {
public:
    DjehutiImportWatcher(engine::World& world, ViewportComponent& viewport,
                         scene::ObjectDefinitionCatalog& objectDefinitions);
    ~DjehutiImportWatcher() override;

    // Called the same places/times ImportPanel::SetProjectContent already is
    // (MainComponent's project-open paths). session == nullptr stops
    // watching (project closed). Also ensures the suite-level default
    // export contract file exists, using the same VfsService connection
    // this discovers for its own WebSocket subscription.
    void SetProjectContent(creation::assets::ProjectSession* session, const juce::String& projectId);

    // Same "here's what just happened" convention as ImportPanel::onLogLine
    // -- this class has no visible surface of its own either.
    std::function<void(const juce::String&)> onLogLine;

private:
    void handleAsyncUpdate() override; // juce::AsyncUpdater -- runs on the message thread.
    void ProcessImportFile(const juce::String& logicalPath);
    void WriteResult(const juce::String& originalName, bool ok, const juce::String& assetIdOrError);

    import::ImporterRegistry registry_;
    engine::World* world_ = nullptr;
    ViewportComponent* viewport_ = nullptr;
    scene::ObjectDefinitionCatalog* objectDefinitions_ = nullptr;
    creation::assets::ProjectSession* projectSession_ = nullptr;
    juce::String projectId_;

    std::unique_ptr<ix::WebSocket> socket_;

    // Populated on ix's own background thread (setOnMessageCallback), drained
    // on the message thread by handleAsyncUpdate -- ProjectSession/AssetCatalog/
    // the importer registry are not safe to touch off the message thread.
    juce::CriticalSection pendingPathsLock_;
    juce::StringArray pendingPaths_;
};

} // namespace ce
