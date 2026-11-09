// '(' inside <...> belongs to the template argument: function signatures and
// implicit struct fields with function types keep their full type.
cclass Button
{
public:
    cfunc std::function<void(int)> Callback() const;
    cfield() std::function<void(int)> onClick;
};

cstruct Handlers
{
    std::function<bool(const Event&)> filter;
    int priority;
};
