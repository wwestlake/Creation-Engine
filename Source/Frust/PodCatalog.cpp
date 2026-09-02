#include "Frust/PodCatalog.h"

#include <creation/assets/ProjectAssetService.h>
#include <creation/assets/ProjectManifest.h>
#include <creation/assets/ProjectSession.h>
#include <node_system/frgraph_serialization.h>

namespace ce::frust {

namespace {

node_system::GraphTarget TargetFor(PodKind kind) {
    return kind == PodKind::Processing ? node_system::GraphTarget::Dataflow : node_system::GraphTarget::Behavior;
}

juce::String KindToken(PodKind kind) { return kind == PodKind::Processing ? "processing" : "behavior"; }
PodKind KindFromToken(const juce::String& token) {
    return token.equalsIgnoreCase("processing") ? PodKind::Processing : PodKind::Behavior;
}

juce::String AuthoringModeToken(PodAuthoringMode mode) { return mode == PodAuthoringMode::Source ? "source" : "graph"; }
PodAuthoringMode AuthoringModeFromToken(const juce::String& token) {
    return token.equalsIgnoreCase("source") ? PodAuthoringMode::Source : PodAuthoringMode::Graph;
}

juce::String SlugifyPodName(const juce::String& name) {
    juce::String slug;
    for (auto ch : name) {
        if (juce::CharacterFunctions::isLetterOrDigit(ch)) slug << juce::CharacterFunctions::toLowerCase(ch);
        else if (ch == ' ' || ch == '-' || ch == '_') slug << '-';
    }
    return slug.isEmpty() ? juce::Uuid().toString().replaceCharacter('-', '_') : slug;
}

juce::String LogicalPathFor(const juce::String& name) {
    return juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot) + "Pods/" + SlugifyPodName(name) + ".pod";
}

// Only the FRust-supported subset (matches frust_codegen.cpp's FrustType) --
// the interface editor's type picker is restricted to these too.
juce::String DataTypeToken(node_system::DataType type) {
    switch (type) {
        case node_system::DataType::Float: return "float";
        case node_system::DataType::Bool: return "bool";
        case node_system::DataType::String: return "string";
        case node_system::DataType::Entity: return "entity";
        case node_system::DataType::Transform: return "transform";
        case node_system::DataType::Material: return "material";
        case node_system::DataType::Model: return "model";
        case node_system::DataType::Controller: return "controller";
        default: return "int";
    }
}
node_system::DataType DataTypeFromToken(const juce::String& token) {
    if (token.equalsIgnoreCase("float")) return node_system::DataType::Float;
    if (token.equalsIgnoreCase("bool")) return node_system::DataType::Bool;
    if (token.equalsIgnoreCase("string")) return node_system::DataType::String;
    if (token.equalsIgnoreCase("entity")) return node_system::DataType::Entity;
    if (token.equalsIgnoreCase("transform")) return node_system::DataType::Transform;
    if (token.equalsIgnoreCase("material")) return node_system::DataType::Material;
    if (token.equalsIgnoreCase("model")) return node_system::DataType::Model;
    if (token.equalsIgnoreCase("controller")) return node_system::DataType::Controller;
    return node_system::DataType::Int;
}

}  // namespace

node_system::Graph& PodCatalog::GetOrCreateGraph(const juce::String& name, PodKind kind) {
    return GetOrCreateEntry(name, kind).graph;
}

juce::String& PodCatalog::GetOrCreateSource(const juce::String& name, PodKind kind) {
    auto& entry = GetOrCreateEntry(name, kind);
    entry.authoringMode = PodAuthoringMode::Source;
    return entry.sourceText;
}

PodEntry& PodCatalog::GetOrCreateEntry(const juce::String& name, PodKind kind) {
    const auto key = name.toStdString();
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        PodEntry entry;
        entry.kind = kind;
        entry.graph = node_system::Graph(key, TargetFor(kind));
        it = entries_.emplace(key, std::move(entry)).first;
    }
    return it->second;
}

node_system::Graph* PodCatalog::FindGraph(const juce::String& name) {
    const auto it = entries_.find(name.toStdString());
    return it == entries_.end() ? nullptr : &it->second.graph;
}

PodEntry* PodCatalog::FindEntry(const juce::String& name) {
    const auto it = entries_.find(name.toStdString());
    return it == entries_.end() ? nullptr : &it->second;
}

std::vector<juce::String> PodCatalog::Names(std::optional<PodKind> filterKind) const {
    std::vector<juce::String> result;
    result.reserve(entries_.size());
    for (const auto& [name, entry] : entries_) {
        if (filterKind.has_value() && entry.kind != *filterKind) continue;
        result.push_back(name);
    }
    return result;
}

bool PodCatalog::Remove(const juce::String& name) { return entries_.erase(name.toStdString()) != 0; }

bool PodCatalog::Contains(const juce::String& name) const { return entries_.find(name.toStdString()) != entries_.end(); }

PodKind PodCatalog::Kind(const juce::String& name) const {
    const auto it = entries_.find(name.toStdString());
    return it == entries_.end() ? PodKind::Behavior : it->second.kind;
}

PodAuthoringMode PodCatalog::AuthoringMode(const juce::String& name) const {
    const auto it = entries_.find(name.toStdString());
    return it == entries_.end() ? PodAuthoringMode::Graph : it->second.authoringMode;
}

void PodCatalog::SetCompiledPodPath(const juce::String& name, const juce::String& path) {
    const auto it = entries_.find(name.toStdString());
    if (it != entries_.end()) it->second.compiledPodPath = path;
}

juce::String PodCatalog::CompiledPodPath(const juce::String& name) const {
    const auto it = entries_.find(name.toStdString());
    return it == entries_.end() ? juce::String() : it->second.compiledPodPath;
}

void PodCatalog::SetExposeAsNode(const juce::String& name, bool expose) {
    const auto it = entries_.find(name.toStdString());
    if (it != entries_.end()) it->second.exposeAsNode = expose;
}

bool PodCatalog::ExposeAsNode(const juce::String& name) const {
    const auto it = entries_.find(name.toStdString());
    return it == entries_.end() ? false : it->second.exposeAsNode;
}

bool PodCatalog::Save(creation::assets::ProjectSession& session, const juce::String& name, juce::String& error) {
    const auto it = entries_.find(name.toStdString());
    if (it == entries_.end()) {
        error = "No Pod named \"" + name + "\" to save.";
        return false;
    }
    auto& entry = it->second;

    const juce::String content = entry.authoringMode == PodAuthoringMode::Source
                                      ? entry.sourceText
                                      : juce::String(node_system::SerializeGraph(entry.graph));

    // Kind/authoringMode/exposeAsNode are embedded in the saved payload
    // itself (self-describing), not only recorded in this catalog -- see
    // PodCatalog.h's Save() doc comment.
    auto* payload = new juce::DynamicObject();
    payload->setProperty("kind", KindToken(entry.kind));
    payload->setProperty("authoringMode", AuthoringModeToken(entry.authoringMode));
    payload->setProperty("exposeAsNode", entry.exposeAsNode);
    payload->setProperty("content", content);
    payload->setProperty("outputNode", static_cast<int>(entry.outputNode));
    payload->setProperty("outputPin", static_cast<int>(entry.outputPin));
    juce::Array<juce::var> interfaceInputsJson;
    for (const auto& input : entry.interfaceInputs) {
        auto* inputObject = new juce::DynamicObject();
        inputObject->setProperty("name", input.name);
        inputObject->setProperty("type", DataTypeToken(input.type));
        inputObject->setProperty("boundNode", static_cast<int>(input.boundNode));
        inputObject->setProperty("boundPin", static_cast<int>(input.boundPin));
        interfaceInputsJson.add(juce::var(inputObject));
    }
    payload->setProperty("interfaceInputs", interfaceInputsJson);
    const juce::String json = juce::JSON::toString(juce::var(payload));
    const juce::MemoryBlock data(json.toRawUTF8(), json.getNumBytesAsUTF8());

    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::pod;
    options.displayName = name;
    options.logicalPath = LogicalPathFor(name);
    options.category = KindToken(entry.kind);
    options.mediaType = "application/x-creation-engine-pod";
    options.sourceApp = "Creation Engine";
    options.sourceTool = entry.authoringMode == PodAuthoringMode::Source ? "Pod Editor (Source)" : "Pod Editor (Graph)";
    options.description = "Creation Suite Pod -- " + KindToken(entry.kind) + " / " + AuthoringModeToken(entry.authoringMode);

    creation::assets::AssetDescriptor savedAsset;
    if (!creation::assets::ProjectAssetService::saveGeneratedAsset(session, data, options, savedAsset, error)) return false;

    entry.assetId = savedAsset.id;

    if (!session.commit(error)) return false;
    return true;
}

bool PodCatalog::LoadAll(creation::assets::ProjectSession& session, juce::String& error) {
    const auto podAssets = session.getManifest().assetCatalog.query({creation::assets::AssetKind::pod});

    for (const auto& asset : podAssets) {
        juce::MemoryBlock data;
        if (!session.readEntry(asset.logicalPath, data)) {
            error = "Could not read Pod asset \"" + asset.displayName + "\" (" + asset.logicalPath + ").";
            return false;
        }

        const juce::String json = data.toString();
        auto parsed = juce::JSON::parse(json);
        auto* payload = parsed.getDynamicObject();
        if (payload == nullptr) {
            error = "Pod asset \"" + asset.displayName + "\" has a malformed payload.";
            return false;
        }

        const auto kind = KindFromToken(payload->getProperty("kind").toString());
        const auto authoringMode = AuthoringModeFromToken(payload->getProperty("authoringMode").toString());
        const juce::String content = payload->getProperty("content").toString();

        PodEntry entry;
        entry.kind = kind;
        entry.authoringMode = authoringMode;
        entry.exposeAsNode = static_cast<bool>(payload->getProperty("exposeAsNode"));
        entry.assetId = asset.id;
        entry.outputNode = static_cast<node_system::NodeId>(static_cast<int>(payload->getProperty("outputNode")));
        entry.outputPin = static_cast<node_system::PinId>(static_cast<int>(payload->getProperty("outputPin")));
        if (const auto* inputsArray = payload->getProperty("interfaceInputs").getArray()) {
            for (const auto& inputVar : *inputsArray) {
                if (auto* inputObject = inputVar.getDynamicObject()) {
                    PodInterfaceInput input;
                    input.name = inputObject->getProperty("name").toString();
                    input.type = DataTypeFromToken(inputObject->getProperty("type").toString());
                    input.boundNode = static_cast<node_system::NodeId>(static_cast<int>(inputObject->getProperty("boundNode")));
                    input.boundPin = static_cast<node_system::PinId>(static_cast<int>(inputObject->getProperty("boundPin")));
                    entry.interfaceInputs.push_back(std::move(input));
                }
            }
        }

        if (authoringMode == PodAuthoringMode::Source) {
            entry.sourceText = content;
            entry.graph = node_system::Graph(asset.displayName.toStdString(), TargetFor(kind));
        } else {
            std::string deserializeError;
            auto graph = node_system::DeserializeGraph(content.toStdString(), deserializeError);
            if (!graph) {
                error = "Pod asset \"" + asset.displayName + "\" failed to deserialize: " + juce::String(deserializeError);
                return false;
            }
            entry.graph = std::move(*graph);
        }

        entries_[asset.displayName.toStdString()] = std::move(entry);
    }

    return true;
}

}  // namespace ce::frust
