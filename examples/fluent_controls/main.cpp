#include "FluentPage.g.hpp"

#include <array>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlWindow.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/AppBarButton.hpp>
#include <velora/fluent/AutoSuggestBox.hpp>
#include <velora/fluent/ComboBox.hpp>
#include <velora/fluent/CommandBar.hpp>
#include <velora/fluent/CommandBarFlyout.hpp>
#include <velora/fluent/ContentDialog.hpp>
#include <velora/fluent/DropDownButton.hpp>
#include <velora/fluent/FlipView.hpp>
#include <velora/fluent/FluentPackage.hpp>
#include <velora/fluent/MenuBar.hpp>
#include <velora/fluent/MenuFlyout.hpp>
#include <velora/fluent/NumberBox.hpp>
#include <velora/fluent/PersonPicture.hpp>
#include <velora/fluent/Pivot.hpp>
#include <velora/fluent/RadioButtons.hpp>
#include <velora/fluent/RadioMenuFlyoutItem.hpp>
#include <velora/fluent/RatingControl.hpp>
#include <velora/fluent/SplitView.hpp>
#include <velora/fluent/SymbolIcon.hpp>
#include <velora/fluent/TabView.hpp>
#include <velora/fluent/TeachingTip.hpp>
#include <velora/fluent/ToggleSplitButton.hpp>
#include <velora/fluent/ToolTip.hpp>
#include <velora/fluent/TwoPaneView.hpp>
#include <windows.h>

using namespace velora;
class FluentWindow final : public XamlWindow
{
  public:
    explicit FluentWindow(XamlApplication& app, bool previewMenu, bool previewMenuBar, bool previewCommands,
                          bool previewPanes, bool previewRating, bool previewCommandFlyout,
                          bool previewChoices, i32 previewPanels)
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
        for (const auto& error : {DropDownButton::registerType(ui),
                                  SplitButton::registerType(ui),
                                  ToggleSplitButton::registerType(ui),
                                  MenuFlyoutItem::registerType(ui),
                                  ToggleMenuFlyoutItem::registerType(ui),
                                  MenuFlyoutSeparator::registerType(ui),
                                  RadioMenuFlyoutItem::registerType(ui),
                                  FontIcon::registerType(ui),
                                  SymbolIcon::registerType(ui),
                                  MenuBar::registerType(ui),
                                  MenuBarItem::registerType(ui),
                                  AppBarButton::registerType(ui),
                                  AppBarToggleButton::registerType(ui),
                                  CommandBar::registerType(ui),
                                  AppBarSeparator::registerType(ui),
                                  SplitView::registerType(ui),
                                  TwoPaneView::registerType(ui),
                                  RatingControl::registerType(ui),
                                  TabView::registerType(ui),
                                  TabViewItem::registerType(ui),
                                  Pivot::registerType(ui),
                                  FlipView::registerType(ui),
                                  ComboBox::registerType(ui),
                                  RadioButtons::registerType(ui),
                                  AutoSuggestBox::registerType(ui)})
            if (!error.empty())
                throw std::runtime_error(error);
        Demo::FluentPage_component::Names names;
        const auto loaded = Demo::FluentPage_component::initializeComponent(ui, m_controller, names);
        if (!loaded.ok)
            throw std::runtime_error(loaded.error);
        m_controller.setPicture(std::dynamic_pointer_cast<PersonPicture>(names.Avatar));
        m_controller.setStatusElement(names.Status);
        const auto search = std::dynamic_pointer_cast<AutoSuggestBox>(names.Search);
        if (!search)
            throw std::runtime_error("AutoSuggestBox factory mismatch");
        search->setSuggestions({"Views", "Visual states", "Virtualization"});
        const std::weak_ptr<AutoSuggestBox> weakSearch = search;
        search->addTextChanged(
            [weakSearch](const AutoSuggestTextChangedEventArgs& args)
            {
                if (args.reason != AutoSuggestTextChangeReason::kUserInput)
                    return;
                if (const auto box = weakSearch.lock())
                {
                    auto query = args.text;
                    for (auto& c : query)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    std::vector<std::string> matches;
                    for (const auto* label :
                         {"Views", "Visual states", "Virtualization", "Values", "Windows", "Widgets"})
                    {
                        std::string word(label);
                        for (auto& c : word)
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (!query.empty() && word.find(query) != std::string::npos)
                            matches.emplace_back(label);
                    }
                    box->setSuggestions(std::move(matches));
                }
            });
        search->addQuerySubmitted(
            [this](const AutoSuggestQuerySubmittedEventArgs& args)
            {
                m_controller.record("Query: " + args.queryText +
                                    (args.chosenSuggestion ? " (suggestion)" : ""));
            });
        m_dialog = std::make_shared<ContentDialog>();
        m_dialog->setTitle("Save changes?");
        m_dialog->setContent("Choose Save, Discard, or Cancel.");
        m_dialog->setPrimaryButtonText("Save");
        m_dialog->setSecondaryButtonText("Discard");
        m_dialog->setCloseButtonText("Cancel");
        m_dialog->setDefaultButton("Primary");
        m_dialog->addClosed(
            [this](PanelCloseReason, PanelResult result)
            { m_controller.record("Dialog result: " + std::to_string(static_cast<i32>(result))); });
        m_teachingTip = std::make_shared<TeachingTip>();
        m_teachingTip->setTitle("Try the new controls");
        m_teachingTip->setSubtitle("Fluent controls are being connected in groups.");
        m_teachingTip->setContent("Use the action below or close this tip.");
        m_teachingTip->setActionButtonContent("Got it");
        m_teachingTip->setCloseButtonContent("Close");
        m_teachingTip->setIsLightDismissEnabled(true);
        m_teachingTip->addButtonClick(
            [this](PanelButtonEventArgs& args)
            {
                if (args.button == PanelButton::kPrimary)
                    m_controller.record("Teaching tip action invoked");
            });
        const auto dialogAnchor = ui.tree().element(*names.ShowDialog);
        m_teachingTip->attach(ui, ui.tree().element(*names.ShowTeachingTip));
        ui.on(dialogAnchor, ui.events().Click,
              [this, dialogAnchor] { m_dialog->showAt(GetFramework(), dialogAnchor); });
        ui.on(ui.tree().element(*names.ShowTeachingTip), ui.events().Click,
              [this] { m_teachingTip->setIsOpen(true); });
        const auto themeChoice = std::dynamic_pointer_cast<ComboBox>(names.ThemeChoice);
        const auto densityChoice = std::dynamic_pointer_cast<RadioButtons>(names.DensityChoice);
        if (!themeChoice || !densityChoice)
            throw std::runtime_error("Choice factory mismatch");
        themeChoice->setItems({"System", "Light", "Dark"});
        densityChoice->setItems({"Compact", "Normal", "Comfortable", "Spacious"});
        themeChoice->addSelectionChanged([this](i32, i32 index)
                                         { m_controller.record("Theme choice: " + std::to_string(index)); });
        densityChoice->addSelectionChanged(
            [this](i32, i32 index) { m_controller.record("Density choice: " + std::to_string(index)); });
        m_commandFlyout = std::make_shared<CommandBarFlyout>();
        const auto copy = createElement<AppBarButton>();
        copy->setLabel("Copy");
        copy->setIconGlyph("\xEE\xA3\x88");
        copy->setCommand([this] { m_controller.record("Copy command invoked"); });
        m_commandFlyout->appendPrimary(copy);
        const auto flyoutPin = createElement<AppBarToggleButton>();
        flyoutPin->setLabel("Pin");
        flyoutPin->setIconGlyph("\xEE\x9C\x98");
        m_commandFlyout->appendPrimary(flyoutPin);
        const auto share = createElement<AppBarButton>();
        share->setLabel("Share");
        share->setIconGlyph("\xEE\x9C\xAD");
        share->setCommand([this] { m_controller.record("Share command invoked"); });
        m_commandFlyout->appendSecondary(share);
        m_commandFlyout->appendSecondary(createElement<AppBarSeparator>());
        const auto favorite = createElement<AppBarToggleButton>();
        favorite->setLabel("Favorite");
        favorite->setIconGlyph("\xEE\x9C\xB4");
        m_commandFlyout->appendSecondary(favorite);
        const auto commandAnchor = ui.tree().element(*names.ContextCommands);
        ui.on(commandAnchor, ui.events().Click,
              [this, commandAnchor] { m_commandFlyout->showAt(GetFramework(), commandAnchor); });
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
        const auto pivot = std::dynamic_pointer_cast<Pivot>(names.Pages);
        const auto slides = std::dynamic_pointer_cast<FlipView>(names.Slides);
        if (!pivot || !slides)
            throw std::runtime_error("Page selector factory mismatch");
        const auto notes = createElement(ElementKind::kTextBox);
        const auto tabs = std::dynamic_pointer_cast<TabView>(names.Documents);
        if (!tabs)
            throw std::runtime_error("TabView factory mismatch");
        const auto welcomeTab = createElement<TabViewItem>();
        welcomeTab->setHeader("Welcome");
        welcomeTab->setIsClosable(false);
        welcomeTab->setContent(textPane("This tab stays open."));
        const auto documentTab = createElement<TabViewItem>();
        documentTab->setHeader("Document");
        const auto documentEditor = createElement(ElementKind::kTextBox);
        documentEditor->setValue(*ui.props().Text, "Edit this document, switch tabs, then return.");
        documentTab->setContent(documentEditor);
        tabs->setTabItems({welcomeTab, documentTab});
        const std::weak_ptr<TabView> weakTabs = tabs;
        tabs->addTabCloseRequested(
            [this, weakTabs](const TabCloseRequestedEventArgs& args)
            {
                if (const auto view = weakTabs.lock())
                {
                    view->removeTab(args.tab);
                    m_controller.record("Closed tab: " + args.tab->header());
                }
            });
        tabs->addAddTabButtonClick(
            [this, weakTabs]
            {
                if (const auto view = weakTabs.lock())
                {
                    const auto tab = createElement<TabViewItem>();
                    auto items = view->tabItems();
                    tab->setHeader("New " + std::to_string(items.size()));
                    const auto editor = createElement(ElementKind::kTextBox);
                    editor->setValue(*GetFramework().props().Text, "New document");
                    tab->setContent(editor);
                    items.push_back(tab);
                    view->setTabItems(std::move(items));
                    view->setSelectedIndex(static_cast<i32>(view->pages().size()) - 1);
                }
            });
        notes->setValue(*ui.props().Text, "This editor keeps its state when switching pages.");
        pivot->setPages({{"Overview", textPane("Workspace overview")}, {"Notes", notes}});
        slides->setPages({{"First", textPane("First slide")},
                          {"Second", textPane("Second slide")},
                          {"Third", textPane("Third slide")}});
        pivot->addSelectionChanged([this](i32, i32 index)
                                   { m_controller.record("Pivot page: " + std::to_string(index)); });
        slides->addSelectionChanged([this](i32, i32 index)
                                    { m_controller.record("FlipView page: " + std::to_string(index)); });
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
        if (previewMenu || previewMenuBar || previewCommands || previewPanes || previewRating ||
            previewCommandFlyout || previewChoices || previewPanels)
        {
            ui.update(GetAppWindow()->getWidth(), GetAppWindow()->getHeight());
            if (previewPanels)
            {
                if (previewPanels == 5)
                {
                    const auto editor = ui.templateChild(ui.tree().element(*search), "TextBox");
                    ui.focus(editor);
                    search->setIsSuggestionListOpen(true);
                    if (!editor || !search->isSuggestionListOpen() || ui.focused() != editor ||
                        search->suggestions().size() != 3)
                        throw std::runtime_error("AutoSuggestBox startup failed");
                }
                else if (previewPanels == 4)
                {
                    if (tabs->selectedItem() != documentTab || !documentTab->isInTree() ||
                        !documentEditor->isInTree() || welcomeTab->isClosable())
                        throw std::runtime_error("TabView startup failed");
                }
                else if (previewPanels == 3)
                {
                    if (pivot->selectedItem() != notes || !notes->isInTree() ||
                        slides->selectedIndex() != 0 || !slides->selectedItem()->isInTree())
                        throw std::runtime_error("Page selector startup failed");
                }
                else if (previewPanels == 1)
                {
                    if (!m_dialog->showAt(ui, dialogAnchor))
                        throw std::runtime_error("ContentDialog startup: " + m_dialog->lastError());
                }
                else
                {
                    m_teachingTip->setIsOpen(true);
                    if (!m_teachingTip->isOpen())
                        throw std::runtime_error("TeachingTip startup: " + m_teachingTip->lastError());
                }
            }
            else if (previewChoices)
            {
                themeChoice->setIsDropDownOpen(true);
                if (!themeChoice->isDropDownOpen() || themeChoice->selectedItem() != "Light" ||
                    densityChoice->selectedItem() != "Compact")
                    throw std::runtime_error("Choice controls startup failed");
            }
            else if (previewCommandFlyout)
            {
                if (!m_commandFlyout->showAt(ui, commandAnchor) || !copy->isInTree() || !favorite->isInTree())
                    throw std::runtime_error("CommandBarFlyout startup failed: " +
                                             m_commandFlyout->lastError());
            }
            else if (previewRating)
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
    std::shared_ptr<CommandBarFlyout> m_commandFlyout;
    std::shared_ptr<ContentDialog> m_dialog;
    std::shared_ptr<TeachingTip> m_teachingTip;
};

class FluentApp final : public XamlApplication
{
  public:
    explicit FluentApp(bool previewMenu, bool previewMenuBar, bool previewCommands, bool previewPanes,
                       bool previewRating, bool previewCommandFlyout, bool previewChoices, i32 previewPanels)
        : m_previewMenu(previewMenu), m_previewMenuBar(previewMenuBar), m_previewCommands(previewCommands),
          m_previewPanes(previewPanes), m_previewRating(previewRating),
          m_previewCommandFlyout(previewCommandFlyout), m_previewChoices(previewChoices),
          m_previewPanels(previewPanels)
    {
    }
    void OnLaunched() override
    {
        m_window = std::make_unique<FluentWindow>(*this, m_previewMenu, m_previewMenuBar, m_previewCommands,
                                                  m_previewPanes, m_previewRating, m_previewCommandFlyout,
                                                  m_previewChoices, m_previewPanels);
        m_window->Show();
    }

  private:
    bool m_previewMenu = false;
    bool m_previewMenuBar = false;
    bool m_previewCommands = false;
    bool m_previewPanes = false;
    bool m_previewRating = false;
    bool m_previewCommandFlyout = false;
    bool m_previewChoices = false;
    i32 m_previewPanels = 0;
    std::unique_ptr<FluentWindow> m_window;
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR arguments, int)
{
    const std::string_view args(arguments);
    const bool previewMenuBar = args.find("--open-menubar") != std::string_view::npos;
    const bool previewCommands = args.find("--open-commandbar") != std::string_view::npos;
    FluentApp app(!previewMenuBar && args.find("--open-menu") != std::string_view::npos, previewMenuBar,
                  previewCommands, args.find("--preview-panes") != std::string_view::npos,
                  args.find("--preview-rating") != std::string_view::npos,
                  args.find("--open-commandflyout") != std::string_view::npos,
                  args.find("--preview-choices") != std::string_view::npos,
                  args.find("--open-dialog") != std::string_view::npos           ? 1
                  : args.find("--open-teachingtip") != std::string_view::npos    ? 2
                  : args.find("--preview-pages") != std::string_view::npos       ? 3
                  : args.find("--preview-tabs") != std::string_view::npos        ? 4
                  : args.find("--preview-suggestions") != std::string_view::npos ? 5
                                                                                 : 0);
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
