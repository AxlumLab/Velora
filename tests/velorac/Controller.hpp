#pragma once

// Controllers for the velorac compiler test (tests/velorac_test.cpp). Only parsed by
// `velorac reflect` logic in-process; never compiled.
#include <velora/reflect/Markers.hpp>

#include <string>
#include <vector>

namespace Test {

rkclass Item : public velora::Observable {
public:
    rkproperty(Label, std::string, get = label, set = set_label)
    rkproperty(Rank, int, get = rank)
    std::string label() const;
    void set_label(std::string value);
    int rank() const;
};

rkclass Page {
public:
    rkproperty(Name, std::string, get = name, set = set_name)
    rkproperty(Caption, std::string, get = caption)
    rkproperty(Count, int, get = count)
    rkproperty(Ratio, double, get = ratio)
    rkproperty(Current, Item&, get = current)
    rkproperty(Items, velora::ObservableVector<Item>&, get = items)
    rkproperty(ConstItems, const velora::ObservableVector<Item>&, get = const_items)
    rkproperty(Names, std::vector<std::string>&, get = names)

    std::string name() const;
    void set_name(std::string value);
    std::string caption() const;
    int count() const;
    double ratio() const;
    Item& current();

    cfunc void OnClick();
    cfunc void Pick(int index);
    cfunc void Open(Item& item);
    cfunc void OpenPage(Page& page);
};

// Custom control with a XAML view (peers/Chip.xaml).
rkclass Chip {
public:
    rkproperty(Label, std::string, get = label, set = set_label)
    rkproperty(Size, int, get = size, set = set_size)
    rkproperty(Tag, std::string, get = tag)

    std::string label() const;
    void set_label(std::string value);
    int size() const;
    void set_size(int value);
    std::string tag() const;
};

// Reflected but has no XAML view.
rkclass Plain {
public:
    rkproperty(Value, int, get = value)
    int value() const;
};

}  // namespace Test
