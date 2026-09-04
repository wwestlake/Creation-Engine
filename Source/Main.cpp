#include <JuceHeader.h>

#include "Diagnostics/JuceLoggerBridge.h"
#include "MainComponent.h"
#include <creation/ui/CreationSuiteLogos.h>

class CreationEngineApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Creation Engine"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        // Installed before anything else runs so every juce::Logger::
        // writeToLog call from here on -- including ones inside
        // MainComponent's own constructor -- reaches EngineLog. See
        // JuceLoggerBridge's header comment.
        juce::Logger::setCurrentLogger(&loggerBridge_);
        mainWindow_.reset(new MainWindow(getApplicationName()));
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
