#include <memory>
#include <velora/fluent/FluentPackage.hpp>
#include <velora/resources/PackageFile.hpp>

namespace velora
{

std::string mountFluent(const std::filesystem::path& package, ResourceManager& manager)
{
    auto opened = PackageFile::open(package);
    if (const auto* error = std::get_if<VpakError>(&opened))
        return error->message;
    manager.mount(ResourceNamespace::kFramework,
                  std::make_shared<PackageFileProvider>(std::get<std::shared_ptr<PackageFile>>(opened)));
    return {};
}

} // namespace velora
