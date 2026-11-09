// 'inline namespace' bodies are parsed like any other namespace.
namespace demo {
inline namespace v1 {
cstruct Versioned
{
    int revision;
};
}
}
