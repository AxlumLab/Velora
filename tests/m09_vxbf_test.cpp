// R12.1: M09 acceptance through compiled .vxbf and XamlLoader, headless:
// x:Bind change notification and TwoWay loop protection, {Binding} with DataContext switches,
// TextBox IME composition, ListView realization/recycling, lifetime and two independent
// Framework instances (windows) over one shared model.
#include "BindingPage.g.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/XamlItems.hpp>
#include <velora/runtime/Framework.hpp>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const std::string& name)
{
    std::printf("m09 %s: %s\n", name.c_str(), passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

constexpr float kWidth = 900, kHeight = 400;

struct Window
{
    Compositor compositor;
    Framework ui{compositor};
    M09::Page controller;
    ElementId root = 0;
    struct Names
    {
        TypedElementId<ElementKind::kTextBox> QueryBox, CardEdit;
        TypedElementId<ElementKind::kTextBlock> SummaryText, SelectedName, CardName, CardAge;
        TypedElementId<ElementKind::kBorder> Card;
        TypedElementId<ElementKind::kListView> People;
        TypedElementId<ElementKind::kScrollViewer> Details;
    } ids;
    explicit Window(M09::Model& model) : controller(model)
    {
        M09::Page_component::Names names;
        const auto loaded = M09::Page_component::initializeComponent(ui, controller, names);
        if (!loaded.ok)
            throw std::runtime_error(loaded.error);
        root = ui.tree().element(*loaded.root);
#define NAME(member) ids.member = decltype(ids.member){ui.tree().element(*names.member)}
        NAME(QueryBox);
        NAME(CardEdit);
        NAME(SummaryText);
        NAME(SelectedName);
        NAME(CardName);
        NAME(CardAge);
        NAME(Card);
        NAME(People);
        NAME(Details);
#undef NAME
        update();
    }
    ~Window()
    {
        close();
    }
    void close()
    {
        if (root)
            ui.tree().remove(std::exchange(root, 0));
    }
    XamlItemsHost& items()
    {
        return *XamlItemsHost::find(ui, ids.People);
    }
    ElementId itemName(usize index, const char* name)
    {
        return ui.templateChild(items().container(index), name);
    }
    void update()
    {
        ui.update(kWidth, kHeight);
    }
    std::string text(ElementId e) const
    {
        const auto& v = ui.tree().property(e, *ui.props().Text);
        return v.isString() ? v.asString() : "<unset>";
    }
    void click(ElementId e)
    {
        const auto b = ui.bounds(e);
        ui.pointerDown(b.x + b.width / 2, b.y + b.height / 2);
        ui.pointerUp(b.x + b.width / 2, b.y + b.height / 2);
        update();
    }
    const Names& names() const
    {
        return ids;
    }
};

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // keep every line if a check crashes
    M09::Model model;
    for (int i = 0; i < 1000; ++i)
        model.people().emplace_back("Person " + std::to_string(i), 20 + i % 50);
    auto a = std::make_unique<Window>(model);
    auto& w = *a;
    const auto& n = w.names();

    check(w.text(n.SummaryText) == "Type to search" && w.text(n.SelectedName) == "Alice" &&
              w.text(n.CardName) == "Alice" && w.text(n.CardAge) == "30",
          "initial x:Bind and {Binding} values");

    // ---- TwoWay x:Bind and IME -------------------------------------------------------
    w.click(n.QueryBox);
    w.ui.textEditing("ni", 2);
    w.update();
    check(model.query().empty() && model.setQueryCalls == 0 && w.text(n.QueryBox).empty(),
          "preedit is not written to the TwoWay source");
    check(w.ui.tree().property(n.QueryBox, *w.ui.props().PreeditText).asString() == "ni",
          "preedit is shown in the TextBox");
    const Box caret = w.ui.caretBounds(), box = w.ui.bounds(n.QueryBox);
    check(caret.height > 0 && caret.x > box.x && box.contains(caret.x, caret.y + 1),
          "caret bounds for the IME candidate window");
    w.ui.key(Key::kBackspace); // the IME owns keys during composition
    check(w.ui.tree().property(n.QueryBox, *w.ui.props().PreeditText).asString() == "ni",
          "keys go to the IME while composing");
    w.ui.textInput("\xE4\xBD\xA0\xE5\xA5\xBD"); // 你好
    w.update();
    check(model.query() == "\xE4\xBD\xA0\xE5\xA5\xBD" && model.setQueryCalls == 1,
          "commit writes the source exactly once");
    check(w.text(n.SummaryText) == "Searching for \xE4\xBD\xA0\xE5\xA5\xBD",
          "OneWay target follows the notification without updateBindings");
    check(w.ui.tree().property(n.QueryBox, *w.ui.props().PreeditText).asString().empty(),
          "commit ends the composition");

    w.ui.textInput("abcdefghij"); // 6 + 10 bytes, the source keeps 12
    w.update();
    check(model.setQueryCalls == 2 && model.query().size() == 12 && w.text(n.QueryBox) == model.query(),
          "coerced source value is shown back without a write loop");
    w.ui.key(Key::kA, false, true);
    w.ui.textInput("x");
    check(model.query() == "x" && model.setQueryCalls == 3, "typing replaces the selection");
    w.ui.textInput("yz");
    w.ui.key(Key::kLeft);
    w.ui.key(Key::kLeft, true);
    w.ui.key(Key::kDelete);
    check(model.query() == "xz", "caret movement, shift selection and delete");
    w.ui.key(Key::kHome);
    w.ui.key(Key::kEnd, true);
    w.ui.key(Key::kBackspace);
    check(model.query().empty(), "home, shift+end and backspace");
    const int calls = model.setQueryCalls;
    model.setQuery("model side");
    w.update();
    check(w.text(n.QueryBox) == "model side" && model.setQueryCalls == calls + 1,
          "source change updates the TextBox without writing back");

    // Moving focus completes a composition.
    w.ui.key(Key::kEnd);
    w.ui.textEditing("!", 1);
    w.click(n.CardEdit);
    check(model.query() == "model side!" && w.ui.focused() == n.CardEdit,
          "focus change commits the composition");

    // ---- Path re-subscription and {Binding} DataContext switch -------------------------
    model.selectBob(true);
    w.update();
    check(w.text(n.SelectedName) == "Bob" && w.text(n.CardName) == "Bob" && w.text(n.CardAge) == "40",
          "Selected switch re-tracks x:Bind and DataContext");
    check(model.alice().subscriberCount() == 0, "no subscription is left on the previous DataContext");
    model.alice().setName("Alicia");
    w.update();
    check(w.text(n.SelectedName) == "Bob" && w.text(n.CardName) == "Bob",
          "the old object no longer drives the view");
    model.bob().birthday();
    w.update();
    check(w.text(n.CardAge) == "41", "{Binding} follows the new DataContext");
    w.ui.textInput("by");
    w.update();
    check(model.bob().name() == "Bobby" && w.text(n.SelectedName) == "Bobby" && w.text(n.CardName) == "Bobby",
          "TwoWay {Binding} writes back");
    const auto stats = w.ui.bindingStats();
    const auto containers = a->items().realizedCount() + a->items().pooledCount();
    check(stats.bindings == 3 + containers && w.ui.bindingErrors().empty(),
          "one {Binding} per page target and container, no errors");
    const auto lookups = stats.accessorLookups;
    for (int i = 0; i < 20; ++i)
        model.bob().birthday();
    check(w.ui.bindingStats().accessorLookups == lookups && w.text(n.CardAge) == "61",
          "re-evaluation uses the cached accessors");

    // ---- ListView realization and recycling -----------------------------------------
    auto& items = a->items();
    const auto v = w.ui.viewport(n.People);
    const std::size_t visible = static_cast<std::size_t>(v.height / 32) + 1;
    check(v.count == 1000 && items.realizedCount() <= visible + 1 && items.realizedCount() >= visible - 1,
          "only the visible range is realized");
    const auto shows = [&](std::size_t index)
    {
        const auto c = items.container(index);
        return c &&
               w.ui.tree().property(c, *w.ui.props().DataContext).asObject().object ==
                   &model.people()[index] &&
               w.text(w.itemName(index, "Label")) == model.people()[index].name() &&
               w.text(w.itemName(index, "AgeText")) == std::to_string(model.people()[index].age());
    };
    check(shows(0) && shows(5), "containers show their items (x:Bind and {Binding} in the template)");
    const auto nodesBefore = w.ui.tree().node_count();
    bool allShown = true;
    std::size_t maxContainers = 0, maxHandlers = 0;
    for (double offset = 0; offset <= 32 * 1000; offset += 157)
    {
        w.ui.scrollTo(n.People, offset);
        w.update();
        const auto vp = w.ui.viewport(n.People);
        const auto first = static_cast<std::size_t>(vp.offset / 32);
        allShown = allShown && shows(first) && shows(std::min<std::size_t>(999, first + 3));
        maxContainers = std::max(maxContainers, items.realizedCount() + items.pooledCount());
        maxHandlers = std::max(maxHandlers, w.ui.tree().stats().handlers);
    }
    check(allShown, "scrolling through 1000 items shows the right item in every container");
    check(maxContainers <= visible + 1 + 8 && w.ui.tree().node_count() <= nodesBefore + 8 * 4,
          "containers are recycled, not created per item");
    check(maxHandlers <= (visible + 1 + 8) + 3, "handler count stays bounded while scrolling");
    check(model.people()[0].subscriberCount() == 0 && model.people()[3].subscriberCount() == 0,
          "recycled containers keep no subscription to old items");
    w.ui.scrollTo(n.People, 0);
    w.update();
    model.people()[1].setName("Renamed");
    model.people()[1].birthday();
    check(w.text(w.itemName(1, "Label")) == "Renamed" &&
              w.text(w.itemName(1, "AgeText")) == std::to_string(model.people()[1].age()),
          "item changes reach their realized container");

    // Remove through the template's handler: it receives the container's current item.
    const std::string second = model.people()[2].name();
    w.click(w.itemName(1, "RemoveButton"));
    check(a->controller.removed.back() == "Renamed" && model.people().size() == 999 && shows(1) &&
              model.people()[1].name() == second,
          "item handler removes its own item; containers shift");
    w.click(w.itemName(1, "RemoveButton"));
    check(a->controller.removed.back() == second && model.people().size() == 998,
          "a reused container acts on its new item");
    model.people().erase(0); // removing a realized item from code
    w.update();
    check(shows(0) && shows(3), "removal of a realized item rebinds the range");
    model.people().emplace(0, "Inserted", 1);
    w.update();
    check(shows(0) && w.text(w.itemName(0, "Label")) == "Inserted" && shows(1),
          "insertion shifts realized containers");
    model.people().replace(2, "Replaced", 2);
    w.update();
    check(shows(2), "replacement rebinds its container");

    // ---- Named elements share the retained tree's actual objects -------------------------
    const ElementId removeButton = w.itemName(0, "RemoveButton");
    check(w.ui.tree().is(removeButton, ElementKind::kButton) &&
              w.ui.tree().property(removeButton, *w.ui.props().Content).asString() == "Remove",
          "an item template name identifies the Button and its XAML-set Content");
    const auto card = w.ui.tree().object(n.Card);
    check(card && w.ui.tree().is(n.Card, ElementKind::kBorder) &&
              card == w.ui.tree().object(n.Card) && w.ui.tree().element(*card) == n.Card,
          "a page name identifies one retained Border object");
    const auto selected = w.ui.tree().object(n.SelectedName);
    w.ui.set(n.SelectedName, w.ui.props().Text, PropertyValue(std::string("Typed")));
    check(selected && w.ui.tree().is(n.SelectedName, ElementKind::kTextBlock) &&
              selected->getValue(*w.ui.props().Text).asString() == "Typed" && w.text(n.SelectedName) == "Typed",
          "Framework writes and the named C++ object read the same property");
    model.selectBob(true);
    model.selectBob(false);
    w.update();
    check(w.text(n.SelectedName) == model.selected().name(),
          "re-applying a OneWay x:Bind overwrites a local property write");

    // The details column sits in a ScrollViewer: everything fits, so there is nothing to scroll.
    const double extent = w.ui.tree().property(n.Details, *w.ui.props().ExtentHeight).asDouble();
    const double viewport = w.ui.tree().property(n.Details, *w.ui.props().ViewportHeight).asDouble();
    check(w.ui.tree().is(n.Details, ElementKind::kScrollViewer) && viewport > 0 && extent == viewport &&
              w.ui.bounds(n.Details).height == w.ui.bounds(n.People).height,
          "a generated ScrollViewer fills its cell and measures its content");

    // ---- Two windows over one model ------------------------------------------------------
    auto b = std::make_unique<Window>(model);
    w.click(n.QueryBox);
    w.ui.key(Key::kEnd);
    w.ui.textEditing("q", 1);
    w.update();
    b->update();
    check(b->ui.focused() == 0 &&
              b->ui.tree().property(b->names().QueryBox, *b->ui.props().PreeditText).asString().empty(),
          "focus and composition stay in their window");
    w.ui.textInput("?");
    w.update();
    b->update();
    check(b->text(b->names().QueryBox) == model.query() &&
              b->text(b->names().SummaryText) == "Searching for " + model.query(),
          "a commit in one window updates the other through the model");
    b->ui.scrollTo(b->names().People, 320);
    b->update();
    check(w.ui.viewport(n.People).offset == 0 && b->ui.viewport(b->names().People).offset == 320,
          "scroll position stays in its window");

    // ---- Lifetime ----------------------------------------------------------------------
    b.reset();
    check(model.subscriberCount() > 0, "the remaining window still observes the model");
    a->close();
    check(a->ui.tree().node_count() == 0 && a->ui.bindingStats().bindings == 0,
          "destroying the view removes elements and bindings");
    check(!a->ui.tree().contains(n.SelectedName) && !a->ui.tree().contains(removeButton) &&
              selected && a->ui.tree().element(*selected) == 0,
          "named elements detach from the tree when their view is removed");
    check(model.subscriberCount() == 0 && model.bob().subscriberCount() == 0 &&
              model.people().subscriberCount() == 0,
          "no subscription outlives the views");
    std::size_t itemSubscribers = 0;
    for (std::size_t i = 0; i < model.people().size(); ++i)
        itemSubscribers += model.people()[i].subscriberCount();
    check(itemSubscribers == 0, "no item keeps a subscription after the view is gone");
    model.setQuery("after");
    model.selectBob(false);
    model.people().clear();
    check(true, "model changes after the views are gone are harmless");

    // A source object destroyed before its subscriber: the subscription simply goes inert.
    {
        Subscription sub;
        {
            M09::Person temp("Temp", 1);
            sub = observe(temp, propertyId("Name"), [] {});
            check(sub.active(), "subscription is active while its source lives");
        }
        check(!sub.active(), "subscription goes inert when the source dies first");
    }
    // Errors are reported, not thrown.
    {
        Compositor c;
        Framework ui(c);
        M09::Person person("P", 2);
        const ElementId t = ui.create(ElementKind::kTextBlock);
        ui.set(t, ui.props().DataContext, PropertyValue(ObjectRef::borrow(person)));
        ui.bind(t, ui.props().Text, "Missing");
        check(ui.bindingErrors().size() == 1 &&
                  ui.bindingErrors()[0].find("no property 'Missing'") != std::string::npos,
              "an unknown {Binding} path is a reported binding error");
    }

    std::printf("m09 exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
