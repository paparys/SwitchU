#pragma once

#include "TabbedOverlayScreen.hpp"
#include <nxui/core/Texture.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GameOptionsScreen final : public TabbedOverlayScreen {
public:
    enum class ArtworkKind { Hero, Logo, Icon, Poster, PosterWide };
    struct GameInfo {
        std::uint64_t titleId = 0;
        std::string name;
        std::string version;
        std::string publisher;
        nxui::Texture* icon = nullptr;
        bool gameCard = false;
        bool suspended = false;
        bool canMove = true;
        bool canResize = true;
        // Fixed layouts keep every game at 1x1; larger layouts expose only the
        // sizes their rows/columns can actually pack.
        bool tileSizeFixed = false;
        std::vector<int> tileSizeIndices{0, 1, 2, 3, 4};
        int sizeIndex = 0;
        // 0 = 3:4, 1 = 16:9, 2 = 1:1, 3 = 4:3.
        int aspectIndex = 2;
        bool hasHeroArt = false;
        bool hasLogoArt = false;
        bool hasIconArt = false;
        bool hasPosterArt = false;
        bool hasPosterWideArt = false;
        int heroOffsetX = 0;
        int heroOffsetY = 0;
    };

    struct FolderEntry {
        std::uint32_t id = 0;
        std::string name;
        bool containsGame = false;
    };

    GameOptionsScreen();

    void setGame(const GameInfo& info);
    /// Folders shown in the per-game Folders section. Call before setGame()
    /// when opening the sheet so the rows are built with the current list.
    void setFolders(std::vector<FolderEntry> folders);
    void setGameIcon(nxui::Texture* icon) { m_game.icon = icon; }
    /// Slide the sheet to the side opposite the tile that opened it, so the
    /// tile stays visible beside the options.
    void setIconAnchor(const nxui::Rect& iconRect);
    /// Rebuild SteamGridDB rows so × only appears when that kind has art.
    void refreshArtworkPresence(bool hasHero, bool hasLogo, bool hasIcon,
                                bool hasPoster, bool hasPosterWide);
    void onMove(VoidCb cb) { m_moveCb = std::move(cb); }
    void onResize(IntCb cb) { m_resizeCb = std::move(cb); }
    void onAspectChange(IntCb cb) { m_aspectCb = std::move(cb); }
    void onCloseSoftware(VoidCb cb) { m_closeSoftwareCb = std::move(cb); }
    void onDeleteSoftware(VoidCb cb) { m_deleteSoftwareCb = std::move(cb); }
    void onToggleShortcut(VoidCb cb) { m_shortcutCb = std::move(cb); }
    // Adds (or moves) this title on the chosen end of the shortcut row.
    // 0 = left cluster, 1 = right cluster.
    void onAddShortcutSide(IntCb cb) { m_addShortcutSideCb = std::move(cb); }
    void onHideFromHome(VoidCb cb) { m_hideCb = std::move(cb); }
    void onRenameGame(VoidCb cb) { m_renameCb = std::move(cb); }
    // Add this title to the folder, or take it back out when it is already in.
    void onToggleFolder(std::function<void(std::uint32_t)> cb) {
        m_folderToggleCb = std::move(cb);
    }
    // Creates a folder, adds this title to it and places it on the carousel.
    void onCreateFolder(VoidCb cb) { m_createFolderCb = std::move(cb); }
    // Renames one specific folder from its row in the Folders section.
    void onRenameFolder(std::function<void(std::uint32_t)> cb) {
        m_folderRenameCb = std::move(cb);
    }
    void setShortcutPinned(bool pinned);
    bool shortcutPinned() const { return m_shortcutPinned; }
    void onSelectArtwork(std::function<void(ArtworkKind)> cb) {
        m_selectArtworkCb = std::move(cb);
    }
    void onHeroOffsetChange(std::function<void(int, int)> cb) {
        m_heroOffsetCb = std::move(cb);
    }
    void onClearArtwork(std::function<void(ArtworkKind)> cb) {
        m_clearArtworkCb = std::move(cb);
    }

protected:
    void buildTabs() override;
    // Match the settings shell: a persistent header description, flat rows and
    // controller hints. The game-options sheet stays a floating window instead
    // of taking over the whole screen.
    float overlayHeaderHeight() const override { return 132.f; }
    float overlayTabWidth() const override { return 260.f; }
    float overlayFooterHeight() const override { return 34.f; }
    bool showsRowDescriptions() const override { return false; }
    void drawOverlayHeader(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) override;
    void drawOverlayFooter(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) override;

private:
    void invalidateHeaderCache();

    GameInfo m_game;
    VoidCb m_moveCb;
    IntCb m_resizeCb;
    IntCb m_aspectCb;
    VoidCb m_closeSoftwareCb;
    VoidCb m_deleteSoftwareCb;
    VoidCb m_shortcutCb;
    IntCb m_addShortcutSideCb;
    VoidCb m_hideCb;
    VoidCb m_renameCb;
    std::function<void(std::uint32_t)> m_folderToggleCb;
    std::function<void(std::uint32_t)> m_folderRenameCb;
    VoidCb m_createFolderCb;
    std::vector<FolderEntry> m_folders;
    bool m_shortcutPinned = false;
    // Cached, already-ellipsized header strings. Ellipsizing all of them on
    // every frame made scrolling the sheet noticeably choppy.
    std::string m_headerEyebrow;
    std::string m_headerName;
    std::string m_headerVersion;
    std::string m_headerPublisher;
    std::string m_headerDescription;
    std::string m_headerDescriptionSource;
    // Inputs the identity lines were built from, so a description change (one
    // per focused row) does not re-ellipsize the name, version and publisher.
    std::string m_headerNameSource;
    std::string m_headerVersionSource;
    std::string m_headerPublisherSource;
    float m_headerTextWidth = -1.f;
    nxui::Font* m_headerFont = nullptr;
    nxui::Font* m_headerSmallFont = nullptr;
    bool m_headerCacheValid = false;
    // First description in the active tab, rebuilt only on tab/row-set changes.
    int m_headerFallbackTab = -2;
    int m_headerFallbackCount = -1;
    std::string m_headerFallbackDescription;
    std::string m_footerHintsText;
    int m_footerHintsKey = -1;
    std::function<void(ArtworkKind)> m_selectArtworkCb;
    std::function<void(ArtworkKind)> m_clearArtworkCb;
    std::function<void(int, int)> m_heroOffsetCb;
};
