namespace demo::containers {

cclass Inventory : public Base<int, Vec<float>>, virtual public Observer
{
public:
    cfield() std::vector<int> ids;
    cfield() std::map<std::string, std::vector<Item*>> byName;
    cfield() const std::unordered_map<int, float>& weights;

    cfunc std::vector<std::string> Names() const;
    cfunc void Merge(const std::map<int, std::vector<int>>& other, std::size_t limit);
    cfunc std::pair<int, float> Best(std::span<const Item> items);
};

cstruct Pair : Base<int>
{
    std::array<int, 4> slots;
    std::optional<std::string> label;
};

}
