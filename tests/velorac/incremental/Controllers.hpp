#pragma once

#include <velora/reflect/Markers.hpp>

#include <string>

namespace Inc {

rkclass Main {
public:
    rkproperty(Title, std::string, get = title)
    std::string title() const;
};

rkclass Chip {
public:
    rkproperty(Label, std::string, get = label, set = set_label)
    std::string label() const;
    void set_label(std::string value);
};

rkclass Other {
public:
    cfunc void OnClick();
};

}  // namespace Inc
