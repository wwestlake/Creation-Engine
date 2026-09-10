#include <JuceHeader.h>

#include "Diagnostics/JuceLoggerBridge.h"
#include "MainComponent.h"
#include <creation/ui/CreationSuiteLogos.h>

class CreationEngineApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Djehuti Engine"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }

    // Single-instance: launching the exe again (a shortcut double-click, a
    // tool relaunching it to "make sure it's running") must focus the
    // already-open window, not start a second process pointed at the same
    // project/VFS. JUCE's START_JUCE_APPLICATION macro handles the actual
    // IPC handshake -- with this false, a second launch attempt sends its
    // command line to the first instance's anotherInstanceStarted() below
    // and quits immediately, never reaching initialise() itself.
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        // Installed before anything else runs so every juce::Logger::
        // writeToLog call from here on -- including ones inside
        // MainComponent's own constructor -- reaches EngineLog. See
        // JuceLoggerBridge's header comment.
        juce::Logger::setCurrentLogger(&loggerBridge_);
        mainWindow_.reset(new MainWindow(getApplicationName()));
    }

    void anotherInstanceStarted(const juce::String&) override {
        if (mainWindow_ == nullptr) return;
        if (auto* peer = mainWindow_->getPeer(); peer != nullptr && peer->isMinimised()) peer->setMinimised(false);
        mainWindow_->toFront(true);
    }

    void shutdown() override {
        mainWindow_ = nullptr;
        juce::Logger::setCurrentLogger(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                   juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::engine));
            setContentOwned(new MainComponent(), true);
            centreWithSize(1400, 900);
            setVisible(true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> mainWindow_;
    ce::diagnostics::JuceLoggerBridge loggerBridge_;
};

START_JUCE_APPLICATION(CreationEngineApplication)
