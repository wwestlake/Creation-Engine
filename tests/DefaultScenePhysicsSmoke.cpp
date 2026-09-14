#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
[[noreturn]] void fail(const char* message)
{
    std::cerr << "DefaultScenePhysicsSmoke failure: " << message << '\n';
    std::exit(1);
}
}

int main()
{
    std::ifstream input(CE_DEFAULT_SCENE_PATH, std::ios::binary);
    if (!input)
        fail("could not read the authored default scene");

    const std::string scene{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    std::size_t entityCount = 0;
    std::size_t staticBodyCount = 0;
    std::size_t colliderCount = 0;
    for (std::size_t cursor = 0; (cursor = scene.find("<Entity ", cursor)) != std::string::npos; ++cursor)
    {
        ++entityCount;
        const auto end = scene.find("</Entity>", cursor);
        if (end == std::string::npos)
            fail("an Entity element has no closing tag");
        const auto entity = scene.substr(cursor, end - cursor);
        staticBodyCount += entity.find("<RigidBody motionType=\"0\"") != std::string::npos;
        colliderCount += entity.find("<Collider ") != std::string::npos;
    }

    if (entityCount == 0 || staticBodyCount != entityCount || colliderCount != entityCount)
        fail("every authored DefaultScene object must carry an explicit static collider");

    std::cout << "DefaultScenePhysicsSmoke passed: " << entityCount
              << " authored static collision instances.\n";
    return 0;
}
