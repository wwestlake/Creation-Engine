#include <JuceHeader.h>

#include "MainComponent.h"
#include <creation/ui/CreationSuiteLogos.h>
#include <creation/ui/SuiteJUCEApplication.h>

class CreationEngineApplication final : public creation::ui::SuiteJUCEApplication {
public:
    CreationEngineApplication() : SuiteJUCEApplication(creation::ui::SuiteLogoId::engine) {}

    const juce::String getApplicationName() override { return "Creation Engine"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void systemRequestedQuit() override { quit(); }

protected:
    std::unique_ptr<juce::DocumentWindow> createMainWindow() override {
        return std::make_unique<MainWindow>(getApplicationName());
    }

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
};

START_JUCE_APPLICATION(CreationEngineApplication)
