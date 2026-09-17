#include <iostream>

#include "VR/OpenXRProvider.h"

int main()
{
    ce::vr::OpenXRProvider provider;
    if (!provider.initialize()) {
        std::cerr << "OpenXR runtime was not available.\n";
        return 1;
    }

    std::cout << "OpenXR runtime ready; recommended eye target "
              << provider.recommendedRenderSize().width << 'x'
              << provider.recommendedRenderSize().height << "\n";
    provider.shutdown();
    return 0;
}
