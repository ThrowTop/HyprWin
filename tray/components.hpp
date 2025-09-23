// Components.hpp

#pragma once
#include <string>
#include <functional>
#include <windows.h>
#include <memory>
#include <type_traits>
#include <vector>
#include <utility>

#include "assets.hpp"
namespace Tray {
class BaseTray; // forward decl

// -------------------- TrayEntry (base) --------------------
class TrayEntry {
  protected:
    std::wstring text;
    bool disabled = false;
    BaseTray* parent = nullptr;

    // glyph plumbing (modular: either a bitmap or an icon)
    enum class GlyphKind { None, Bitmap, Icon };
    GlyphKind glyphKind = GlyphKind::None;

    Image glyphBitmapOwned;   // if user set a bitmap directly
    Icon glyphIcon;           // if user set an icon…
    mutable Image glyphCache; // ...we lazily convert to a bitmap for menus
    mutable int cacheCx = 0;
    mutable int cacheCy = 0;

    bool defaultItem = false;

  public:
    explicit TrayEntry(std::wstring text) : text(std::move(text)) {}
    virtual ~TrayEntry() = default;

    BaseTray* getParent() {
        return parent;
    }
    void setParent(BaseTray* newParent);
    const std::wstring& getText() const {
        return text;
    }
    void setText(std::wstring newText);
    void setDisabled(bool state);
    bool isDisabled() const {
        return disabled;
    }

    // DECLARE ONLY here:
    void setGlyphBitmap(Image hbmp);
    void setGlyphIcon(Icon ico);
    void setDefault(bool v);

    // used by menu builder:
    HBITMAP getOrBuildGlyphBitmap(int cx, int cy) const;
    bool isDefault() const {
        return defaultItem;
    }
};

template <typename T>
concept tray_entry = std::is_base_of_v<TrayEntry, T>; // or std::derived_from in C++20

// -------------------- BaseTray (container) --------------------
class BaseTray {
  protected:
    Icon icon;
    std::wstring identifier;
    std::vector<std::shared_ptr<TrayEntry>> entries;

  public:
    BaseTray(std::wstring identifierIn, Icon iconIn) : icon(std::move(iconIn)), identifier(std::move(identifierIn)) {}
    virtual ~BaseTray() = default;

    // Add a pack of existing entries (const lvalues)
    template <tray_entry... Ts>
    void addEntries(const Ts&... es) {
        (addEntry(es), ...);
    }

    // Add a single entry by const reference (one copy of T)
    template <tray_entry T>
    std::shared_ptr<T> addEntry(const T& entry) {
        auto sp = std::make_shared<T>(entry); // one copy of T
        sp->setParent(this);
        entries.emplace_back(sp); // upcast to shared_ptr<TrayEntry>
        update();
        return sp; // typed shared_ptr<T>
    }

    // Add a single entry by rvalue (move-construct T, avoid copy)
    template <tray_entry T>
    std::shared_ptr<T> addEntry(T&& entry) {
        auto sp = std::make_shared<T>(std::move(entry));
        sp->setParent(this);
        entries.emplace_back(sp);
        update();
        return sp;
    }

    // Construct T in-place (perfect forwarding)
    template <tray_entry T, typename... Args>
    std::shared_ptr<T> emplaceEntry(Args&&... args) {
        auto sp = std::make_shared<T>(std::forward<Args>(args)...);
        sp->setParent(this);
        entries.emplace_back(sp);
        update();
        return sp;
    }

    virtual void run() = 0;
    virtual void exit() = 0;
    virtual void update() = 0;

    const std::vector<std::shared_ptr<TrayEntry>>& getEntries() const {
        return entries; // no copy
    }
};

// ---- TrayEntry inline impls (now that BaseTray exists) ----
inline void TrayEntry::setParent(BaseTray* newParent) {
    parent = newParent;
}
inline void TrayEntry::setText(std::wstring newText) {
    text = std::move(newText);
    if (parent)
        parent->update();
}
inline void TrayEntry::setDisabled(bool state) {
    disabled = state;
    if (parent)
        parent->update();
}
// After BaseTray is fully defined:

inline void TrayEntry::setGlyphBitmap(Image hbmp) {
    glyphKind = GlyphKind::Bitmap;
    glyphBitmapOwned = std::move(hbmp);
    glyphCache = Image(); // drop any icon cache
    if (parent)
        parent->update();
}

inline void TrayEntry::setGlyphIcon(Icon ico) {
    glyphKind = GlyphKind::Icon;
    glyphIcon = std::move(ico);
    glyphCache = Image(); // will rebuild on demand
    if (parent)
        parent->update();
}

inline void TrayEntry::setDefault(bool v) {
    defaultItem = v;
    if (parent)
        parent->update();
}

inline HBITMAP TrayEntry::getOrBuildGlyphBitmap(int cx, int cy) const {
    if (glyphKind == GlyphKind::Bitmap)
        return glyphBitmapOwned.get();
    if (glyphKind == GlyphKind::Icon) {
        if (!glyphCache.get() || cacheCx != cx || cacheCy != cy) {
            glyphCache = Image(glyphIcon.toBitmap(cx, cy), OwnershipPolicy::Adopt);
            cacheCx = cx;
            cacheCy = cy;
        }
        return glyphCache.get();
    }
    return nullptr;
}

// -------------------- Concrete entries --------------------
class Button : public TrayEntry {
    std::function<void()> callback;

  public:
    ~Button() override = default;
    Button(std::wstring text, std::function<void()> cb) : TrayEntry(std::move(text)), callback(std::move(cb)) {}
    void clicked() {
        if (callback)
            callback();
    }
    void setCallback(std::function<void()> cb) {
        callback = std::move(cb);
    }
};

class ImageButton : public Button {
    Image image;

  public:
    ~ImageButton() override = default;
    ImageButton(std::wstring text, Image img, std::function<void()> cb) : Button(std::move(text), std::move(cb)), image(std::move(img)) {
        // also expose as glyph so menu builder shows it
        this->setGlyphBitmap(image);
    }
    Image getImage() {
        return image;
    }
    void setImage(Image newImage) {
        image = std::move(newImage);
        this->setGlyphBitmap(image);
    }
};

class Label : public TrayEntry {
  public:
    explicit Label(std::wstring text) : TrayEntry(std::move(text)) {}
    ~Label() override = default;
};

class Separator : public TrayEntry {
  public:
    Separator() : TrayEntry(L"") {}
    ~Separator() override = default;
};
class Toggle : public TrayEntry {
    bool* toggled = nullptr; // synced, non-owning
    // Forced return: always return a string; empty string => don't change label
    std::function<std::wstring(bool&)> onToggle;

    // Optional custom checkmark bitmaps
    HBITMAP hbmpChecked = nullptr;
    HBITMAP hbmpUnchecked = nullptr;

  public:
    ~Toggle() override = default;

    Toggle(std::wstring text, bool* state, std::function<std::wstring(bool&)> cb = {}) : TrayEntry(std::move(text)), toggled(state), onToggle(std::move(cb)) {}

    void onToggled() {
        if (!toggled)
            return;
        *toggled = !*toggled;

        if (onToggle) {
            std::wstring newText = onToggle(*toggled);
            if (!newText.empty()) {
                setText(std::move(newText)); // triggers parent->update()
            } else if (parent) {
                parent->update(); // still refresh check state
            }
        } else if (parent) {
            parent->update();
        }
    }

    bool isToggled() const {
        return toggled ? *toggled : false;
    }

    // Optional custom checkmark bitmaps
    void setCheckBitmaps(HBITMAP checked, HBITMAP unchecked) {
        hbmpChecked = checked;
        hbmpUnchecked = unchecked;
        if (parent)
            parent->update();
    }
    HBITMAP getCheckedBitmap() const {
        return hbmpChecked;
    }
    HBITMAP getUncheckedBitmap() const {
        return hbmpUnchecked;
    }

    void setCallback(std::function<std::wstring(bool&)> cb) {
        onToggle = std::move(cb);
    }
};
// -------------------- Submenu --------------------
class Submenu : public TrayEntry {
    std::vector<std::shared_ptr<TrayEntry>> children;

  public:
    explicit Submenu(std::wstring textIn) : TrayEntry(std::move(textIn)) {}
    ~Submenu() override = default;

    // Add a pack of existing entries (const lvalues)
    template <tray_entry... Ts>
    void addEntries(const Ts&... es) {
        (addEntry(es), ...);
    }

    // Add a single entry by const reference (one copy of T)
    template <tray_entry T>
    std::shared_ptr<T> addEntry(const T& entry) {
        auto sp = std::make_shared<T>(entry);
        sp->setParent(parent); // owning tray is the parent
        children.emplace_back(sp);
        if (parent)
            parent->update();
        return sp;
    }

    // Add a single entry by rvalue (move-construct T)
    template <tray_entry T>
    std::shared_ptr<T> addEntry(T&& entry) {
        auto sp = std::make_shared<T>(std::move(entry));
        sp->setParent(parent);
        children.emplace_back(sp);
        if (parent)
            parent->update();
        return sp;
    }

    // Construct T in-place (perfect forwarding)
    template <tray_entry T, typename... Args>
    std::shared_ptr<T> emplaceEntry(Args&&... args) {
        auto sp = std::make_shared<T>(std::forward<Args>(args)...);
        sp->setParent(parent);
        children.emplace_back(sp);
        if (parent)
            parent->update();
        return sp;
    }

    void update() {
        if (parent)
            parent->update();
    }

    const std::vector<std::shared_ptr<TrayEntry>>& getEntries() const {
        return children; // no copy
    }
};
} // namespace Tray
