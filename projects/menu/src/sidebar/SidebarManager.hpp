#pragma once
#include "SidebarAnimation.hpp"
#include "widgets/AdaptiveIconLoad.hpp"
#include "widgets/AppletButton.hpp"
#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/Theme.hpp>
#include <nxui/widgets/Widget.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SidebarManager {
public:
    struct Actions {
        std::function<void()> onAlbum;
        std::function<void()> onMiiEditor;
        std::function<void()> onControllers;
        std::function<void()> onSleep;
        std::function<void()> onSettings;
        std::function<void()> onMiiverse;
    };

    void build(nxui::GpuDevice& gpu, nxui::Renderer& ren,
               const std::string& assetsBase, const Actions& actions);

    void reloadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                      const std::string& assetsBase,
                      const std::string& customIconsBase = std::string(),
                      const std::string& iconPackId = std::string());
    void invalidateAssetsCache();

    void update(float dt, nxui::Widget* focusedWidget);

    // The dynamic-line HOME layout turns the two side columns into one
    // horizontal control strip between the profile bar and the applications.
    void setDynamicLineLayout(bool enabled);
    // Re-place the live arrangement (pinned sockets come and go).
    void refreshLayout() { setDynamicLineLayout(m_dynamicLine); }
    // Shortcut size and chrome come from the same settings as the tiles, so
    // the side columns and the top strip never read as a separate UI.
    // percent: 70..150 of the stock button.
    void setButtonScale(int percent);
    // Re-place the row by `pixels` and put it straight back, in one frame. Used
    // once at startup so the shortcuts settle on the configured size instead of
    // whatever the first placement pass produced.
    void nudgeButtonScale(int percent, float pixels);
    // Game pinning. Pinned titles own six sockets of their own, parked next to
    // the six stock applets: slot 0..5 never shares a button with Album / Mii /
    // Controllers / Sleep / Theme Shop / Settings, so adding a game cannot take
    // an applet shortcut away. An empty socket stays hidden.
    static constexpr int kPinnedSlotCount = 6;
    void setGameShortcut(int slot, const std::string& label,
                         const std::vector<std::uint8_t>& iconData,
                         nxui::GpuDevice& gpu, nxui::Renderer& ren,
                         std::function<void()> action);
    // Binds just the icon of an already-labelled socket. Returns false when
    // there was no data or the decode failed, so the caller can retry later.
    bool setGameShortcutIcon(int slot, const std::vector<std::uint8_t>& iconData,
                             nxui::GpuDevice& gpu, nxui::Renderer& ren);
    void clearGameShortcut(int slot);
    // cornerRadius 0 = square; borderWidth is clamped to a sane range.
    void setChromeStyle(float cornerRadius, float borderWidth);
    // 0 = strip at the top of the screen, 1 = at the bottom.
    void setStripEdge(int edge);
    // Which applet cluster the pinned sockets sit next to: 0 = left, 1 = right.
    void setPinSide(int side);
    int  pinSide() const { return m_pinSide; }
    // Horizontal travel in pixels, clamped so no group leaves the screen.
    // Left and right groups travel independently (pixels, clamped).
    void setStripNudge(float leftPx, float rightPx);
    // Vertical travel of the strip row in pixels (2px steps in the UI).
    void setStripNudgeY(float px);
    // Horizontal span of the game tiles. The two-column layout parks the
    // shortcut stacks outside it, so moving them never lands on the grid.
    void setGridClearance(float left, float right);
    // One continuous plate behind the whole strip instead of a pill per
    // shortcut (top / bottom strip layout only).
    void setStripPlate(bool enabled);
    // Hide it with the sidebar when another screen owns the frame.
    void setStripPlateSuppressed(bool suppressed);
    std::shared_ptr<nxui::GlassPanel> stripPlate() const { return m_stripPlateWidget; }
    // True while an overlay owns the frame and the plate is hidden with it.
    bool stripPlateSuppressed() const { return m_plateSuppressed; }
    // Target row of the shortcut strip (full width, height = one button). The
    // HUD uses it to park the profile/battery capsules on the same line.
    nxui::Rect stripBandTarget() const;
    // Space the live strip occupies, for the grid to keep clear.
    float reservedTop() const;
    float reservedBottom() const;
    void setDynamicLineDownAction(std::function<void()> action);
    void setDynamicLineUpTarget(nxui::Widget* target);
    void setDynamicLineProfileTarget(nxui::Widget* profile);
    // Battery is the strip's right-hand neighbour: RIGHT from the last shortcut
    // lands on it. Stored like the profile target so a re-place keeps the hop.
    void setDynamicLineBatteryTarget(nxui::Widget* battery);
    // Profile / battery capsules the plate runs into when the strip is on top.
    void setHudAnchors(const nxui::Rect& profile, const nxui::Rect& battery);

    void applyTheme(const nxui::Theme& theme);
    // Colour icon contrast adapt: follow HOME chrome while a single-row hero
    // is on screen (same light/dark flip as the rest of the UI).
    void setColourIconContrast(bool enabled);
    bool colourIconContrast() const { return m_colourContrastAdapt; }
    // Photoshop-style HSL adjust baked into colour glyphs on the next reload.
    void setIconHslAdjust(const switchu::icons::HslAdjust& adj);
    const switchu::icons::HslAdjust& iconHslAdjust() const { return m_hslAdjust; }

    std::vector<std::shared_ptr<AppletButton>>& leftButtons()  { return m_leftButtons; }
    std::vector<std::shared_ptr<AppletButton>>& rightButtons() { return m_rightButtons; }
    const std::vector<std::shared_ptr<AppletButton>>& leftButtons() const  { return m_leftButtons; }
    const std::vector<std::shared_ptr<AppletButton>>& rightButtons() const { return m_rightButtons; }
    // Pinned-game sockets, separate from the applet clusters.
    std::vector<std::shared_ptr<AppletButton>>& pinButtons() { return m_pinButtons; }
    const std::vector<std::shared_ptr<AppletButton>>& pinButtons() const { return m_pinButtons; }
    int visiblePinCount() const { return m_visiblePins; }

    AppletButton*  albumButton()    const { return m_albumButton; }
    nxui::Widget*  settingsButton() const { return m_settingsButton; }
    nxui::Widget*  themeShopButton() const { return m_themeShopButton; }

private:
    void loadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                    const std::string& assetsBase,
                    const std::string& customIconsBase,
                    const std::string& iconPackId);
    void tryLoadAnimation(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                          const std::string& webpPath, AppletButton* button,
                          nxui::Texture* staticIcon);

    // Chrome glide for a layout flip: each side button travels from its grid
    // column to the single-row strip (and back) on the same easing/duration as
    // the tile morph, so the buttons never snap ahead of the icons.
    struct ButtonGlide {
        AppletButton* button  = nullptr;
        nxui::Rect    from{};
        nxui::Rect    to{};
        float         elapsed = 0.f;
        float         delay   = 0.f;
        bool          active  = false;
    };
    void glideButtonTo(AppletButton* button, const nxui::Rect& target,
                       float delay);
    void updateButtonGlides(float dt);
    void applyChromeToButtons();
    void applyPaddingToButtons();
    AppletButton* gameSlotButton(int slot) const;
    float stripReserve() const;
    void syncStripPlateGeometry();

    std::vector<nxui::Texture> m_icons;
    // Pinned-game textures: one per pin socket, resized once at build.
    std::vector<nxui::Texture> m_gameIcons;
    // White×alpha twins for colour glyphs (empty/invalid when icon is mono).
    std::vector<nxui::Texture> m_iconContrastMasks;
    std::vector<float> m_iconMeanLumas;
    bool m_colourContrastAdapt = true;
    switchu::icons::HslAdjust m_hslAdjust;
    std::vector<std::shared_ptr<AppletButton>> m_leftButtons;
    std::vector<std::shared_ptr<AppletButton>> m_rightButtons;
    std::vector<std::shared_ptr<AppletButton>> m_pinButtons;
    int m_visiblePins = 0;
    int m_pinSide = 0;
    AppletButton*  m_albumButton    = nullptr;
    nxui::Widget*  m_settingsButton = nullptr;
    nxui::Widget*  m_themeShopButton = nullptr;

    struct AnimEntry {
        SidebarAnimation anim;
        AppletButton*    button    = nullptr;
        nxui::Texture*   staticTex = nullptr;
    };
    std::vector<AnimEntry> m_anims;
    std::vector<ButtonGlide> m_buttonGlides;
    // The first arrangement (boot / first build) snaps into place; only later
    // layout flips glide.
    bool m_layoutArranged = false;
    std::string m_loadedAssetsBase;
    std::string m_loadedCustomIconsBase;
    std::string m_loadedIconPackId;
    bool m_assetsLoaded = false;
    float m_buttonScale   = 1.f;
    float m_chromeRadius  = 16.f;
    float m_chromeBorder  = 2.2f;
    // Which arrangement is live, so a size change can re-run the right one.
    bool  m_dynamicLine   = false;
    int   m_stripEdge     = 0;
    float m_stripNudgeX   = 0.f;
    float m_stripNudgeRight = 0.f;
    float m_stripNudgeY     = 0.f;
    std::shared_ptr<nxui::GlassPanel> m_stripPlateWidget;
    bool m_stripPlate = true;
    bool m_plateSuppressed = false;
    float m_clearLeft  = 0.f;
    float m_clearRight = 1280.f;
    nxui::Widget*  m_hudLeftTarget  = nullptr;
    nxui::Widget*  m_hudRightTarget = nullptr;
    nxui::Rect     m_anchorProfile{};
    nxui::Rect     m_anchorBattery{};
    bool           m_anchorValid = false;
    // Theme chrome, cached so the plate can be styled without a Theme handle.
    nxui::Color m_btnBase      {0.10f, 0.11f, 0.16f, 0.90f};
    nxui::Color m_btnBorder    {0.50f, 0.50f, 0.65f, 0.34f};
    nxui::Color m_btnHighlight {1.f, 1.f, 1.f, 0.08f};
};
