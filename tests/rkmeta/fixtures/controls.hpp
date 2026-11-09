#pragma once

#include "velora/graphics/Types.hpp"
#include "velora/reflect/Markers.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <compare>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace velora::fluent {

enum class Key { Left, Right, Up, Down, Enter, Space, Escape, Tab };
struct ControlState {
  bool enabled = true, hovered = false, pressed = false, focused = false;
};

rkstruct Text {
  std::string value;
  float font_size = 16;
  bool wrap = true;
};
struct Image {
  std::string path;
  enum class Fit { Contain, Cover, Stretch } fit = Fit::Contain;
};
rkstruct Panel {
  velora::Color fill{0.98f, 0.98f, 0.98f, 1.0f};
  velora::Color border{1.0f, 1.0f, 1.0f, 1.0f};
  float radius = 8;
  float padding = 0;
  velora::Rect content_bounds(velora::Rect bounds) const noexcept {
    const float inset = std::clamp(padding, 0.f,
                                   std::min(bounds.width, bounds.height) / 2.f);
    return {bounds.x + inset, bounds.y + inset, bounds.width - 2 * inset,
            bounds.height - 2 * inset};
  }
};
rkstruct Card : Panel {
  float elevation = 2;
  bool clip = true;
};
struct LayoutDebugOverlay {
  bool enabled = false;
};

rkclass Button {
  std::string text_;
  std::function<void()> on_click_;
  ControlState state_;

public:
  rkproperty(Content, std::string_view, get = text, Measure, Paint, Content)
  rkproperty(IsEnabled, bool, get = enabled, set = set_enabled, default = true, Paint)
  rkevent(Click, void(), add = set_on_click)

  explicit Button(std::string text, std::function<void()> on_click = {})
      : text_(std::move(text)), on_click_(std::move(on_click)) {}
  void pointer_down() {
    if (state_.enabled)
      state_.pressed = true;
  }
  void pointer_up(bool inside = true) {
    if (inside && state_.pressed && on_click_)
      on_click_();
    state_.pressed = false;
  }
  void key(Key key) {
    if (state_.enabled && (key == Key::Enter || key == Key::Space) && on_click_)
      on_click_();
  }
  void set_enabled(bool value) {
    state_.enabled = value;
    if (!value)
      state_.pressed = false;
  }
  void set_hovered(bool value) { state_.hovered = value; }
  void set_focused(bool value) { state_.focused = value; }
  void set_on_click(std::function<void()> callback) { on_click_ = std::move(callback); }
  [[nodiscard]] bool enabled() const noexcept { return state_.enabled; }
  [[nodiscard]] const ControlState &state() const noexcept { return state_; }
  [[nodiscard]] std::string_view text() const noexcept { return text_; }
};

rkclass ToggleSwitch {
  bool checked_ = false;
  ControlState state_;
  std::function<void(bool)> on_change_;

public:
  rkproperty(IsChecked, bool, get = checked, set = set_checked, default = false, Paint)
  rkevent(Toggled, void(bool), add = set_on_change)

  explicit ToggleSwitch(bool checked = false) : checked_(checked) {}
  void toggle() {
    if (state_.enabled) {
      checked_ = !checked_;
      if (on_change_)
        on_change_(checked_);
    }
  }
  void key(Key key) {
    if (key == Key::Space || key == Key::Enter)
      toggle();
  }
  [[nodiscard]] bool checked() const noexcept { return checked_; }
  void set_checked(bool value) {
    if (checked_ == value)
      return;
    checked_ = value;
    if (on_change_)
      on_change_(checked_);
  }
  void set_enabled(bool value) { state_.enabled = value; }
  void set_hovered(bool value) { state_.hovered = value; }
  void set_focused(bool value) { state_.focused = value; }
  void set_on_change(std::function<void(bool)> callback) {
    on_change_ = std::move(callback);
  }
  [[nodiscard]] const ControlState &state() const noexcept { return state_; }
};

rkclass CheckBox : public ToggleSwitch {
  using ToggleSwitch::ToggleSwitch;
};
class RadioButton : public ToggleSwitch {
  using ToggleSwitch::ToggleSwitch;
};

class RadioGroup {
  std::vector<bool> enabled_;
  std::size_t selected_ = 0;
  std::function<void(std::size_t)> on_change_;

public:
  explicit RadioGroup(std::size_t count = 0) : enabled_(count, true) {}
  std::size_t size() const noexcept { return enabled_.size(); }
  std::size_t selected() const noexcept { return selected_; }
  bool checked(std::size_t index) const noexcept {
    return index < enabled_.size() && index == selected_;
  }
  bool enabled(std::size_t index) const noexcept {
    return index < enabled_.size() && enabled_[index];
  }
  void set_enabled(std::size_t index, bool value) {
    if (index < enabled_.size())
      enabled_[index] = value;
  }
  void select(std::size_t index) {
    if (!enabled(index) || index == selected_)
      return;
    selected_ = index;
    if (on_change_)
      on_change_(selected_);
  }
  void set_on_change(std::function<void(std::size_t)> callback) {
    on_change_ = std::move(callback);
  }
  void key(Key key) {
    if (enabled_.empty())
      return;
    if (key != Key::Left && key != Key::Right && key != Key::Up &&
        key != Key::Down)
      return;
    const int direction = (key == Key::Left || key == Key::Up) ? -1 : 1;
    auto index = static_cast<int>(selected_);
    for (std::size_t i = 0; i < enabled_.size(); ++i) {
      index = (index + direction + static_cast<int>(enabled_.size())) %
              static_cast<int>(enabled_.size());
      if (enabled(static_cast<std::size_t>(index))) {
        select(static_cast<std::size_t>(index));
        return;
      }
    }
  }
};

rkclass Slider {
  float value_ = 0, minimum_ = 0, maximum_ = 1;
  ControlState state_;
  std::function<void(float)> on_change_;

public:
  rkproperty(Value, float, get = value, set = set, default = 0, Paint)
  rkproperty(Minimum, float, get = minimum, default = 0, Paint)
  rkproperty(Maximum, float, get = maximum, default = 1, Paint)
  rkevent(ValueChanged, void(float), add = set_on_change)

  Slider(float minimum = 0, float maximum = 1, float value = 0)
      : value_(value), minimum_(std::min(minimum, maximum)),
        maximum_(std::max(minimum, maximum)) {
    set(value);
  }
  void set(float value) {
    if (!state_.enabled)
      return;
    const float next = std::clamp(value, minimum_, maximum_);
    if (next == value_)
      return;
    value_ = next;
    if (on_change_)
      on_change_(value_);
  }
  void key(Key key) {
    float step = (maximum_ - minimum_) / 100.f;
    if (key == Key::Left || key == Key::Down)
      set(value_ - step);
    if (key == Key::Right || key == Key::Up)
      set(value_ + step);
  }
  void pointer_value(float position, float start, float end) {
    if (!state_.enabled || end <= start)
      return;
    const float fraction = std::clamp((position - start) / (end - start), 0.f,
                                      1.f);
    set(minimum_ + fraction * (maximum_ - minimum_));
  }
  void wheel(float delta) {
    set(value_ + delta * (maximum_ - minimum_) / 100.f);
  }
  [[nodiscard]] float value() const noexcept { return value_; }
  [[nodiscard]] float minimum() const noexcept { return minimum_; }
  [[nodiscard]] float maximum() const noexcept { return maximum_; }
  void set_enabled(bool value) {
    state_.enabled = value;
  }
  void set_hovered(bool value) { state_.hovered = value; }
  void set_focused(bool value) { state_.focused = value; }
  void set_on_change(std::function<void(float)> callback) {
    on_change_ = std::move(callback);
  }
  [[nodiscard]] const ControlState &state() const noexcept { return state_; }
};

rkclass ProgressBar {
  float value_ = 0;
  float transition_ = 1;
  std::function<void(float)> on_change_;

public:
  rkproperty(Value, float, get = value, set = set, default = 0, Paint)
  rkevent(ValueChanged, void(float), add = set_on_change)

  explicit ProgressBar(float value = 0) : value_(std::clamp(value, 0.f, 1.f)) {}
  void set(float value) {
    const float next = std::clamp(value, 0.f, 1.f);
    if (next == value_)
      return;
    value_ = next;
    transition_ = 0;
    if (on_change_)
      on_change_(value_);
  }
  float value() const noexcept { return value_; }
  void set_on_change(std::function<void(float)> callback) {
    on_change_ = std::move(callback);
  }
  float animation_progress() const noexcept { return transition_; }
  void settle() noexcept { transition_ = 1; }
};
class ProgressRing : public ProgressBar {
  using ProgressBar::ProgressBar;
};

rkclass TextBox {
  std::string text_;
  std::size_t cursor_ = 0;
  bool password_ = false;
  ControlState state_;

public:
  rkproperty(Text, std::string_view, get = text, Measure, Paint)
  rkproperty(IsPassword, bool, get = password, default = false, Measure, Paint)

  explicit TextBox(bool password = false) : password_(password) {}
  void insert(std::string_view value) {
    if (state_.enabled) {
      text_.insert(cursor_, value);
      cursor_ += value.size();
    }
  }
  void key(Key key) {
    if (key == Key::Left && cursor_)
      --cursor_;
    if (key == Key::Right && cursor_ < text_.size())
      ++cursor_;
    if (key == Key::Escape)
      cursor_ = text_.size();
  }
  [[nodiscard]] std::string_view text() const noexcept { return text_; }
  [[nodiscard]] bool password() const noexcept { return password_; }
  void set_enabled(bool value) { state_.enabled = value; }
  [[nodiscard]] const ControlState &state() const noexcept { return state_; }
};
class PasswordBox : public TextBox {
public:
  PasswordBox() : TextBox(true) {}
};

class ComboBox {
  std::vector<std::string> items_;
  std::size_t selected_ = 0;
  bool open_ = false;

public:
  explicit ComboBox(std::vector<std::string> items)
      : items_(std::move(items)) {}
  void key(Key key) {
    if (key == Key::Enter || key == Key::Space)
      open_ = !open_;
    if (open_ && !items_.empty() && key == Key::Down)
      selected_ = std::min(selected_ + 1, items_.size() - 1);
    if (open_ && !items_.empty() && key == Key::Up && selected_)
      --selected_;
  }
  [[nodiscard]] bool open() const noexcept { return open_; }
  [[nodiscard]] std::string_view selected() const {
    return items_.empty() ? std::string_view{} : items_[selected_];
  }
};

// Controlled setters never emit user callbacks. Draft changes commit only on
// confirm.
class Dropdown {
  std::vector<std::string> items_;
  std::vector<bool> enabled_;
  std::size_t selected_ = 0, highlighted_ = 0, offset_ = 0, rows_ = 4;
  bool open_ = false;
  std::function<void(std::size_t)> on_change_;
  void reveal() {
    if (highlighted_ < offset_)
      offset_ = highlighted_;
    if (highlighted_ >= offset_ + rows_)
      offset_ = highlighted_ - rows_ + 1;
  }

public:
  explicit Dropdown(std::vector<std::string> items)
      : items_(std::move(items)), enabled_(items_.size(), true) {}
  void set_enabled(std::size_t i, bool value) {
    if (i < enabled_.size())
      enabled_[i] = value;
  }
  bool enabled(std::size_t i) const {
    return i < enabled_.size() && enabled_[i];
  }
  void set_selected(std::size_t i) {
    if (enabled(i)) {
      selected_ = highlighted_ = i;
      reveal();
    }
  }
  void set_on_change(std::function<void(std::size_t)> cb) {
    on_change_ = std::move(cb);
  }
  void set_viewport(std::size_t rows) {
    rows_ = std::max<std::size_t>(1, rows);
    scroll(0);
    reveal();
  }
  void scroll(int delta) {
    const int limit =
        static_cast<int>(items_.size() > rows_ ? items_.size() - rows_ : 0);
    offset_ = static_cast<std::size_t>(
        std::clamp(static_cast<int>(offset_) + delta, 0, limit));
  }
  void open() {
    open_ = true;
    highlighted_ = selected_;
    reveal();
  }
  void close() {
    open_ = false;
    highlighted_ = selected_;
  }
  void toggle() {
    if (open_)
      close();
    else
      open();
  }
  bool open_state() const noexcept { return open_; }
  void set_open(bool value) {
    if (value && !open_)
      open();
    else if (!value)
      close();
  }
  void key(Key key) {
    if (key == Key::Escape) {
      close();
      return;
    }
    if (!open_) {
      if (key == Key::Enter || key == Key::Space)
        open();
      return;
    }
    if (key == Key::Enter || key == Key::Space) {
      select(highlighted_);
      return;
    }
    if (items_.empty() || (key != Key::Up && key != Key::Down))
      return;
    auto next = highlighted_;
    for (std::size_t n = 0; n < items_.size(); ++n) {
      next = key == Key::Down ? (next + 1) % items_.size()
                              : (next + items_.size() - 1) % items_.size();
      if (enabled(next)) {
        highlighted_ = next;
        reveal();
        break;
      }
    }
  }
  void select(std::size_t i) {
    if (enabled(i)) {
      const bool changed = i != selected_;
      selected_ = highlighted_ = i;
      close();
      if (changed && on_change_)
        on_change_(i);
    }
  }
  std::size_t selected_index() const noexcept { return selected_; }
  std::size_t size() const noexcept { return items_.size(); }
  std::size_t highlighted_index() const noexcept { return highlighted_; }
  std::size_t scroll_offset() const noexcept { return offset_; }
  std::size_t viewport() const noexcept { return rows_; }
  std::size_t item_count() const noexcept { return items_.size(); }
  std::string_view item(std::size_t i) const {
    return i < items_.size() ? std::string_view(items_[i]) : std::string_view{};
  }
  std::string_view selected() const { return item(selected_); }
};

struct CalendarDate {
  int year = 2026, month = 1, day = 1;
  auto operator<=>(const CalendarDate &) const = default;
};
class DatePicker {
  CalendarDate value_, draft_, minimum_{1900, 1, 1}, maximum_{2200, 12, 31};
  bool open_ = false;
  std::function<void(int, int, int)> on_change_;
  static CalendarDate normalized(CalendarDate d) {
    d.year = std::clamp(d.year, 1, 9999);
    d.month = std::clamp(d.month, 1, 12);
    d.day = std::clamp(d.day, 1, days(d.year, d.month));
    return d;
  }
  CalendarDate bounded(CalendarDate d) const {
    return std::clamp(normalized(d), minimum_, maximum_);
  }

public:
  static int days(int year, int month) {
    constexpr int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return d[std::clamp(month, 1, 12) - 1] +
           (month == 2 &&
            ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0));
  }
  void set_range(CalendarDate minimum, CalendarDate maximum) {
    minimum = normalized(minimum);
    maximum = normalized(maximum);
    minimum_ = std::min(minimum, maximum);
    maximum_ = std::max(minimum, maximum);
    value_ = bounded(value_);
    draft_ = bounded(draft_);
  }
  void set_date(int year, int month, int day) {
    value_ = bounded({year, month, day});
    if (!open_)
      draft_ = value_;
  }
  void set_on_change(std::function<void(int, int, int)> cb) {
    on_change_ = std::move(cb);
  }
  void open() {
    open_ = true;
    draft_ = value_;
  }
  void set_open(bool value) {
    if (value && !open_)
      open();
    else if (!value)
      cancel();
  }
  void cancel() {
    draft_ = value_;
    open_ = false;
  }
  void confirm() {
    if (!open_)
      return;
    const auto previous = value_;
    value_ = bounded(draft_);
    open_ = false;
    if (previous != value_ && on_change_)
      on_change_(year(), month(), day());
  }
  bool open_state() const noexcept { return open_; }
  void next_month() {
    auto next = draft_;
    if (++next.month > 12) {
      next.month = 1;
      ++next.year;
    }
    draft_ = bounded(next);
  }
  void previous_month() {
    auto next = draft_;
    if (--next.month < 1) {
      next.month = 12;
      --next.year;
    }
    draft_ = bounded(next);
  }
  bool day_enabled(int day) const {
    const CalendarDate d{draft_.year, draft_.month, day};
    return day > 0 && day <= draft_days() && d >= minimum_ && d <= maximum_;
  }
  void select_day(int day) {
    draft_ = bounded({draft_.year, draft_.month, day});
  }
  void today() {
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    draft_ = bounded({local.tm_year + 1900, local.tm_mon + 1, local.tm_mday});
  }
  int year() const noexcept { return value_.year; }
  int month() const noexcept { return value_.month; }
  int day() const noexcept { return value_.day; }
  int draft_year() const noexcept { return draft_.year; }
  int draft_month() const noexcept { return draft_.month; }
  int draft_day() const noexcept { return draft_.day; }
  int draft_days() const noexcept { return days(draft_.year, draft_.month); }
};

class TimePicker {
  int hour_ = 12, minute_ = 0, draft_hour_ = 12, draft_minute_ = 0, step_ = 1;
  bool open_ = false;
  std::function<void(int, int)> on_change_;

public:
  explicit TimePicker(int minute_step = 1)
      : step_(std::clamp(minute_step, 1, 30)) {}
  void set_time(int h, int m) {
    hour_ = std::clamp(h, 0, 23);
    minute_ = std::clamp(m, 0, 59);
  }
  void set_on_change(std::function<void(int, int)> cb) {
    on_change_ = std::move(cb);
  }
  void open() {
    open_ = true;
    draft_hour_ = hour_;
    draft_minute_ = minute_;
  }
  void set_open(bool value) {
    if (value && !open_)
      open();
    else if (!value)
      cancel();
  }
  void cancel() {
    draft_hour_ = hour_;
    draft_minute_ = minute_;
    open_ = false;
  }
  void confirm() {
    if (!open_)
      return;
    const int h = std::clamp(draft_hour_, 0, 23),
              m = (std::clamp(draft_minute_, 0, 59) / step_) * step_;
    const bool changed = h != hour_ || m != minute_;
    hour_ = h;
    minute_ = m;
    open_ = false;
    if (changed && on_change_)
      on_change_(hour_, minute_);
  }
  bool open_state() const noexcept { return open_; }
  void set_draft(int h, int m) {
    draft_hour_ = std::clamp(h, 0, 23);
    draft_minute_ = std::clamp(m, 0, 59);
  }
  int hour() const noexcept { return hour_; }
  int minute() const noexcept { return minute_; }
  int draft_hour() const noexcept { return draft_hour_; }
  int draft_minute() const noexcept { return draft_minute_; }
  int minute_step() const noexcept { return step_; }
  std::string formatted(bool draft = false) const {
    char text[6];
    std::snprintf(text, sizeof(text), "%02d:%02d", draft ? draft_hour_ : hour_,
                  draft ? draft_minute_ : minute_);
    return text;
  }
};

class ColorPicker {
  Color color_{1, 1, 1, 1}, draft_{1, 1, 1, 1};
  bool open_ = false;
  std::function<void(Color)> on_change_;

public:
  void set_color(Color c) {
    const auto previous = draft_;
    set_draft(c);
    color_ = draft_;
    if (open_)
      draft_ = previous;
  }
  void set_on_change(std::function<void(Color)> cb) {
    on_change_ = std::move(cb);
  }
  void open() {
    open_ = true;
    draft_ = color_;
  }
  void set_open(bool value) {
    if (value && !open_)
      open();
    else if (!value)
      cancel();
  }
  void cancel() {
    draft_ = color_;
    open_ = false;
  }
  void confirm() {
    if (!open_)
      return;
    const bool changed = color_.r != draft_.r || color_.g != draft_.g ||
                         color_.b != draft_.b || color_.a != draft_.a;
    color_ = draft_;
    open_ = false;
    if (changed && on_change_)
      on_change_(color_);
  }
  bool open_state() const noexcept { return open_; }
  void set_draft(Color c) {
    draft_ = {std::clamp(c.r, 0.f, 1.f), std::clamp(c.g, 0.f, 1.f),
              std::clamp(c.b, 0.f, 1.f), std::clamp(c.a, 0.f, 1.f)};
  }
  void set_alpha(float alpha) { draft_.a = std::clamp(alpha, 0.f, 1.f); }
  Color color() const noexcept { return color_; }
  Color draft() const noexcept { return draft_; }
};

class Segmented {
  std::vector<std::string> items_;
  std::size_t selected_ = 0;
  bool enabled_ = true;
  ControlState state_;
  std::function<void(std::size_t)> on_change_;

public:
  explicit Segmented(std::vector<std::string> items)
      : items_(std::move(items)) {}
  void select(std::size_t index) {
    if (enabled_ && index < items_.size()) {
      selected_ = index;
      if (on_change_)
        on_change_(index);
    }
  }
  void key(Key key) {
    if (!enabled_)
      return;
    if (key == Key::Left && selected_)
      select(selected_ - 1);
    if (key == Key::Right && selected_ + 1 < items_.size())
      select(selected_ + 1);
  }
  std::size_t selected_index() const noexcept { return selected_; }
  std::size_t size() const noexcept { return items_.size(); }
  std::string_view selected() const {
    return items_.empty() ? std::string_view{} : items_[selected_];
  }
  void set_on_change(std::function<void(std::size_t)> callback) {
    on_change_ = std::move(callback);
  }
  void set_enabled(bool value) {
    enabled_ = value;
    state_.enabled = value;
  }
  bool enabled() const noexcept { return enabled_; }
  void set_hovered(bool value) { state_.hovered = value; }
  void set_focused(bool value) { state_.focused = value; }
  const ControlState &state() const noexcept { return state_; }
};

class Stepper {
  int value_ = 0, minimum_ = 0, maximum_ = 100, step_ = 1, base_ = 10;
  int digits_ = 0, bit_width_ = 0;
  std::string prefix_;
  bool enabled_ = true;
  std::function<void(int)> on_change_;
  void change(int value) {
    if (!enabled_)
      return;
    int before = value_;
    set(value);
    if (before != value_ && on_change_)
      on_change_(value_);
  }

public:
  Stepper(int value = 0, int minimum = 0, int maximum = 100, int step = 1)
      : value_(value), minimum_(minimum), maximum_(std::max(minimum, maximum)),
        step_(std::max(1, step)) {
    set(value);
  }
  void set(int value) { value_ = std::clamp(value, minimum_, maximum_); }
  void increment() {
    change(static_cast<int>(
        std::min<long long>(maximum_, static_cast<long long>(value_) + step_)));
  }
  void decrement() {
    change(static_cast<int>(
        std::max<long long>(minimum_, static_cast<long long>(value_) - step_)));
  }
  void key(Key key) {
    if (key == Key::Right || key == Key::Up)
      increment();
    if (key == Key::Left || key == Key::Down)
      decrement();
  }
  void set_base(int base) { base_ = std::clamp(base, 2, 36); }
  int base() const noexcept { return base_; }
  void set_digits(int digits) { digits_ = std::max(0, digits); }
  int digits() const noexcept { return digits_; }
  void set_prefix(std::string prefix) { prefix_ = std::move(prefix); }
  std::string_view prefix() const noexcept { return prefix_; }
  void set_bit_width(int width) { bit_width_ = std::clamp(width, 0, 64); }
  int bit_width() const noexcept { return bit_width_; }
  void set_on_change(std::function<void(int)> callback) {
    on_change_ = std::move(callback);
  }
  std::string formatted() const {
    char buffer[40];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value_, base_);
    std::string value(buffer, result.ptr);
    for (char &c : value)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (digits_ > 0 && value.size() < static_cast<std::size_t>(digits_))
      value.insert(0, static_cast<std::size_t>(digits_) - value.size(), '0');
    return prefix_ + value;
  }
  bool edit(std::string_view text) {
    int value = 0;
    auto result =
        std::from_chars(text.data(), text.data() + text.size(), value, base_);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value < minimum_ || value > maximum_)
      return false;
    change(value);
    return true;
  }
  int value() const noexcept { return value_; }
  int minimum() const noexcept { return minimum_; }
  int maximum() const noexcept { return maximum_; }
  void set_enabled(bool value) { enabled_ = value; }
  bool enabled() const noexcept { return enabled_; }
};

class Tabs {
  std::vector<std::string> labels_;
  std::vector<std::string> contents_;
  std::vector<float> scroll_offsets_;
  std::size_t selected_ = 0;
  bool enabled_ = true;
  ControlState state_;
  std::function<void(std::size_t)> on_change_;

public:
  explicit Tabs(std::vector<std::string> labels)
      : labels_(std::move(labels)), contents_(labels_),
        scroll_offsets_(labels_.size(), 0) {}
  void select(std::size_t index) {
    if (enabled_ && index < labels_.size()) {
      selected_ = index;
      if (on_change_)
        on_change_(index);
    }
  }
  void key(Key key) {
    if (!enabled_)
      return;
    if (key == Key::Left && selected_)
      select(selected_ - 1);
    if (key == Key::Right && selected_ + 1 < labels_.size())
      select(selected_ + 1);
  }
  std::size_t selected_index() const noexcept { return selected_; }
  std::size_t size() const noexcept { return labels_.size(); }
  std::string_view selected() const {
    return labels_.empty() ? std::string_view{} : labels_[selected_];
  }
  void set_content(std::size_t index, std::string content) {
    if (index < contents_.size())
      contents_[index] = std::move(content);
  }
  std::string_view content() const {
    return contents_.empty() ? std::string_view{} : contents_[selected_];
  }
  void set_scroll_offset(std::size_t index, float offset) {
    if (index < scroll_offsets_.size())
      scroll_offsets_[index] = std::max(0.f, offset);
  }
  float scroll_offset() const noexcept {
    return selected_ < scroll_offsets_.size() ? scroll_offsets_[selected_] : 0;
  }
  void set_on_change(std::function<void(std::size_t)> callback) {
    on_change_ = std::move(callback);
  }
  void set_enabled(bool value) {
    enabled_ = value;
    state_.enabled = value;
  }
  bool enabled() const noexcept { return enabled_; }
  void set_hovered(bool value) { state_.hovered = value; }
  void set_focused(bool value) { state_.focused = value; }
  const ControlState &state() const noexcept { return state_; }
};

class NavigationView {
  std::vector<std::string> routes_;
  std::size_t current_ = 0;

public:
  explicit NavigationView(std::vector<std::string> routes)
      : routes_(std::move(routes)) {}
  void back() {
    if (current_)
      --current_;
  }
  void next() {
    if (current_ + 1 < routes_.size())
      ++current_;
  }
  std::string_view route() const {
    return routes_.empty() ? std::string_view{} : routes_[current_];
  }
};

} // namespace velora::fluent
