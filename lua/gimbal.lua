-- gimbal -- tablet mode and auto-rotation for the Framework Laptop 12
-- on Omarchy 4 / Hyprland.
--
-- Install:
--   cp gimbal.lua ~/.config/hypr/
--   then add to ~/.config/hypr/hyprland.lua:
--       require("hypr.gimbal")
--
-- There is deliberately no daemon. Everything here is measured rather than
-- assumed; see FINDINGS.md for the numbers behind each choice.
--
--   * Tablet mode comes from the kernel SW_TABLET_MODE switch (INT33D3 /
--     soc_button_array), via Hyprland's switch binds. The firmware owns the
--     hysteresis: it enters around 220-257 deg and leaves around 106-170 deg.
--   * Rotation reads the *display* accelerometer directly from sysfs. A
--     3-axis read costs 82 us on average and 291 us at worst, so at 4 Hz this
--     is ~0.03% of compositor time.
--   * The hinge angle is used only to seed the initial state on load, since
--     switch binds are edge-triggered and cannot report the current position.

local M = {}

-- ---------------------------------------------------------------------------
-- State
--
-- `hyprctl reload` destroys and rebuilds the whole Lua state -- verified by
-- setting a marker global, reloading three times, and finding it gone. So
-- there is deliberately no teardown code here: nothing survives to duplicate.
-- Measured after three consecutive reloads: exactly one of each switch bind,
-- one SUPER+R, and Hyprland at 0.2% CPU.
--
-- Exposed on _G purely so it can be inspected live:
--   hyprctl eval 'local S=_G.__fw12_tablet ...'
-- ---------------------------------------------------------------------------
local S = {}
_G.__fw12_tablet = S

-- ---------------------------------------------------------------------------
-- Tunables
-- ---------------------------------------------------------------------------
local POLL_MS = 250 -- accelerometer sample interval while in tablet mode
local SETTLE_TICKS = 2 -- consecutive agreeing samples before rotating (~500 ms)
local ONE_G = 16384 -- raw counts per g (scale = 0.000598550 m/s^2/count)
local DEAD_ZONE = math.floor(ONE_G * 2 / 5) -- 40% of 1 g to call an axis dominant
local TABLET_ANGLE = 200 -- hinge angle treated as "already folded" at load
local SWITCH_DEV = "gpio-keys" -- as Hyprland names the SW_TABLET_MODE device

-- Focus handling while folded.
--
-- With follow_mouse = 1 the keyboard focus detaches from the window you are
-- typing into for as long as a finger rests on the on-screen keyboard, because
-- what is under the finger is a layer surface and not a window. A tap is too
-- brief to notice; a swipe or a resting hand holds it, and every key pressed in
-- that time goes nowhere. Measured on the Hyprland event socket: `activewindow`
-- goes empty on touch-down and comes back on release.
--
-- 2 means keyboard focus follows clicks into windows rather than the pointer,
-- which is the behaviour a tablet wants anyway. Laptop mode is left alone.
local TABLET_FOLLOW_MOUSE = 2
local LAPTOP_FOLLOW_MOUSE = 1 -- Omarchy's default; change here if yours differs

-- Where the folded state is published for anything outside Hyprland to read.
-- The shell plugin watches this to decide whether to show its keyboard button.
-- A file rather than IPC because the reader is a Quickshell FileView, which
-- already does inotify, and because a plain word on disk is trivial to check
-- by hand when something looks wrong.
local MODE_PATH = (os.getenv("XDG_RUNTIME_DIR") or "/tmp") .. "/gimbal-mode"

-- ---------------------------------------------------------------------------
-- sysfs helpers
--
-- IIO device numbering is NOT stable across boots: cros-ec-accel.11.auto was
-- accel-base on one boot and accel-display on the next. Only `label` and
-- `name` are safe, so every lookup goes through them. Lua has no directory
-- listing, but probing a small fixed range is enough and avoids the dependency.
-- ---------------------------------------------------------------------------
local IIO = "/sys/bus/iio/devices/iio:device"

local function read_line(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local v = f:read("*l")
    f:close()
    return v
end

local function read_number(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local v = f:read("*n")
    f:close()
    return v
end

local function write_mode(mode)
    local f = io.open(MODE_PATH, "w")
    if not f then return end
    f:write(mode)
    f:close()
end

local function find_iio(attr, want)
    for i = 0, 15 do
        local dir = IIO .. i
        if read_line(dir .. "/" .. attr) == want then return dir end
    end
    return nil
end

local function accel_dir()
    if S.accel and read_line(S.accel .. "/label") == "accel-display" then
        return S.accel
    end
    S.accel = find_iio("label", "accel-display")
    if not S.accel then
        hl.notification.create({
            text = "gimbal: no accel-display sensor; rotation disabled",
            timeout = 5000,
        })
    end
    return S.accel
end

-- ---------------------------------------------------------------------------
-- Orientation
--
-- Which way up the panel is, per dominant gravity axis. Measured on this
-- machine against iio-sensor-proxy and replayed over 90 captured samples.
--
-- This is the one table in the file that is NOT guessable, and the one most
-- likely to be wrong on a machine that is not mine: it depends on how the
-- panel and its sensor are mounted, which differs between units and even
-- between panel suppliers for the same model. Every other number here is
-- either read from the hardware or a threshold with slack in it.
--
-- So it is a table rather than four literals buried in a branch. If rotation
-- is wrong on your machine, it is almost certainly only this:
--
--   sideways      -- landscape and portrait swapped -- swap the y_* and x_*
--                    pairs with each other
--   mirrored      -- rotates the wrong way round     -- swap x_pos and x_neg
--   upside down   -- 180 degrees out                 -- swap y_pos and y_neg
--
-- Transform numbers are Wayland's: 0 normal, 1 left-up, 2 bottom-up,
-- 3 right-up. |z| dominant means lying flat, which carries no orientation
-- information at all, so the previous one stands.
local ORIENT = {
    y_pos = 0, -- normal
    y_neg = 2, -- bottom-up
    x_pos = 3, -- right-up
    x_neg = 1, -- left-up
}
-- ---------------------------------------------------------------------------
local function classify(x, y, z)
    local ax, ay, az = math.abs(x), math.abs(y), math.abs(z)
    if az > ax and az > ay then return nil end -- lying flat: no information
    if ay > ax then
        if ay < DEAD_ZONE then return nil end
        return y > 0 and ORIENT.y_pos or ORIENT.y_neg
    end
    if ax < DEAD_ZONE then return nil end
    return x > 0 and ORIENT.x_pos or ORIENT.x_neg
end

-- ---------------------------------------------------------------------------
-- Apply
--
-- Monitor, touch and stylus must move together or the pen and finger stop
-- landing where the user is pointing. Note `hyprctl keyword` does NOT work on
-- a Lua config -- Hyprland refuses it -- which is why this is a direct call
-- rather than an IPC command.
-- ---------------------------------------------------------------------------
local function apply(transform)
    local monitors = hl.get_monitors()
    local target = nil
    for _, m in ipairs(monitors) do
        if m.name:sub(1, 3) == "eDP" then
            target = m
            break
        end
    end
    target = target or monitors[1]
    if not target then return end

    hl.monitor({
        output = target.name,
        mode = "preferred",
        position = "auto",
        scale = target.scale,
        transform = transform,
    })
    hl.config({
        input = {
            touchdevice = { transform = transform },
            tablet = { transform = transform },
        },
    })
    S.applied = transform
end

-- ---------------------------------------------------------------------------
-- Poll
-- ---------------------------------------------------------------------------
local function tick()
    if not S.tablet or S.locked then return end

    local dir = accel_dir()
    if not dir then return end

    local x = read_number(dir .. "/in_accel_x_raw")
    local y = read_number(dir .. "/in_accel_y_raw")
    local z = read_number(dir .. "/in_accel_z_raw")
    if not (x and y and z) then
        S.accel = nil -- renumbered or unbound; re-resolve next tick
        return
    end

    local want = classify(x, y, z)
    if want == nil or want == S.applied then
        S.pending, S.pending_n = nil, 0
        return
    end

    if want == S.pending then
        S.pending_n = S.pending_n + 1
        if S.pending_n >= SETTLE_TICKS then
            apply(want)
            S.pending, S.pending_n = nil, 0
        end
    else
        S.pending, S.pending_n = want, 1
    end
end

-- ---------------------------------------------------------------------------
-- Mode transitions
-- ---------------------------------------------------------------------------

-- Rotation lock. SUPER+R is free in Omarchy (the existing R binds are all
-- SUPER+CTRL variants).
--
-- Bound only while folded. It has no meaning in laptop mode, where nothing
-- rotates anyway, so leaving it live there just means an accidental press can
-- arm a setting that does nothing now and breaks rotation later.
local function toggle_lock()
    S.locked = not S.locked
    hl.notification.create({
        text = S.locked and "Rotation locked" or "Rotation unlocked",
        timeout = 1500,
    })
    if not S.locked then tick() end
end

local function enter_tablet()
    S.tablet = true
    S.pending, S.pending_n = nil, 0
    write_mode("tablet")
    hl.config({ input = { follow_mouse = TABLET_FOLLOW_MOUSE } })
    if not S.lock_bound then
        hl.bind("SUPER + R", toggle_lock, { description = "Toggle auto-rotation lock" })
        S.lock_bound = true
    end
    tick() -- catch up to however the device is being held right now
end

local function leave_tablet()
    S.tablet = false
    S.pending, S.pending_n = nil, 0
    write_mode("laptop")
    hl.config({ input = { follow_mouse = LAPTOP_FOLLOW_MOUSE } })
    -- Unfolding clears the rotation lock.
    --
    -- The lock is for holding the device at an angle you do not want followed
    -- -- reading in bed, mostly -- which is a thing that ends when you fold it
    -- back into a laptop. Carrying it forward meant it could sit on silently
    -- for days: nothing shows it is set, and the symptom is auto-rotation
    -- simply not working, which looks exactly like a bug in the sensor path.
    -- That happened, and it cost an evening looking in the wrong place.
    S.locked = false
    if S.lock_bound then
        hl.unbind("SUPER + R")
        S.lock_bound = false
    end
    if S.applied ~= 0 then apply(0) end
end

-- Switch binds are edge-triggered, so on load we cannot ask the switch where
-- it currently is. The hinge angle can answer that. Values above 360 are the
-- EC's "indeterminate" sentinel and must never be read as "past 360 therefore
-- folded" -- it appears reliably during the fold itself.
local function seed_initial_state()
    local dir = find_iio("name", "cros-ec-lid-angle")
    if not dir then return false end
    local angle = read_number(dir .. "/in_angl_raw")
    if not angle or angle > 360 then return false end
    return angle >= TABLET_ANGLE
end

-- ---------------------------------------------------------------------------
-- Wire up
-- ---------------------------------------------------------------------------
S.locked = false
S.lock_bound = false
S.applied = 0
S.pending, S.pending_n = nil, 0

-- Toggle the on-screen keyboard.
--
-- Bound always, not only while folded, because its most useful job is the
-- reverse of what it sounds like: dismissing the on-screen keyboard *from* the
-- on-screen keyboard, whose Framework key is a real Super. Aiming for a 32 px
-- strip is not always what you want, and there is nothing else to press.
--
-- SUPER + B for "board". SUPER + K is Omarchy's own keybindings menu
-- (bindings/utilities.lua), so it is not available; the free SUPER letters on
-- a stock install are A B D E H I M N Q U Y Z.
hl.bind("SUPER + B", function()
    hl.dispatch(hl.dsp.exec_cmd(
        "omarchy-shell shell call io.github.mechanicsunlocked.gimbal toggle ''"))
end, { description = "Toggle the on-screen keyboard" })

hl.bind("switch:on:" .. SWITCH_DEV, enter_tablet, { locked = true })
hl.bind("switch:off:" .. SWITCH_DEV, leave_tablet, { locked = true })

S.timer = hl.timer(tick, { timeout = POLL_MS, type = "repeat" })

-- Publish a state before deciding, so a reader that starts between here and
-- the seed below never sees a stale word from the previous Hyprland session.
write_mode("laptop")
if seed_initial_state() then enter_tablet() end

function M.status()
    return {
        tablet = S.tablet,
        locked = S.locked,
        applied = S.applied,
        accel = S.accel,
    }
end

-- Clear the lock without a config reload.
--
-- Added because diagnosing a stuck lock ended with `hyprctl reload` as the
-- only way out, which throws away the whole Lua state to change one boolean.
--
--   hyprctl eval 'require("hypr.gimbal").set_locked(false)'
function M.set_locked(v)
    S.locked = v and true or false
    if not S.locked then tick() end
end

return M
