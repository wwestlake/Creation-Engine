#include <JuceHeader.h>

#include "Diagnostics/JuceLoggerBridge.h"
#include "MainComponent.h"
#include <creation/ui/CreationSuiteLogos.h>
#include <creation/ui/SuiteCommonSpacePanel.h>

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
        splashWindow_ = std::make_unique<StartupSplashWindow>(getApplicationName());
        splashWindow_->report("Preparing Djehuti Engine...", 0.04f);

        // Let the native splash paint before synchronous JUCE component
        // construction begins. MainComponent reports real stages below.
        juce::Timer::callAfterDelay(25, [this] { createMainWindow(); });
    }

    void anotherInstanceStarted(const juce::String&) override {
        if (mainWindow_ == nullptr) return;
        if (auto* peer = mainWindow_->getPeer(); peer != nullptr && peer->isMinimised()) peer->setMinimised(false);
        mainWindow_->toFront(true);
    }

    void shutdown() override {
        mainWindow_ = nullptr;
        splashWindow_ = nullptr;
        juce::Logger::setCurrentLogger(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class StartupSplashWindow final : public juce::DocumentWindow {
    public:
        explicit StartupSplashWindow(const juce::String& title)
            : DocumentWindow(title, juce::Colour(0xff0b0f14), 0)
        {
            auto content = std::make_unique<creation::ui::SuiteCommonSpacePanel>(creation::ui::SuiteCommonSpacePanel::Mode::splash);
            panel_ = content.get();
            panel_->setSelectedLogoId(creation::ui::SuiteLogoId::engine);
            setContentOwned(content.release(), true);
            setUsingNativeTitleBar(false);
            setAlwaysOnTop(true);
            centreWithSize(1040, 700);
            setVisible(true);
        }

        void report(const juce::String& status, float progress)
        {
            if (panel_ == nullptr) return;
            panel_->setStatusText(status);
            panel_->setProgress(progress);
            panel_->setFooterText(juce::String(juce::roundToInt(progress * 100.0f)) + "% complete");
            panel_->repaint();
            repaint();
            if (auto* peer = getPeer(); peer != nullptr)
                peer->performAnyPendingRepaintsNow();
        }

    private:
        creation::ui::SuiteCommonSpacePanel* panel_ = nullptr;
    };

    class MainWindow final : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String& name, MainComponent::StartupProgressCallback startupProgressCallback)
            : DocumentWindow(name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                   juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::engine));
            setContentOwned(new MainComponent(std::move(startupProgressCallback)), true);
            centreWithSize(1400, 900);
            setVisible(true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    void createMainWindow()
    {
        if (splashWindow_ == nullptr) return;
        auto reportProgress = [this](const juce::String& status, float progress) {
            if (splashWindow_ != nullptr) splashWindow_->report(status, progress);
        };
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), std::move(reportProgress));
        splashWindow_->report("Djehuti Engine is ready.", 1.0f);
        splashWindow_ = nullptr;
    }

    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<StartupSplashWindow> splashWindow_;
    ce::diagnostics::JuceLoggerBridge loggerBridge_;
};

START_JUCE_APPLICATION(CreationEngineApplication)
