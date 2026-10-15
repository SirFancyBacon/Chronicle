# UI Framework (`port/src/ui_framework.{hpp,cpp}`)

`UI` is Chronicle's native 2D menu and widget framework in `port/src/ui_framework.hpp` and
`port/src/ui_framework.cpp`. It provides procedural and retail-backed window surfaces, Godot-style
container anchoring, artifact-free scaled TrueType text, unified gamepad/mouse list views, callback-driven
modal dialogs, and deterministic Z-layer ordering in the game's `640x480` logical coordinate space.

---

## 1. Core Principles & Coordinate Space

- **Logical Canvas**: All coordinates are in the game's `640x480` logical 2D space (`UI::Rect::Screen()`).
  Aspect-ratio adjustments (`video.aspect`) and `video.ui_scale` are applied automatically by the underlying
  `draw2d` / `gfx::GetUiMapping` pipeline.
- **Alpha Convention**: Opacity follows the PS2 GS convention where `0x80` (`128`) is full opacity and `0`
  is fully transparent.
- **Deterministic Z-Order**: In immediate-mode 2D rendering, elements drawn later appear on top. Always
  draw backgrounds first, then dividers/recessed troughs, then icons and text, and finally selection
  brackets (`UI::Brackets`), modals (`UI::DialogBox`), and the pointing hand (`UI::Cursor`)—or use
  `UI::LayerQueue` (`DeferDraw`) to guarantee ordering automatically.

---

## 2. Standard Metrics (`UI::Metrics`)

Use `UI::Metrics` constants rather than magic numbers so menus share consistent proportions:

| Constant | Value | Purpose |
|---|---|---|
| `Metrics::kScreenWidth`, `kScreenHeight` | `640`, `480` | Full logical screen bounds |
| `Metrics::kScaleButton` | `1.08f` | Standard beveled button label scale (with `xscale = 1.25f`) |
| `Metrics::kScaleNormal` | `1.00f` | Default unscaled message/category font |
| `Metrics::kScaleHeader` | `0.92f` | Section headers (`SETTLEMENTS`, `Area Total`, scroll banners) |
| `Metrics::kScaleBody` | `0.84f` | Dense table rows, counters, and breakdown labels |
| `Metrics::kScaleSmall` | `0.76f` | Progress bar overlay text and compact captions |
| `Metrics::kWindowMarginX`, `kWindowMarginY` | `28`, `18` | Standard inner padding from a window's outer edge |
| `Metrics::kPaneGap` | `16` | Default horizontal/vertical gap between split subwindows |
| `Metrics::kRowHeight`, `kRowStride` | `22`, `25` | Standard interactive row height and vertical step |
| `Metrics::kIconSizeSmall`, `kIconSizeStandard` | `20`, `24` | Item icon sizes for dense lists (`20px`) or standard rows (`24px`) |
| `Metrics::kButtonWidth`, `kButtonHeight`, `kButtonGap` | `100`, `26`, `6` | Standard `BevelButton` dimensions and vertical spacing |

---

## 3. Window Surfaces & Presets (`UI::WindowPreset`)

`UI::TrackerWindow::Draw(rect, alpha, preset)` and `UI::Window::Draw(rect, alpha, preset)` render any of the
four native Dark Cloud surface types:

| Preset | Alias | Appearance & Backing |
|---|---|---|
| `WindowPreset::BlueStone` | `UI::BLUE_STONE_MENU` | Georama-style slate-blue mottled stone with 12px 3D pillow bevel, cached per `(w, h)` in VRAM. Optional override PNG: `textures/plates/ui_stone.png`. |
| `WindowPreset::Paper` | `UI::PAPER_MENU` | Weathered warm parchment (`R:160, G:128, B:90`) with fibrous relief and 12px bevel, cached per `(w, h)` in VRAM. Optional override PNG: `textures/plates/ui_paper.png`. |
| `WindowPreset::DarkTranslucent` | `UI::DARK_TRANSLUCENT_MENU` | Retail dark translucent help/dialog frame (`MenuHelpWinDraw` / `UI::HelpBox`). |
| `WindowPreset::Recessed` | — | Shade-masked sunken trough (`TrackerWindow::DrawIndentation`) that darkens the underlying panel and adds a bottom-right catch-light, inheriting the color of whatever stone or paper panel sits beneath it. |

### Scroll Banners & Recessed Dividers

- **Scroll Title Banner**:
  ```cpp
  UI::TrackerWindow::DrawStoneBanner(
      UI::Rect(150, 74, 340, 28),
      0x80,
      0x80,
      LocalizeText("tracker.title", "JOURNAL: CHEST TRACKER")
  );