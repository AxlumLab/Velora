// Newly authored regression input. See README.md for provenance.
#pragma once

#include <velora/reflect/Markers.hpp>

namespace rkgen_test::nested
{

rkstruct InventoryItem
{
    int itemId = 0;
    rkfield(get=GetCount, set=SetCount) int count = 0;
    const char* debugName = nullptr;

    int GetCount() const;
    void SetCount(int value);
};

rkclass Inventory
{
public:
    rkfield() InventoryItem* selected = nullptr;
    rkfunc void Select(const InventoryItem& item);
    rkfunc const InventoryItem& GetSelected() const;
};

} // namespace rkgen_test::nested
