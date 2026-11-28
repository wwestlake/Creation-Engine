#include "GameplayProcess.h"

#include "Gameplay/GameplayClient.h"
#include "Frust/PodCatalog.h"

#include <csignal>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

using namespace ce;

namespace
{
volatile std::sig_atomic_t stopping = 0;

void StopSignal(int) { stopping = 1; }

#ifdef _WIN32
BOOL WINAPI StopConsole(DWORD type)
{
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
    StopSignal(0);
    return TRUE;
}
#endif

juce::File ExplicitFile(const juce::ArgumentList& args, const char* option)
{
    const auto path = GameplayOption(args, option);
    return path.isEmpty() ? juce::File{} : juce::File::getCurrentWorkingDirectory().getChildFile(path);
}

gameplay::Message ReadCommands(const juce::File& file)
{
    if (!file.existsAsFile() || file.getSize() > 2048) return {};
    const auto payload = gameplay::Decode(file.loadFileAsString().toStdString());
    return payload ? *payload : gameplay::Message{};
}

gameplay::Input ReadInput(const gameplay::Message& message)
{
    using namespace gameplay;
    const auto x = Integer(Get(message, "x"), -1000, 1000), z = Integer(Get(message, "z"), -1000, 1000);
    return {x ? static_cast<int>(*x) : 0, z ? static_cast<int>(*z) : 0};
}

int CheckWire(const gameplay::ClientSettings& settings, const juce::String& mode)
{
    using namespace gameplay;
    if (settings.address != "127.0.0.1") return 2;
    if (mode != "version" && mode != "malformed" && mode != "budget") return 2;
    Transport transport;
    juce::String error;
    if (!transport.Connect(settings.address, settings.port, error)) return 2;
    const auto expected = mode == "version" ? "incompatible_version" : mode == "budget" ? "message_budget" : "malformed";
    bool done = false, passed = false;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 8000.0;
    while (!done && juce::Time::getMillisecondCounterHiRes() < deadline) {
        transport.Pump([&](const auto& event) {
            if (event.kind == Transport::Event::Kind::disconnected) { done = true; return; }
            if (event.kind != Transport::Event::Kind::message) return;
            const auto message = Decode(event.bytes);
            if (!message) return;
            const auto type = Get(*message, "type").toString();
            if (type == "challenge") {
                if (mode == "version")
                    transport.Send(event.connection, Lane::control,
                        Encode(Object({{"type", "hello"}, {"version", protocolVersion + 1},
                                       {"player", settings.player}, {"proof", "unused"}})));
                else if (mode == "budget")
                    transport.Send(event.connection, Lane::control, std::string(2049, ' '));
                else
                    transport.Send(event.connection, Lane::control, "{broken");
            }
            if (type == "rejected") {
                passed = Get(*message, "reason").toString() == expected;
                done = true;
                std::cout << "[protocol] " << Get(*message, "reason").toString() << std::endl;
            }
        });
        transport.Flush();
        juce::Thread::sleep(10);
    }
    return passed ? 0 : 3;
}

void WriteFile(const juce::File& path, const gameplay::Message& record)
{
    if (path != juce::File{}) path.replaceWithText(gameplay::Encode(record));
}
}

juce::String GameplayOption(const juce::ArgumentList& arguments, const juce::String& name)
{
    const auto position = arguments.indexOfOption(name);
    return position >= 0 && position + 1 < arguments.size() ? arguments[position + 1].text : juce::String{};
}

bool ReadClientOptions(const juce::ArgumentList& arguments, gameplay::ClientSettings& settings, juce::String& error)
{
    settings.address = GameplayOption(arguments, "--join");
    settings.port = GameplayOption(arguments, "--gameplay-port").getIntValue();
    settings.world = GameplayOption(arguments, "--world");
    settings.player = GameplayOption(arguments, "--player");
    settings.secret = juce::SystemStats::getEnvironmentVariable("DJEHUTI_GAMEPLAY_SECRET", {});
    if (settings.address.isEmpty() || settings.port < 1 || settings.port > 65535
        || !gameplay::Identifier(settings.world) || !gameplay::Identifier(settings.player)) {
        error = "Specify --join HOST --gameplay-port PORT --world WORLD --player PLAYER.";
        return false;
    }
    return true;
}

runtime::RuntimeGameSource AuthoredGameplayFixture(const juce::File& root)
{
    runtime::RuntimeGameSource source;
    source.game.id = "gameplay-fixture";
    source.game.name = "Authoritative gameplay fixture";
    source.game.entrySceneId = "entry";
    source.game.scenes.add(project::SceneDocumentInfo{"entry", "Entry", {}});
    source.readScene = [root](const auto& scene, auto& state, auto& error) {
        const auto file = root.getChildFile(scene.id + ".xml");
        state = juce::ValueTree::fromXml(file.loadFileAsString());
        if (state.hasType("CreationEngineScene")) return true;
        error = "Could not read the authored gameplay fixture.";
        return false;
    };
    source.readPods = [root](auto& pods, auto& error) {
        const auto file = root.getChildFile("gameplay-probe.frust");
        if (!file.existsAsFile()) { error = "The authored gameplay fixture Pod is missing."; return false; }
        pods.GetOrCreateSource("gameplay-probe", frust::PodKind::Behavior) = file.loadFileAsString();
        return true;
    };
    return source;
}

void InstallGameplaySignalHandlers()
{
    std::signal(SIGINT, StopSignal);
    std::signal(SIGTERM, StopSignal);
#ifdef _WIN32
    SetConsoleCtrlHandler(StopConsole, TRUE);
#endif
}

bool GameplayStopping() { return stopping != 0; }

int RunGameplayClientProbe(const juce::ArgumentList& arguments)
{
    gameplay::ClientSettings settings;
    juce::String error;
    if (!ReadClientOptions(arguments, settings, error)) {
        std::cerr << error << std::endl;
        return 2;
    }
    if (const auto check = GameplayOption(arguments, "--protocol-check"); check.isNotEmpty())
        return CheckWire(settings, check);
    gameplay::GameplayClient client(std::move(settings));
    if (!client.Connect(error)) { std::cerr << error << std::endl; return 2; }
    const auto commands = ExplicitFile(arguments, "--commands");
    const auto output = ExplicitFile(arguments, "--client-output");
    InstallGameplaySignalHandlers();
    int lastRequest = 0, lastResync = 0;
    using State = gameplay::GameplayClient::State;
    while (!GameplayStopping()) {
        client.Pump();
        const auto command = ReadCommands(commands);
        if (static_cast<bool>(gameplay::Get(command, "exit"))) break;
        if (client.ConnectionState() == State::active) {
            client.Submit(ReadInput(command));
            const auto next = static_cast<int>(gameplay::Get(command, "request"));
            if (next > lastRequest) {
                const auto name = gameplay::Get(command, "name").toString();
                const auto target = gameplay::Get(command, "target").toString();
                if (client.Request(name, target)) {
                    if (static_cast<bool>(gameplay::Get(command, "duplicate"))) client.RetryLastRequest();
                    lastRequest = next;
                }
            }
            const auto sync = static_cast<int>(gameplay::Get(command, "resync"));
            if (sync > lastResync) { client.Resynchronize(); lastResync = sync; }
        }
        WriteFile(output, client.Inspect());
        if (client.ConnectionState() == State::rejected || client.ConnectionState() == State::disconnected)
            return client.ConnectionState() == State::rejected ? 3 : 4;
        juce::Thread::sleep(33);
    }
    client.Disconnect();
    return 0;
}

int RunLocalGameplayProbe(gameplay::WorldSession& world, const juce::ArgumentList& arguments, int ticks)
{
    if (!world.AdmitLocal()) return 3;
    const auto commands = ReadCommands(ExplicitFile(arguments, "--commands"));
    const auto target = gameplay::Get(commands, "target").toString();
    if (target.isNotEmpty())
        world.SubmitLocalRequest(gameplay::Get(commands, "name").toString(), target);
    for (int i = 0; i < ticks; ++i) {
        world.SubmitLocal(ReadInput(commands));
        world.Tick();
    }
    const auto state = world.InspectLocal();
    if (!world.Stop()) { std::cerr << world.Error() << std::endl; return 2; }
    WriteFile(ExplicitFile(arguments, "--client-output"), state);
    std::cout << "[local] completed " << ticks << " gameplay ticks" << std::endl;
    return 0;
}
