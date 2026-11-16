# Frame Tap headroom handshake

SwitchU has no persistent low-detail HOME mode tied to Frame Tap. Connecting a
viewer leaves posters, SteamGridDB heroes, WaraWara shapes, folder covers and
normal icon caching active.

Frame Tap still uses the display-remount path and its explicit `NeedHeadroom`
handshake. Those are one-shot memory operations for donor remount, not a viewer
mode.

## Detection

Polls Frame Tap's `sftap` control service (`GetStatus`, cmd 0), matching FT's
`StreamStatus` (64 bytes):

- `reserved3 & 4` — network viewer present
- `state == Streaming (4)` — actively streaming (USB/net)

There is no forced-viewer debug file.

## NeedHeadroom

Frame Tap sets `StreamStatus.reserved3` bit **6** and writes
`sdmc:/config/switch-frame-tap/need_headroom`. SwitchU polls faster while the
bit is set, performs one hard reclaim of icon/poster/hero GPU memory, optionally
shrinks the Applet heap, then acknowledges via `sftap` cmd **15**
`AckHeadroom(u32 free_kb)` and `sdmc:/config/SwitchU/ft_headroom_ready`.

The requested MB comes from `GetDonorCap` (cmd 13), with the file fallback for
older Frame Tap builds or brief service gaps.

## Reclaim and refill

- Streamer GPU is released, including the focused tile for a donor request.
- Folder covers and leave-splash art are cleared.
- Font caches are trimmed, not wiped.
- The GPU refuse headroom is temporarily raised, clamped to leave a UI reserve.
- After the donor maps, browse refill returns to the normal streamer radius and
  folder covers are re-armed incrementally.
- The 30-second hold only suppresses another hard reclaim while Frame Tap
  finishes remount; it does not hide heroes or folder covers. Browse refills
  after the donor grace, and the GPU refuse floor returns to normal even while
  the viewer stays connected. With no viewer and no pending request, the heap
  also returns to normal.

## Capture remount

On cold boot, SwitchU may open its first window before Frame Tap's `vi` mitm is
registered. When a viewer later connects, one
`GpuDevice::remountDisplayForCaptureMitm()` reopens the window through the mitm.
On a normal HOME return, the restarted 100D already opens through the mitm, so
no remount is performed.
