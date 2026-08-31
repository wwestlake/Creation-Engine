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

// AI6: one timeline-slice request -- a name plus a start/end time (in
// seconds) into whatever glTF animation is dropped next. Values are read
// live off the text editors at drop time (BuildAnimationImportOptions),
// not validated here -- an empty name means "skip this row" (see
// BuildAnimationImportOptions), and an end <= start is silently dropped
// by AnimationSlicer::SliceClip itself.
class ImportPanel::SliceRow final : public juce::Component {
public:
    explicit SliceRow(ImportPanel& owner) : owner_(owner) {
        nameEditor_.setTextToShowWhenEmpty("clip name", juce::Colours::grey);
        addAndMakeVisible(nameEditor_);

        startEditor_.setText("0.0", juce::dontSendNotification);
        startEditor_.setInputRestrictions(8, "0123456789.");
        addAndMakeVisible(startEditor_);

        endEditor_.setText("1.0", juce::dontSendNotification);
        endEditor_.setInputRestrictions(8, "0123456789.");
        addAndMakeVisible(endEditor_);

        removeButton_.onClick = [this] { owner_.RemoveSliceRow(this); };
        addAndMakeVisible(removeButton_);
    }

    juce::String Name() const { return nameEditor_.getText().trim(); }
    float Start() const { return startEditor_.getText().getFloatValue(); }
    float End() const { return endEditor_.getText().getFloatValue(); }

    void resized() override {
        auto bounds = getLocalBounds();
        removeButton_.setBounds(bounds.removeFromRight(22).reduced(1));
        bounds.removeFromRight(2);
        endEditor_.setBounds(bounds.removeFromRight(46).reduced(1));
        startEditor_.setBounds(bounds.removeFromRight(46).reduced(1));
        nameEditor_.setBounds(bounds.reduced(1));
    }

private:
    ImportPanel& owner_;
    juce::TextEditor nameEditor_;
    juce::TextEditor startEditor_;
    juce::TextEditor endEditor_;
    juce::TextButton removeButton_{ "x" };
};

ImportPanel::~ImportPanel() = default;

ImportPanel::ImportPanel(engine::World& world, ViewportComponent& viewport) {
    registry_.RegisterBuiltins();
    context_.world = &world;
    context_.catalog = &viewport.Catalog();
    context_.viewport = &viewport;
    context_.audioCatalog = &audioCatalog_;
    context_.audioFormatManager = &audioFormatManager_;
    // The project session is attached when MainComponent selects a game.

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

    animationOptionsLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    animationOptionsLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(animationOptionsLabel_);

    interpolationLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(interpolationLabel_);

    interpolationCombo_.addItem("Keep authored", 1);
    interpolationCombo_.addItem("Force Step", 2);
    interpolationCombo_.addItem("Force Linear", 3);
    interpolationCombo_.addItem("Force CubicSpline", 4);
    interpolationCombo_.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(interpolationCombo_);

    rootMotionToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(rootMotionToggle_);

    slicesLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(slicesLabel_);

    addSliceButton_.onClick = [this] { AddSliceRow(); };
    addAndMakeVisible(addSliceButton_);
}

void ImportPanel::SetProjectContent(creation::assets::ProjectSession* session, const juce::String& gameAssetRoot)
{
    context_.projectSession = session;
    context_.gameAssetRoot = gameAssetRoot;
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

    // AI6: read once per drop batch, not per file -- these are "how
    // should the next animated glTF be imported" settings, not something
    // that varies file to file within one drop. Non-animated files (and
    // non-glTF importers entirely) ignore this field.
    context_.animationOptions = BuildAnimationImportOptions();

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

void ImportPanel::AddSliceRow() {
    auto* row = sliceRows_.add(new SliceRow(*this));
    addAndMakeVisible(row);
    resized();
}

void ImportPanel::RemoveSliceRow(SliceRow* row) {
    sliceRows_.removeObject(row);
    resized();
}

import::AnimationImportOptions ImportPanel::BuildAnimationImportOptions() const {
    import::AnimationImportOptions options;

    switch (interpolationCombo_.getSelectedId()) {
        case 2:
            options.interpolationOverride = AnimationInterpolation::Step;
            break;
        case 3:
            options.interpolationOverride = AnimationInterpolation::Linear;
            break;
        case 4:
            options.interpolationOverride = AnimationInterpolation::CubicSpline;
            break;
        default:
            break; // "Keep authored" (id 1), or nothing selected -- leave nullopt.
    }

    options.extractRootMotion = rootMotionToggle_.getToggleState();

    for (const auto* row : sliceRows_) {
        const auto name = row->Name();
        if (name.isEmpty()) {
            continue; // an unnamed row is a blank/in-progress row, not a real request.
        }
        options.slices.push_back(scene::ClipSlice{ name, row->Start(), row->End() });
    }

    return options;
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

    auto sidebarArea = area.removeFromRight(300);
    area.removeFromRight(12);
    log_.setBounds(area);

    animationOptionsLabel_.setBounds(sidebarArea.removeFromTop(22));
    sidebarArea.removeFromTop(4);
    interpolationLabel_.setBounds(sidebarArea.removeFromTop(16));
    interpolationCombo_.setBounds(sidebarArea.removeFromTop(24));
    sidebarArea.removeFromTop(6);
    rootMotionToggle_.setBounds(sidebarArea.removeFromTop(22));
    sidebarArea.removeFromTop(6);
    slicesLabel_.setBounds(sidebarArea.removeFromTop(16));
    for (auto* row : sliceRows_) {
        row->setBounds(sidebarArea.removeFromTop(24));
        sidebarArea.removeFromTop(2);
    }
    addSliceButton_.setBounds(sidebarArea.removeFromTop(22));
    sidebarArea.removeFromTop(14);

    audioClipsLabel_.setBounds(sidebarArea.removeFromTop(22));
    sidebarArea.removeFromTop(4);
    for (auto* row : audioClipRows_) {
        row->setBounds(sidebarArea.removeFromTop(26));
        sidebarArea.removeFromTop(2);
    }
}

} // namespace ce
