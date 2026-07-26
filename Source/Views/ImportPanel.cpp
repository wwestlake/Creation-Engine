#include "Views/ImportPanel.h"

namespace ce {

ImportPanel::ImportPanel(engine::World& world, ViewportComponent& viewport) {
    registry_.RegisterBuiltins();
    context_.world = &world;
    context_.catalog = &viewport.Catalog();
    context_.viewport = &viewport;
    // context_.vfs stays null -- nothing registered yet persists into the
    // VirtualFileSystem, only the live AssetCatalog (see GltfAssetImporter).

    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    dropZoneLabel_.setJustificationType(juce::Justification::centred);
    dropZoneLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(dropZoneLabel_);

    log_.setMultiLine(true);
    log_.setReadOnly(true);
    log_.setScrollbarsShown(true);
    log_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10141a));
    log_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffb8c4d5));
    addAndMakeVisible(log_);
}

bool ImportPanel::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files) {
        if (registry_.FindFor(juce::File(path)) != nullptr) {
            return true;
        }
    }
    return false;
}

void ImportPanel::fileDragEnter(const juce::StringArray&, int, int) {
    isDragHovering_ = true;
    repaint();
}

void ImportPanel::fileDragExit(const juce::StringArray&) {
    isDragHovering_ = false;
    repaint();
}

void ImportPanel::filesDropped(const juce::StringArray& files, int, int) {
    isDragHovering_ = false;
    repaint();

    for (const auto& path : files) {
        const juce::File file(path);
        auto* importer = registry_.FindFor(file);
        if (importer == nullptr) {
            AppendLogLine("[skip] " + file.getFileName() + " -- no importer registered for this file type.");
            continue;
        }

        const auto result = importer->Import(file, context_);
        AppendLogLine(juce::String(result.success ? "[ok]   " : "[fail] ") + file.getFileName() + " (" +
                      importer->DisplayName() + "): " + result.message);
    }
}

void ImportPanel::AppendLogLine(const juce::String& line) {
    log_.moveCaretToEnd();
    log_.insertTextAtCaret(line + juce::newLine);
}

void ImportPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));

    auto dropZoneBounds = dropZoneLabel_.getBounds().toFloat();
    g.setColour(isDragHovering_ ? juce::Colour(0xff56f4ff) : juce::Colours::white.withAlpha(0.15f));
    g.drawRoundedRectangle(dropZoneBounds, 8.0f, isDragHovering_ ? 2.0f : 1.0f);
}

void ImportPanel::resized() {
    auto area = getLocalBounds().reduced(16);
    titleLabel_.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    dropZoneLabel_.setBounds(area.removeFromTop(100));
    area.removeFromTop(12);
    log_.setBounds(area);
}

} // namespace ce
