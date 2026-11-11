# Velora

[简体中文](README.md) | **English**

**A C++23 UI framework for native applications, with compiled XAML.**

Velora aims to provide a WPF/UWP-style development experience through properties, bindings, templates, resources, and code-behind. Its visual and interaction reference is the historical WinUI 2.3 Gallery. Controls use ordinary C++ classes, inheritance, virtual extension points, and RAII. There is no Qt dependency.

Development currently focuses on **Windows**, using Win32, Direct2D, and DirectWrite. The framework is under active development and is suitable for experimentation, prototypes, and framework development. It is not yet a mature application SDK or a complete WinUI replacement.

Repository: [AxlumLab/Velora](https://github.com/AxlumLab/Velora) · License: [MIT](LICENSE)

## Contents

- [Status and compatibility](#status-and-compatibility)
- [Capabilities and controls](#capabilities-and-controls)
- [Architecture and source layout](#architecture-and-source-layout)
- [Windows quick start](#windows-quick-start)
- [Examples](#examples)
- [Working with C++ and XAML](#working-with-c-and-xaml)
- [Resources and distribution](#resources-and-distribution)
- [Validation and contributions](#validation-and-contributions)
- [Next steps and documentation](#next-steps-and-documentation)
- [Licensing and upstream sources](#licensing-and-upstream-sources)

## Status and compatibility

This is a source snapshot as of **2026-11-11**. [STATUS.md](docs/STATUS.md) is the authoritative record of implementation scope and validation evidence.

| Area | Current state |
| --- | --- |
| Windows | Working paths exist for native windows, D2D/DirectWrite, layout, input, resources, and several examples |
| Fluent types | **45 XAML control types** are declared in the formal `.vtypes` file, alongside C++ services, models, and interfaces |
| Functional groups | All **38 tracked groups are partially implemented**; 0 remain unconnected, and 0 have passed full compatibility acceptance |
| Validation | Earlier baselines have regression records; recent integration work has been limited to compilation and simple startup checks |
| Android | Planned; the Impeller standalone backend, application host, and device input support have not been delivered |
| Plugins and editor | Designs exist; the complete shared-runtime plugin SDK and XAML editing/preview product remain unimplemented |

**A connected group is not a completed port.** The 38-group count tracks selected source regions, including the preview Scroller. It is not a count of every public WinUI 2.3 type or API. Templates, visual states, object content and collections, bindings, input, focus, asynchronous lifecycles, accessibility, and the full historical Gallery comparison remain unfinished.

Velora targets new applications. It does not promise WPF/UWP source compatibility, WinRT binary compatibility, or a stable C++ object ABI across arbitrary compilers and framework versions. The Windows UIA provider, complete validation of real IME composition, and non-Windows product backends still have gaps.

## Capabilities and controls

The existing pipeline includes dependency properties and effective-value notifications, bindings, templates and resources, layout, routed events, focus, pointer capture, drawing, and composition. `controls` provides the shared UI infrastructure; `fluent` owns the internalized control behavior.

| Family | Current types or entry points |
| --- | --- |
| Layout and basic input | Grid, StackPanel, Canvas, Border, TextBlock, TextBox, Button, CheckBox, RadioButton, ToggleSwitch, Slider, ProgressBar, ScrollViewer, ListView, and other common elements |
| Buttons, commands, and icons | DropDownButton, SplitButton, ToggleSplitButton, AppBarButton/AppBarToggleButton, CommandBar/AppBarSeparator, FontIcon, SymbolIcon |
| Menus and popup services | MenuBar/MenuBarItem, regular/toggle/radio menu items and separators; C++ services include Flyout, MenuFlyout, CommandBarFlyout, ContentDialog, TeachingTip, and ToolTip |
| Editing and selection | NumberBox, AutoSuggestBox, ComboBox, RadioButtons, RatingControl, DatePicker, TimePicker, CalendarView, CalendarDatePicker, ColorPicker/ColorSpectrum |
| Navigation and content | NavigationView/NavigationViewItem, TreeView with the C++ TreeViewNode model, TabView/TabViewItem, Pivot, FlipView, SplitView, TwoPaneView, PersonPicture |
| Scrolling, gestures, and motion | ScrollBar, Scroller, ItemsRepeater, SwipeControl/SwipeItem, RefreshContainer/RefreshVisualizer, ParallaxView, AnimatedVisualPlayer |

These types have real implementation entry points, but their feature coverage varies. ItemsRepeater currently realizes and recycles fixed-height vertical items. CalendarView initially supports Gregorian single selection. AnimatedVisualPlayer accepts an ordinary C++ drawing source and does not include a Lottie file decoder. Date/time selection, refresh, swipe, and color controls also have documented API, template, and device-input differences. See [PORTING.md](modules/fluent/PORTING.md).

Windows icons use the system-installed **Segoe MDL2 Assets** font. A cross-platform replacement has not been implemented.

## Architecture and source layout

```text
XAML + C++ code-behind + .vtypes metadata
                  | build-time velorac
                  v
      .vxbf + generated connection code + .vpak
                  | component loading
                  v
      UIElement / DependencyObject / ElementTree
                  | layout, input, drawing, composition
                  v
        Windows: Win32 + D2D + DirectWrite
```

Applications normally load compiled XAML; ANTLR belongs to the build-time toolchain. Unknown types, properties, and unsupported syntax require diagnostics. Each control has one actual object and one effective property-value store.

| Directory | Responsibility |
| --- | --- |
| `modules/foundation` | Fundamental types, containers, and encoding utilities |
| `modules/reflect`, `modules/runtime` | Metadata, type registration, dependency properties, and bindings |
| `modules/resources`, `modules/xaml` | Resource URIs, indexes, packages, XAML binaries, and loading |
| `modules/controls` | The actual element tree, layout, input, templates, and common control infrastructure |
| `modules/fluent/src`, `modules/fluent/resources` | Fluent behavior, formal declarations, compiled templates, and themes |
| `modules/fluent/porting` | Pinned reference inputs; excluded from product compilation, installation, and resource packages |
| `modules/graphics`, `modules/composition` | Drawing, text services, the visual tree, and composition |
| `modules/platform`, `modules/application` | Platform services, applications, windows, and XAML hosting |
| `modules/markup` | The build-time reflection/XAML compiler, `velorac` |
| `examples`, `tests`, `cmake`, `scripts` | Examples, existing tests, build helpers, and source checks |

## Windows quick start

### 1. Get the source

```powershell
git clone https://github.com/AxlumLab/Velora.git
cd Velora
```

An existing checkout only needs its remote URL updated. Reinitialization and history rewriting are unnecessary:

```powershell
git remote set-url origin https://github.com/AxlumLab/Velora.git
```

### 2. Prepare the toolchain and dependencies

You need the Visual Studio 2022 C++ toolchain, a Windows SDK, CMake 3.25+, and Python 3. Full builds also require Java 11+ and rhodium. `core-check` uses Ninja in an x64 MSVC developer terminal; `dev` uses the Visual Studio 2022 multi-configuration generator. Environment initialization is described in [BUILD.md](docs/BUILD.md).

Dependencies are pinned by [rhodium.lua](rhodium.lua) and [tpkg.lock.toml](tpkg.lock.toml). Restore and generate the Debug ANTLR C++ runtime exports first:

```powershell
rhodium restore --toolchain windows-msvc-x64 --locked --config debug
rhodium generate --toolchain windows-msvc-x64 --config debug
```

The exports must exist at `.rhodium/generated/cmake/rhodiumConfig.cmake`. For Release, restore the matching CRT configuration separately:

```powershell
rhodium restore --toolchain windows-msvc-x64 --locked --config release
rhodium generate --toolchain windows-msvc-x64 --config release --output .rhodium/generated/cmake-release
```

The **ANTLR 4.13.2 complete jar** is also required. CMake verifies its pinned SHA-256 and checks `.rhodium/tools/antlr4-4.13.2-complete.jar`. If it cannot find the jar, supply its local path during configuration. rhodium installation and source configuration belong to the user's environment; dependency caches are not shipped in this repository.

### 3. Build and run the Fluent Gallery

```powershell
cmake --preset dev -DVELORA_ANTLR_TOOL_JAR=C:/tools/antlr-4.13.2-complete.jar
cmake --build --preset dev-debug --target velora_fluent_gallery --parallel 4
& ./build/dev/examples/fluent_controls/Debug/velora_fluent_gallery.exe
```

Omit `-DVELORA_ANTLR_TOOL_JAR=...` if CMake has already located a verified jar. The build deploys the matching `Velora.Fluent.vpak` and WinUI MIT license beside the executable.

The latest batch has a simple startup entry point:

```powershell
& ./build/dev/examples/fluent_controls/Debug/velora_fluent_gallery.exe --preview-remaining
```

This argument runs one initialization path and displays the window. It is not a complete behavior test. See [BUILD.md](docs/BUILD.md) for more entry points, troubleshooting, checksums, and Release/ASan configurations.

### 4. Build only the core libraries

The core configuration disables ANTLR and does not require rhodium exports or the jar. It does not build the full XAML toolchain or Windows examples.

```powershell
cmake --preset core-check
cmake --build --preset core-check --parallel 4
python scripts/CheckStyle.py
python scripts/CheckWorkspace.py
```

On Windows, use an x64 MSVC developer terminal with Ninja on PATH. Non-Windows core builds can check selected logic; they do not establish a usable Linux, macOS, or Android UI backend.

## Examples

| Example | Build target | Purpose |
| --- | --- | --- |
| [Fluent Gallery](examples/fluent_controls) | `velora_fluent_gallery` | Current Fluent families, compiled XAML, popup services, and C++ object content |
| [Basic Gallery](examples/gallery) | `velora_gallery` | Basic forms, bindings, layout, and input |
| [Source control](examples/custom_control) | `velora_custom_control` | ValuePad virtual input/drawing and `.vtypes` declarations |
| [Application startup](examples/n04_startup) | `n04_startup` | Application/window setup and a compiled page host |
| [Installed-package consumer](examples/consumer) | Separate CMake project | A development example using `find_package(Velora CONFIG REQUIRED)` |

The Fluent Gallery also accepts `--preview-datetime`, `--preview-refresh`, `--preview-tree`, `--preview-navigation`, and `--preview-tabs`, among other initialization entry points. They offer a quick view of individual integration paths, rather than full historical WinUI 2.3 Gallery acceptance.

## Working with C++ and XAML

This fragment illustrates page structure. Use the Fluent Gallery for the complete host, type registration, and build setup:

```xml
<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:fluent="using:Velora.Fluent" Spacing="12">
    <fluent:PersonPicture DisplayName="Ada Lovelace" Width="64" Height="64"/>
    <fluent:NumberBox Minimum="0" Maximum="100" Value="42"/>
    <fluent:CalendarDatePicker Date="2026-11-11" PlaceholderText="Select a date"/>
</StackPanel>
```

At build time, pass declarations through `velora_add_xaml(... TYPES .../FluentControls.vtypes)`. At runtime, mount the matching Fluent resource package and register the types used by that Framework, for example with `PersonPicture::registerType(ui)`. Object properties, collections, events, and Binding/x:Bind expressions are not yet uniformly supported on source controls. General pipeline support does not imply complete support in every control.

The same actual control can be created and configured in C++:

```cpp
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/PersonPicture.hpp>

auto avatar = velora::createElement<velora::PersonPicture>();
avatar->setDisplayName("Ada Lovelace");
avatar->setBadgeNumber(3);
```

Attach it to the existing UI tree with an operation such as `ui.insert(avatar, parentId)`, after resource mounting and context setup. Typed APIs access the real dependency properties; they do not create proxy controls. The current tree uses `shared_ptr` ownership. Parent references and callbacks must avoid strong cycles, and control operations, unsubscription, and destruction have UI-thread affinity.

See [DESIGN.md](docs/DESIGN.md), [XAML-TOOLCHAIN.md](docs/XAML-TOOLCHAIN.md), and [PLUGIN-SDK.md](docs/PLUGIN-SDK.md) for extension contracts and current limits.

## Resources and distribution

- `.vxbf` contains compiled XAML, `.vpri` is the internal resource index, and `.vpak` is the resource package. The toolchain manages indexes; applications do not maintain them manually.
- `VELORA_EMBED_RESOURCES=ON` embeds the application resource package by default. Disabling it lets build helpers deploy external resources. Embedding application resources does not mean the Fluent Gallery has merged its separate `Velora.Fluent.vpak` into a single executable.
- Modules with source files currently build as static libraries. CMake exports targets such as `velora::controls`, `velora::fluent`, and `velora::application`. Build and installation helpers are development entry points, not a finalized plugin SDK.
- Static, self-contained applications are an intended distribution mode; complete dependency-based trimming remains planned. Native plugins are intended to share one runtime with the host and match the SDK build, compiler, CRT, architecture, and configuration. The loader and complete shared-library SDK have not been delivered.

## Validation and contributions

During the current integration phase, development validation is limited to compilation and simple startup checks. Existing tests are retained; full regression, ASan, screenshot, and behavior comparisons are deferred to the consolidated acceptance phase. When running existing regressions is appropriate, use `ctest --preset core-check` or `ctest --preset dev-debug` after building that configuration. Record the platform, configuration, and actual execution scope.

Read [AGENTS.md](AGENTS.md) and [START-HERE.md](START-HERE.md) before contributing. C++ code uses C++23, Allman braces, four spaces, and UTF-8/LF. Sources live under `modules/<module>/src/velora/`; generated code stays in build directories. Do not edit generated outputs, remove tests to hide regressions, or label placeholders as completed features.

Build output, `.rhodium/`, local editor settings, Python caches, and backups stay out of Git. Split commits by independent topic and update implementation status and provenance when changing controls.

## Next steps and documentation

Near-term work is to complete APIs, object content/collections and bindings, templates and visual states, input and focus, asynchronous cancellation/recycling, UIA, and the historical Gallery's public-type and behavior inventory. Android/Impeller, native plugins, editor preview, and performance comparisons remain future work rather than completed or stable-release commitments.

| Document | Contents |
| --- | --- |
| [STATUS.md](docs/STATUS.md) | Implementation scope, group counts, evidence, and gaps |
| [BUILD.md](docs/BUILD.md) | Dependencies, presets, building, and running examples |
| [DESIGN.md](docs/DESIGN.md) | Architecture, modules, ownership, and distribution goals |
| [IMPLEMENTATION-PLAN.md](docs/IMPLEMENTATION-PLAN.md) | Work batches and acceptance conditions |
| [XAML-TOOLCHAIN.md](docs/XAML-TOOLCHAIN.md) | Type declarations, compilation, resources, and preview design |
| [PLUGIN-SDK.md](docs/PLUGIN-SDK.md) | C++ extension, compatibility, and lifecycle boundaries |
| [PORTING.md](modules/fluent/PORTING.md) | WinUI provenance, adaptations, and per-family limits |
| [CLEANUP.md](docs/CLEANUP.md) | Removals, retained functionality, and migrations in the cleaned baseline |

Most detailed design and status documents are currently written in Chinese.

## Licensing and upstream sources

Velora is licensed under the [MIT License](LICENSE), preserving existing copyright attribution. WinUI-derived implementations and resources also retain Microsoft's copyright notices and the [original WinUI 2.3 MIT text](modules/fluent/LICENSE-WinUI2.3.txt). Moving the repository to AxlumLab does not rewrite original authorship.

The reference is `microsoft/microsoft-ui-xaml` tag `v2.3.191129002`, commit `2237b09d9ba6052414ddccf3d61316de20564303`. See [PORTING.md](modules/fluent/PORTING.md) and the [source manifest](modules/fluent/porting/Sources.json) for file provenance, hashes, and adaptation scope. Third-party dependencies retain their own licenses.
