#include "FluentPage.g.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlWindow.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/AppBarButton.hpp>
#include <velora/fluent/CommandBar.hpp>
#include <velora/fluent/DropDownButton.hpp>
#include <velora/fluent/FluentPackage.hpp>
#include <velora/fluent/MenuBar.hpp>
#include <velora/fluent/MenuFlyout.hpp>
#include <velora/fluent/NumberBox.hpp>
#include <velora/fluent/PersonPicture.hpp>
#include <velora/fluent/RadioMenuFlyoutItem.hpp>
#include <velora/fluent/RatingControl.hpp>
#include <velora/fluent/SplitView.hpp>
#include <velora/fluent/SymbolIcon.hpp>
#include <velora/fluent/ToggleSplitButton.hpp>
#include <velora/fluent/ToolTip.hpp>
#include <velora/fluent/TwoPaneView.hpp>
#include <windows.h>

using namespace velora;
class FluentWindow final : public XamlWindow
{
  public:
    explicit FluentWindow(XamlApplication& app, bool previewMenu, bool previewMenuBar, bool previewCommands,
                          bool previewPanes, bool previewRating)
        : XamlWindow(app)
    {
        std::array<wchar_t, 32768> path{};
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length == path.size())
            throw std::runtime_error("cannot locate executable");
        const auto package = std::filesystem::path(path.data()).parent_path() / "Velora.Fluent.vpak";
        if (const auto error = mountFluent(package); !error.empty())
            throw std::runtime_error(error);
        WindowOptions options;
        options.title = "Velora Fluent controls";
        options.width = 680;
        options.height = 900;
        if (!Create(options))
            return;
        auto& ui = GetFramework();
        if (const auto error = PersonPicture::registerType(ui); !error.empty())
            throw std::runtime_error(error);
        if (const auto error = NumberBox::registerType(ui); !error.empty())
            throw std::runtime_error(error);
        for (const auto& error :
             {DropDownButton::registerType(ui), SplitButton::registerType(ui),
              ToggleSplitButton::registerType(ui), MenuFlyoutItem::registerType(ui),
              ToggleMenuFlyoutItem::registerType(ui), MenuFlyoutSeparator::registerType(ui),
              RadioMenuFlyoutItem::registerType(ui), FontIcon::registerType(ui), SymbolIcon::registerType(ui),
              MenuBar::registerType(ui), MenuBarItem::registerType(ui), AppBarButton::registerType(ui),
              AppBarToggleButton::registerType(ui), CommandBar::registerType(ui),
              AppBarSeparator::registerType(ui), SplitView::registerType(ui), TwoPaneView::registerType(ui),
              RatingControl::registerType(ui)})
            if (!error.empty())
                throw std::runtime_error(error);
        Demo::FluentPage_component::Names names;
        const auto loaded = Demo::FluentPage_component::initializeComponent(ui, m_controller, names);
        if (!loaded.ok)
            throw std::runtime_error(loaded.error);
        m_controller.setPicture(std::dynamic_pointer_cast<PersonPicture>(names.Avatar));
        m_controller.setStatusElement(names.Status);
        const auto rating = std::dynamic_pointer_cast<RatingControl>(names.Rating);
        if (!rating)
            throw std::runtime_error("RatingControl factory mismatch");
        rating->addValueChanged([this](f64 value)
                                { m_controller.record("Rating: " + std::to_string(value)); });
        m_toolTip = std::make_shared<ToolTip>();
        m_toolTip->setContent("Use arrows, Home or End. Click the current rating to clear.");
        m_toolTip->attach(ui, ui.tree().element(*rating));
        const auto sidebar = std::dynamic_pointer_cast<SplitView>(names.Sidebar);
        const auto adaptive = std::dynamic_pointer_cast<TwoPaneView>(names.AdaptivePanes);
        if (!sidebar || !adaptive)
            throw std::runtime_error("Pane control factory mismatch");
        const auto textPane = [&ui](const char* text)
        {
            const auto item = createElement(ElementKind::kTextBlock);
            item->setValue(*ui.props().Text, text);
            item->setValue(*ui.props().Margin, Thickness{8, 8, 8, 8});
            return item;
        };
        sidebar->setPane(textPane("Navigation"));
        sidebar->setContent(textPane("SplitView content"));
        adaptive->setPane1(textPane("Pane 1"));
        adaptive->setPane2(textPane("Pane 2"));
        adaptive->addModeChanged(
            [this](TwoPaneViewMode mode)
            {
                m_controller.record(
                    mode == TwoPaneViewMode::kWide
                        ? "TwoPaneView Wide"
                        : (mode == TwoPaneViewMode::kTall ? "TwoPaneView Tall" : "TwoPaneView SinglePane"));
            });
        const auto commands = std::dynamic_pointer_cast<CommandBar>(names.Commands);
        if (!commands)
            throw std::runtime_error("CommandBar factory mismatch");
        commands->addDynamicOverflowItemsChanging(
            [this](CommandBar&, const DynamicOverflowItemsChangingEventArgs& args)
            {
                m_controller.record(
                    std::string(args.action == CommandBarDynamicOverflowAction::kAddingToOverflow
                                    ? "Moving to overflow: "
                                    : "Returning to toolbar: ") +
                    std::to_string(args.items.size()));
            });
        const auto saveCommand = createElement<AppBarButton>();
        saveCommand->setLabel("Save");
        saveCommand->setIconGlyph("\xEE\x9D\x8E");
        saveCommand->setDynamicOverflowOrder(1);
        saveCommand->setCommand([this] { m_controller.record("CommandBar Save"); });
        commands->appendPrimary(saveCommand);
        const auto primarySeparator = createElement<AppBarSeparator>();
        primarySeparator->setDynamicOverflowOrder(1);
        commands->appendPrimary(primarySeparator);
        const auto primaryPin = createElement<AppBarToggleButton>();
        primaryPin->setLabel("Pin");
        primaryPin->setDynamicOverflowOrder(1);
        primaryPin->setIconGlyph("\xEE\x9C\x98");
        primaryPin->setCommand([this] { m_controller.record("CommandBar Pin toggled"); });
        commands->appendPrimary(primaryPin);
        for (const auto& [label, glyph] : {std::pair{"New", "\xEE\x9C\x90"},
                                           {"Open", "\xEE\xA2\xA5"},
                                           {"Undo", "\xEE\x9E\xA7"},
                                           {"Redo", "\xEE\x9E\xA6"},
                                           {"Cut", "\xEE\xA3\x86"},
                                           {"Copy", "\xEE\xA3\x88"},
                                           {"Paste", "\xEE\x9D\xBF"}})
        {
            const auto item = createElement<AppBarButton>();
            item->setLabel(label);
            item->setIconGlyph(glyph);
            item->setCommand([this, label] { m_controller.record(std::string("CommandBar: ") + label); });
            commands->appendPrimary(item);
        }
        for (const auto* label : {"Settings", "Delete"})
        {
            const auto item = createElement<AppBarButton>();
            item->setLabel(label);
            item->setIconGlyph("\xEE\x9C\x93");
            item->setCommand([this, label] { m_controller.record(std::string("Overflow: ") + label); });
            commands->appendSecondary(item);
        }
        commands->appendSecondary(createElement<AppBarSeparator>());
        const auto secondaryPin = createElement<AppBarToggleButton>();
        secondaryPin->setLabel("Keep pinned");
        secondaryPin->setIconGlyph("\xEE\x9C\x98");
        secondaryPin->setCommand([this] { m_controller.record("Overflow pin toggled"); });
        commands->appendSecondary(secondaryPin);
        ui.on(ui.tree().element(*names.AppBarSave), ui.events().Click,
              [this] { m_controller.record("AppBar Save"); });
        const auto appBarPin = std::dynamic_pointer_cast<AppBarToggleButton>(names.AppBarPin);
        if (!appBarPin)
            throw std::runtime_error("AppBarToggleButton factory mismatch");
        const std::weak_ptr<AppBarToggleButton> weakAppBarPin = appBarPin;
        ui.on(ui.tree().element(*appBarPin), ui.events().Click,
              [this, weakAppBarPin]
              {
                  if (const auto item = weakAppBarPin.lock())
                      m_controller.record(item->isChecked() ? "AppBar pinned" : "AppBar unpinned");
              });
        const auto bar = std::dynamic_pointer_cast<MenuBar>(names.ApplicationMenu);
        if (!bar)
            throw std::runtime_error("MenuBar factory mismatch");
        for (const auto* header : {"File", "Edit", "View"})
        {
            const auto entry = createElement<MenuBarItem>();
            entry->setHeader(header);
            for (const auto* caption : {"First action", "Second action"})
            {
                const auto action = createElement<MenuFlyoutItem>();
                action->setText(caption);
                action->setCommand([this, header, caption]
                                   { m_controller.record(std::string(header) + ": " + caption); });
                entry->menu()->append(action);
            }
            bar->append(entry);
        }
        auto flyout = std::make_shared<MenuFlyout>();
        for (const auto* text : {"Open item", "Save copy"})
        {
            const auto item = createElement<MenuFlyoutItem>();
            item->setText(text);
            item->setIconGlyph("\xEE\x9C\x8D");
            item->setKeyboardAcceleratorTextOverride("Ctrl+O");
            item->setCommand([this, text] { m_controller.record(std::string("Selected: ") + text); });
            flyout->append(item);
        }
        flyout->append(createElement<MenuFlyoutSeparator>());
        const auto pin = createElement<ToggleMenuFlyoutItem>();
        pin->setText("Pin item");
        const std::weak_ptr<ToggleMenuFlyoutItem> weakPin = pin;
        pin->setCommand(
            [this, weakPin]
            {
                if (const auto item = weakPin.lock())
                    m_controller.record(item->isChecked() ? "Pinned" : "Unpinned");
            });
        flyout->append(pin);
        flyout->append(createElement<MenuFlyoutSeparator>());
        for (const auto* name : {"Compact", "Comfortable"})
        {
            const auto item = createElement<RadioMenuFlyoutItem>();
            item->setText(name);
            item->setGroupName("Density");
            item->setIsChecked(std::string_view(name) == "Compact");
            item->setCommand([this, name] { m_controller.record(std::string("Density: ") + name); });
            flyout->append(item);
        }
        const auto drop = std::dynamic_pointer_cast<DropDownButton>(names.Actions);
        const auto split = std::dynamic_pointer_cast<SplitButton>(names.Save);
        const auto toggle = std::dynamic_pointer_cast<ToggleSplitButton>(names.Bold);
        if (!drop || !split || !toggle)
            throw std::runtime_error("Fluent family factory mismatch");
        drop->setFlyout(flyout);
        split->setFlyout(flyout);
        toggle->setFlyout(flyout);
        ui.on(ui.tree().element(*split), ui.events().Click,
              [this] { m_controller.record("Save primary action"); });
        const std::weak_ptr<ToggleSplitButton> weakToggle = toggle;
        ui.on(ui.tree().element(*toggle), ui.events().Click,
              [this, weakToggle]
              {
                  if (const auto button = weakToggle.lock())
                      m_controller.record(button->isChecked() ? "Bold on" : "Bold off");
              });
        m_page = ui.tree().element(*loaded.root);
        SetRoot(m_page);
        if (previewMenu || previewMenuBar || previewCommands || previewPanes || previewRating)
        {
            ui.update(GetAppWindow()->getWidth(), GetAppWindow()->getHeight());
            if (previewRating)
            {
                const auto id = ui.tree().element(*rating);
                ui.focus(id);
                if (rating->value() != 3.5 || !m_toolTip->show() || ui.focused() != id)
                    throw std::runtime_error("RatingControl/passive ToolTip startup failed");
            }
            else if (previewPanes)
            {
                if (adaptive->mode() != TwoPaneViewMode::kWide || !sidebar->isPaneOpen() ||
                    !adaptive->pane1()->isInTree() || !adaptive->pane2()->isInTree() ||
                    !sidebar->pane()->isInTree() || !sidebar->content()->isInTree())
                    throw std::runtime_error("Adaptive pane startup failed");
            }
            else if (previewCommands)
            {
                const auto lastCommand =
                    std::dynamic_pointer_cast<AppBarButton>(commands->primaryCommands().back());
                if (!lastCommand || lastCommand->isInOverflow() || !saveCommand->isInOverflow() ||
                    !primaryPin->isInOverflow())
                    throw std::runtime_error("CommandBar priority group did not overflow together");
                commands->setIsOpen(true);
                if (!commands->isOpen())
                    throw std::runtime_error("CommandBar overflow did not open");
            }
            else if (previewMenuBar)
            {
                if (!bar->items().front()->openFlyout())
                    throw std::runtime_error(bar->items().front()->menu()->lastError());
            }
            else if (!drop->openFlyout())
                throw std::runtime_error(flyout->lastError());
        }
    }
    ~FluentWindow() override
    {
        if (m_page)
        {
            SetRoot(0);
            GetFramework().tree().remove(m_page);
        }
    }

  private:
    Demo::FluentPage m_controller;
    ElementId m_page = 0;
    std::shared_ptr<ToolTip> m_toolTip;
};

class FluentApp final : public XamlApplication
{
  public:
    explicit FluentApp(bool previewMenu, bool previewMenuBar, bool previewCommands, bool previewPanes,
                       bool previewRating)
        : m_previewMenu(previewMenu), m_previewMenuBar(previewMenuBar), m_previewCommands(previewCommands),
          m_previewPanes(previewPanes), m_previewRating(previewRating)
    {
    }
    void OnLaunched() override
    {
        m_window = std::make_unique<FluentWindow>(*this, m_previewMenu, m_previewMenuBar, m_previewCommands,
                                                  m_previewPanes, m_previewRating);
        m_window->Show();
    }

  private:
    bool m_previewMenu = false;
    bool m_previewMenuBar = false;
    bool m_previewCommands = false;
    bool m_previewPanes = false;
    bool m_previewRating = false;
    std::unique_ptr<FluentWindow> m_window;
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR arguments, int)
{
    const std::string_view args(arguments);
    const bool previewMenuBar = args.find("--open-menubar") != std::string_view::npos;
    const bool previewCommands = args.find("--open-commandbar") != std::string_view::npos;
    FluentApp app(!previewMenuBar && args.find("--open-menu") != std::string_view::npos, previewMenuBar,
                  previewCommands, args.find("--preview-panes") != std::string_view::npos,
                  args.find("--preview-rating") != std::string_view::npos);
    try
    {
        return app.Run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Fluent Gallery: " << error.what() << '\n';
        return 1;
    }
}
