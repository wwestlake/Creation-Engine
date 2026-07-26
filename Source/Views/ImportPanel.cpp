#include "Views/ImportPanel.h"

namespace ce {

// One imported audio clip: name/length + a Play/Stop button. All rows
// share owner_'s single AudioPreviewPlayer, so the button doesn't own
// its own playing/stopped state -- it asks owner_.IsPlayingClip() each
// refresh, so switching playback to a different row's clip correctly
// flips this row's button back to "Play" too.
class ImportPanel::AudioClipRow final : public juce::Component {
public:
    AudioClipRow(ImportPanel& owner, juce::String name, double lengthSeconds)
        : owner_(owner), name_(std::move(name)) {
        nameLabel_.setText(name_ + "  (" + juce::String(lengthSeconds, 1) + "s)", juce::dontSendNotification);
        nameLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffb8c4d5));
        addAndMakeVisible(nameLabel_);

        playButton_.onClick = [this] { owner_.TogglePlay(name_); };
        addAndMakeVisible(playButton_);

        UpdatePlayButtonLabel();
    }

    void UpdatePlayButtonLabel() { playButton_.setButtonText(owner_.IsPlayingClip(name_) ? "Stop" : "Play"); }

    void resized() override {
        auto bounds = getLocalBounds();
        playButton_.setBounds(bounds.removeFromRight(56).reduced(2));
        nameLabel_.setBounds(bounds);
    }

private:
    ImportPanel& owner_;
    juce::String name_;
    juce::Label nameLabel_;
    juce::TextButton playButton_;
};

ImportPanel::~ImportPanel() = default;

ImportPanel::ImportPanel(engine::World& world, ViewportComponent& viewport) {
    registry_.RegisterBuiltins();
    context_.world = &world;
    context_.catalog = &viewport.Catalog();
    context_.viewport = &viewport;
    context_.audioCatalog = &audioCatalog_;
    context_.audioFormatManager = &audioFormatManager_;
    // context_.vfs stays null -- nothing registered yet persists into the
    // VirtualFileSystem, only the live AssetCatalog/AudioCatalog (see
    // GltfAssetImporter/AudioAssetImporter).

    audioFormatManager_.registerBasicFormats(); // WAV, AIFF, and (JUCE_USE_FLAC defaults on) FLAC.

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

    audioClipsLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    audioClipsLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(audioClipsLabel_);
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

    RebuildAudioClipRows();
}

void ImportPanel::RebuildAudioClipRows() {
    audioClipRows_.clear();
    for (const auto& name : audioCatalog_.Names()) {
        const auto asset = audioCatalog_.Find(name);
        auto* row = audioClipRows_.add(new AudioClipRow(*this, name, asset.lengthSeconds));
        addAndMakeVisible(row);
    }
    resized();
}

void ImportPanel::TogglePlay(const juce::String& name) {
    if (currentlyPlayingName_ == name) {
        previewPlayer_.Stop();
        currentlyPlayingName_.clear();
    } else {
        const auto asset = audioCatalog_.Find(name);
        if (asset.buffer != nullptr) {
            previewPlayer_.Play(asset.buffer, asset.sampleRate); // stops whatever was already playing first.
            currentlyPlayingName_ = name;
        }
    }
    RefreshPlayButtonLabels();
}

void ImportPanel::RefreshPlayButtonLabels() {
    for (auto* row : audioClipRows_) {
        row->UpdatePlayButtonLabel();
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

    auto audioArea = area.removeFromRight(280);
    area.removeFromRight(12);
    log_.setBounds(area);

    audioClipsLabel_.setBounds(audioArea.removeFromTop(22));
    audioArea.removeFromTop(4);
    for (auto* row : audioClipRows_) {
        row->setBounds(audioArea.removeFromTop(26));
        audioArea.removeFromTop(2);
    }
}

} // namespace ce
