#pragma once
#include <switchu/smi_protocol.hpp>
#include <switchu/smi_helpers.hpp>
#include <switchu/file_log.hpp>
#include "ecs.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>

namespace switchu::daemon::menu_la {

static constexpr AppletId kMenuAppletId = AppletId_LibraryAppletPhotoViewer;
static constexpr bool kEnableExternalContentLaunch = true;
static AppletHolder g_holder = {};
static bool g_active = false;
static bool g_holderCreated = false;
static bool g_externalRegistered = false;
static bool g_commandEventInitialized = false;
static bool g_commandEventAvailable = false;
static LibAppletExitReason g_lastExitReason = LibAppletExitReason_Normal;
static uint64_t g_startedAtTick = 0;
static uint64_t g_lastRuntimeNs = 0;

inline bool hasHolder() {
    return g_holderCreated;
}

inline bool isActive() {
    return g_active && appletHolderActive(&g_holder);
}

inline Event* stateChangedEvent() {
    return g_holderCreated ? &g_holder.StateChangedEvent : nullptr;
}

inline Event* commandEvent() {
    if (!g_holderCreated)
        return nullptr;

    // libnx initializes PopInteractiveOutDataEvent lazily. Returning the field
    // directly before this accessor has run adds handle 0 to waitObjects(),
    // which produces KERNELRESULT(InvalidHandle) (0xE401) every loop tick.
    if (!g_commandEventInitialized) {
        g_commandEventInitialized = true;
        Event* event = nullptr;
        const Result rc = appletHolderGetPopInteractiveOutDataEvent(&g_holder, &event);
        g_commandEventAvailable = R_SUCCEEDED(rc) && event != nullptr;
        if (!g_commandEventAvailable)
            switchu::FileLog::log("[menu_la] command event unavailable rc=0x%X", rc);
    }
    return g_commandEventAvailable ? &g_holder.PopInteractiveOutDataEvent : nullptr;
}

inline Result releaseExternalContent();

inline Result create() {
    const uint64_t startedAt = armGetSystemTick();
    switchu::FileLog::log("[menu_la] create begin active=%d holderActive=%d",
                          g_active ? 1 : 0,
                          (g_active && appletHolderActive(&g_holder)) ? 1 : 0);
    Result rc = appletCreateLibraryApplet(&g_holder,
        kMenuAppletId, LibAppletMode_AllForeground);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[menu_la] CreateLibApplet id=0x%X FAIL: 0x%X",
                              (u32)kMenuAppletId, rc);
        releaseExternalContent();
        return rc;
    }
    switchu::FileLog::log("[menu_la] create ok id=0x%X elapsed=%lums",
                          (u32)kMenuAppletId,
                          static_cast<unsigned long>(
                              armTicksToNs(armGetSystemTick() - startedAt) / 1'000'000ULL));
    g_holderCreated = true;
    return 0;
}

inline Result cleanupHolder() {
    if (g_holderCreated) {
        appletHolderClose(&g_holder);
        g_holder = {};
        g_holderCreated = false;
    }
    g_active = false;
    g_commandEventInitialized = false;
    g_commandEventAvailable = false;
    return 0;
}

// The external-content entry is independent of the holder: a normal reap keeps
// it registered so the next menu start skips the unregister/register pair.
// Only an explicit teardown or a failed start releases it.
inline Result releaseExternalContent() {
    if (!g_externalRegistered)
        return 0;
    const Result rc = switchu::daemon::unregisterExternalContent(
        switchu::smi::kMenuTakeoverProgramId);
    // ldr:ro reports nothing left to drop once it has consumed the entry. The
    // flag still has to clear, or prepare() would keep skipping the refresh and
    // the next launch would run the content installed for the Album title.
    g_externalRegistered = false;
    return rc;
}

inline Result terminate(uint64_t timeoutNs = 15'000'000'000ULL);

inline Result prepare() {
    const uint64_t startedAt = armGetSystemTick();
    if (g_holderCreated) {
        switchu::FileLog::log("[menu_la] prepare requested while holder exists; terminating first");
        const Result terminateRc = terminate();
        if (R_FAILED(terminateRc))
            return terminateRc;
    }

    if (kEnableExternalContentLaunch) {
        // The menu takes over the Album applet (AppletId_LibraryAppletPhotoViewer,
        // program 010000000000100D). ldr:ro hands the registered external code to
        // the next program start only, so a registration left over from the last
        // menu session is spent: the applet then runs whatever is installed for
        // that title - hbmenu on consoles with the usual Album takeover mod.
        // Refresh it before every launch instead of trusting the cached flag.
        if (g_externalRegistered) {
            const Result dropRc = releaseExternalContent();
            if (R_FAILED(dropRc))
                switchu::FileLog::log("[menu_la] stale external content release rc=0x%X", dropRc);
        }
        const uint64_t registerStartedAt = armGetSystemTick();
        Result ecsRc = switchu::daemon::registerExternalContent(
            switchu::smi::kMenuTakeoverProgramId, "/switch/SwitchU/bin/menu");
        if (R_FAILED(ecsRc)) {
            switchu::FileLog::log("[menu_la] external content registration failed rc=0x%X", ecsRc);
            return ecsRc;
        }
        g_externalRegistered = true;
        switchu::FileLog::log("[menu_la] external content ready elapsed=%lums",
                              static_cast<unsigned long>(
                                  armTicksToNs(armGetSystemTick() - registerStartedAt)
                                  / 1'000'000ULL));
    }
    const Result rc = create();
    switchu::FileLog::log("[menu_la] prepare done rc=0x%X elapsed=%lums",
                          rc,
                          static_cast<unsigned long>(
                              armTicksToNs(armGetSystemTick() - startedAt) / 1'000'000ULL));
    return rc;
}

inline Result startPrepared(smi::MenuStartMode mode, const smi::SystemStatus& status) {
    if (!g_holderCreated) {
        Result rc = prepare();
        if (R_FAILED(rc))
            return rc;
    }

    g_lastExitReason = LibAppletExitReason_Normal;
    switchu::FileLog::log("[menu_la] start begin mode=%u status.running=%d status.suspended=0x%016lX",
                          static_cast<u32>(mode),
                          status.app_running ? 1 : 0,
                          status.suspended_app_id);
    LibAppletArgs la_args;
    libappletArgsCreate(&la_args, static_cast<u32>(mode));
    libappletArgsSetPlayStartupSound(&la_args, false);
    Result rc = libappletArgsPush(&la_args, &g_holder);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[menu_la] ArgsPush FAIL: 0x%X", rc);
        cleanupHolder();
        releaseExternalContent();
        return rc;
    }

    rc = libappletPushInData(&g_holder, &status, sizeof(status));
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[menu_la] PushStatus FAIL: 0x%X", rc);
    } else {
        switchu::FileLog::log("[menu_la] PushStatus ok size=%zu", sizeof(status));
    }

    switchu::FileLog::log("[menu_la] holder start call");
    rc = appletHolderStart(&g_holder);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[menu_la] Start FAIL: 0x%X", rc);
        cleanupHolder();
        releaseExternalContent();
        return rc;
    }

    g_active = true;
    g_startedAtTick = armGetSystemTick();
    switchu::FileLog::log("[menu_la] started (mode=%u holderActive=%d)",
                          static_cast<u32>(mode),
                          appletHolderActive(&g_holder) ? 1 : 0);
    return 0;
}

inline Result launch(smi::MenuStartMode mode, const smi::SystemStatus& status) {
    Result rc = prepare();
    if (R_FAILED(rc)) return rc;
    return startPrepared(mode, status);
}

inline Result terminate(uint64_t timeoutNs) {
    if (!g_holderCreated)
        return cleanupHolder();
    switchu::FileLog::log("[menu_la] terminate begin holderActive=%d timeout_ms=%lu",
                          (g_active && appletHolderActive(&g_holder)) ? 1 : 0,
                          static_cast<unsigned long>(timeoutNs / 1'000'000ULL));
    if (g_active) {
        Result rc = appletHolderRequestExitOrTerminate(&g_holder, timeoutNs);
        switchu::FileLog::log("[menu_la] terminate request rc=0x%X", rc);
        g_lastExitReason = R_SUCCEEDED(rc) ? appletHolderGetExitReason(&g_holder)
                                           : LibAppletExitReason_Unexpected;
        if (g_startedAtTick != 0)
            g_lastRuntimeNs = armTicksToNs(armGetSystemTick() - g_startedAtTick);
    }
    const Result cleanupRc = cleanupHolder();
    const Result releaseRc = releaseExternalContent();
    switchu::FileLog::log("[menu_la] terminate done reason=%d", (int)g_lastExitReason);
    return R_FAILED(cleanupRc) ? cleanupRc : releaseRc;
}

inline bool checkFinished() {
    if (!g_active) return false;
    if (appletHolderCheckFinished(&g_holder)) {
        if (g_startedAtTick != 0)
            g_lastRuntimeNs = armTicksToNs(armGetSystemTick() - g_startedAtTick);
        g_lastExitReason = appletHolderGetExitReason(&g_holder);
        switchu::FileLog::log("[menu_la] holder finished reason=%d",
                              (int)g_lastExitReason);
        appletHolderJoin(&g_holder);
        const Result cleanupRc = cleanupHolder();
        if (R_FAILED(cleanupRc))
            switchu::FileLog::log("[menu_la] finished cleanup FAIL: 0x%X", cleanupRc);
        return true;
    }
    return false;
}

inline LibAppletExitReason exitReason() {
    if (g_active)
        return appletHolderGetExitReason(&g_holder);
    return g_lastExitReason;
}

inline uint64_t lastRuntimeNs() {
    return g_lastRuntimeNs;
}

inline Result pushStorage(AppletStorage* st) {
    Result rc = appletHolderPushInteractiveInData(&g_holder, st);
    if (R_FAILED(rc))
        switchu::FileLog::log("[menu_la] PushInteractiveInData FAIL: 0x%X", rc);
    appletStorageClose(st);
    return rc;
}

inline Result popStorage(AppletStorage* st) {
    return appletHolderPopInteractiveOutData(&g_holder, st);
}

}
