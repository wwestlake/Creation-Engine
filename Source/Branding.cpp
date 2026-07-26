#include "Branding.h"

namespace ce::branding {

juce::Image CreateLogoImage(int size) {
    static constexpr const char* svg = R"svg(
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="bg" x1="24" y1="20" x2="232" y2="236" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#111a2f"/>
      <stop offset="100%" stop-color="#06070c"/>
    </linearGradient>
    <linearGradient id="accent" x1="36" y1="36" x2="220" y2="220" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#56f4ff"/>
      <stop offset="45%" stop-color="#7f7dff"/>
      <stop offset="100%" stop-color="#ff5fc8"/>
    </linearGradient>
  </defs>
  <rect x="12" y="12" width="232" height="232" rx="52" fill="url(#bg)"/>
  <rect x="28" y="28" width="200" height="200" rx="40" fill="none" stroke="url(#accent)" stroke-width="4" opacity="0.55"/>
  <path d="M128,78 L74,109 M128,78 L182,109 M74,109 L128,140 M182,109 L128,140
           M74,109 L74,171 M182,109 L182,171 M128,140 L128,202 M74,171 L128,202 M182,171 L128,202"
        fill="none" stroke="url(#accent)" stroke-width="8" stroke-linecap="round" stroke-linejoin="round"/>
  <circle cx="128" cy="78" r="8" fill="#56f4ff"/>
  <circle cx="74" cy="171" r="8" fill="#7f7dff"/>
  <circle cx="182" cy="171" r="8" fill="#ff5fc8"/>
</svg>
)svg";

    auto xml = juce::parseXML(juce::String::fromUTF8(svg));
    if (xml == nullptr) {
        return {};
    }

    auto drawable = juce::Drawable::createFromSVG(*xml);
    if (drawable == nullptr) {
        return {};
    }

    juce::Image image(juce::Image::ARGB, size, size, true);
    juce::Graphics g(image);
    drawable->drawWithin(g, image.getBounds().toFloat(), juce::RectanglePlacement::centred, 1.0f);
    return image;
}

} // namespace ce::branding
