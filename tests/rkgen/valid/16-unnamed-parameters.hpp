// A parameter without a name keeps its whole type (e.g. `const std::string&`)
// and is named _arg<index>.
cclass Logger
{
public:
    cfunc void Write(const std::string&, int);
    cfunc void Flush(bool force, unsigned);
};
