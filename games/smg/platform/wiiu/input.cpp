#include "input.h"

#include "runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#if defined(PORT_GAME_SMG)
#include <sysapp/launch.h>
#endif

#if defined(PORT_GAME_SMG)
extern "C" volatile int sms_wiiu_menu_exit_pending;
extern "C" int64_t sms_wiiu_OSGetTime(void);
#endif
extern "C" volatile int sms_wiiu_shot_request;

#if defined(TARGET_WIIU)
namespace MR {
/* The game's Star Pointer controller owns this state.  Keep the declaration
 * weak so the host input test can exercise the provider without the game. */
bool isStarPointer2PTransparencyMode() __attribute__((weak));
bool isStarPointerModeStarPieceTarget() __attribute__((weak));
}
#endif

namespace {

enum ScriptKind {
    SCRIPT_BUTTON,
    SCRIPT_STICK,
    SCRIPT_POINTER,
    SCRIPT_SHOT,
    SCRIPT_SPIN,
};

struct ScriptEvent {
    ScriptKind kind;
    u32 button;
    f32 stick_x;
    f32 stick_y;
    f32 pointer_x;
    f32 pointer_y;
    u32 at;
    u32 until;
    char name[16];
    bool logged;
};

static const u32 kMaxScriptEvents = 512;
ScriptEvent s_script[kMaxScriptEvents];
u32 s_script_count;
bool s_script_full_reported;
bool s_initialized;
bool s_connected;
bool s_marker;
bool s_script_loaded;
bool s_script_present;
#if defined(PORT_GAME_SMG)
int64_t s_autopress_start;
bool s_autopress_exit_requested;
#endif
u32 s_previous_hold;
bool s_spin_hold;
bool s_vpad_spin_hold;
bool s_vpad_active;
bool s_vpad_read_ok;
bool s_gamepad_motor;
u32 s_gamepad_motor_age;
u32 s_shake_log_count;
u32 s_spin_last_frame = ~0u;
u32 s_spin_frames_since_press = ~0u;
u32 s_vpad_hold;
f32 s_vpad_stick_x;
f32 s_vpad_stick_y;
f32 s_gyro_pointer_x;
f32 s_gyro_pointer_y;
f32 s_gyro_center_x;
f32 s_gyro_center_y;
f32 s_stick_pointer_x;
f32 s_stick_pointer_y;
bool s_stick_pointer_active;
bool s_gyro_pointer_enabled;
enum PointerSource {
    POINTER_SOURCE_NONE,
    POINTER_SOURCE_TOUCH,
    POINTER_SOURCE_STICK,
    POINTER_SOURCE_GYRO,
};
PointerSource s_pointer_source;
bool s_hardware_pointer_valid;
bool s_touch_pointer_valid;
f32 s_hardware_pointer_x;
f32 s_hardware_pointer_y;
bool s_touch_active;
bool s_touch_release_pending;
u16 s_touch_start_x;
u16 s_touch_start_y;
u16 s_touch_last_x;
u16 s_touch_last_y;
f32 s_touch_max_moved;
int64_t s_touch_start_ticks;
u32 s_touch_tap_button;
u32 s_touch_log_count;
bool s_touch_tap_shoot;
/* Feeding a hungry Luma (TicoFat) gives one Star Bit per interval step while
 * Remote B is HELD and the pointer is on it; a tap's one-frame B never lands
 * on a step (tester 2026-11-20: 0/400 while Star Bits drained).  In the
 * StarPieceTarget mode a finger on the screen therefore holds B. */
bool s_touch_feed;
bool s_touch_fed_this_touch;
/* A tap is only ~4-9 frames and TicoFat takes one bit every 5 frames, so B
 * stays held (pointer pinned at the tap) for kTouchFeedTailFrames after the
 * finger lifts: every tap feeds at least one Star Bit. */
constexpr u32 kTouchFeedTailFrames = 6;
u32 s_touch_feed_until_frame;
f32 s_touch_feed_x;
f32 s_touch_feed_y;
/* Tap settle (2026-11-17): menu buttons select only once they are in their Pointing state, which the game
 * enters a few frames after the pointer reaches them. A quick tap used to send A in the same frames the
 * pointer appeared, so in most menus the first tap only moved the cursor. Hold the pointer at the tap
 * point and send the button a few frames later. */
u32 s_tap_pending_button;
u32 s_tap_fire_frame;
constexpr u32 kTapSettleFramesA = 6;
constexpr u32 kTapSettleFramesB = 2;
int64_t s_pointer_last_activity_ticks;
bool s_pointer_hidden;
bool s_have_gyro_sample;
f32 s_previous_gyro_x;
f32 s_previous_gyro_y;
/* Revolution's Nunchuk+ACC+DPD format is 5; wut names this family
 * WPAD_FMT_NUNCHUK_ACC_DPD only in some SDK revisions. */
u32 s_data_format = 5;
WPADExtensionCallback s_extension_callbacks[WPAD_MAX_CONTROLLERS];
WPADConnectCallback s_connect_callbacks[WPAD_MAX_CONTROLLERS];
WPADSamplingCallback s_sampling_callbacks[WPAD_MAX_CONTROLLERS];
bool s_extension_reported;
bool s_connect_reported;

#if defined(TARGET_WIIU)
/* Older Revolution headers used by the game do not carry wut's extension
 * constants. Their numeric values are stable across the two APIs. */
#ifndef WPAD_EXT_NUNCHUK
#define WPAD_EXT_NUNCHUK 0x01
#define WPAD_EXT_CLASSIC 0x02
#define WPAD_EXT_MPLUS_NUNCHUK 0x06
#define WPAD_EXT_MPLUS_CLASSIC 0x07
#define WPAD_EXT_PRO_CONTROLLER 0x1f
#endif
#ifndef WPAD_DEV_CLASSIC
#define WPAD_DEV_CLASSIC 2
#endif
const u32 kWiiRemoteButtonMask =
    WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT | WPAD_BUTTON_DOWN | WPAD_BUTTON_UP |
    WPAD_BUTTON_PLUS | WPAD_BUTTON_2 | WPAD_BUTTON_1 | WPAD_BUTTON_B |
    WPAD_BUTTON_A | WPAD_BUTTON_MINUS | WPAD_BUTTON_Z | WPAD_BUTTON_C |
    WPAD_BUTTON_HOME;
/* wut's KPADStatus has the same public data as Revolution KPADStatus but a
 * different layout. Keep the two ABIs separate: the real call writes this
 * structure, then poll_real_inputs() translates it for Galaxy. */
struct WutKPADVec2 { f32 x, y; };
struct WutKPADVec3 { f32 x, y, z; };
struct WutKPADNunchuk {
    WutKPADVec2 stick;
    WutKPADVec3 acc;
    f32 acc_magnitude;
    f32 acc_variation;
    u32 hold;
    u32 trig;
    u32 release;
};
struct WutKPADClassic {
    u32 hold;
    u32 trig;
    u32 release;
    WutKPADVec2 left_stick;
    WutKPADVec2 right_stick;
    f32 left_trigger;
    f32 right_trigger;
};
struct WutKPADPro {
    u32 hold;
    u32 trig;
    u32 release;
    WutKPADVec2 left_stick;
    WutKPADVec2 right_stick;
    s32 charging;
    s32 wired;
};
struct WutKPADStatus {
    u32 hold, trig, release;
    WutKPADVec3 acc;
    f32 acc_magnitude, acc_variation;
    WutKPADVec2 pos, pos_diff;
    f32 pos_diff_magnitude;
    WutKPADVec2 angle, angle_diff;
    f32 angle_diff_magnitude;
    f32 dist, dist_diff, dist_diff_magnitude;
    WutKPADVec2 down;
    u8 extension_type;
    s8 error;
    s8 pos_valid;
    u8 format;
    union {
        WutKPADNunchuk nunchuk;
        WutKPADClassic classic;
        WutKPADPro pro;
        u8 bytes[0x50];
    } ext;
    u8 mplus[0x40];
};
static_assert(sizeof(WutKPADStatus) == 0xf0, "wut KPADStatus ABI");

extern "C" void __real_KPADInit(void);
extern "C" void __real_WPADInit(void);
extern "C" void __real_KPADReset(void);
extern "C" uint32_t __real_KPADReadEx(s32, WutKPADStatus*, uint32_t, s32*);
extern "C" void __real_KPADSetAccParam(s32, f32, f32);
extern "C" void __real_KPADSetBtnRepeat(s32, f32, f32);
extern "C" void __real_KPADSetDistParam(s32, f32, f32);
extern "C" void __real_KPADSetHoriParam(s32, f32, f32);
extern "C" void __real_KPADSetPosParam(s32, f32, f32);
extern "C" void __real_KPADSetSensorHeight(s32, f32);

struct RealController {
    WutKPADStatus raw;
    KPADStatus status;
    bool connected;
    bool sampled;
    bool connect_pending;
    bool disconnect_pending;
    bool extension_pending;
    bool shake;
    bool have_previous_acc;
    u8 extension_type;
    u8 previous_extension_type;
    f32 pointer_x;
    f32 pointer_y;
    WutKPADVec3 previous_acc;
    WutKPADVec3 previous_nunchuk_acc;
    u32 shake_debounce_until;
    f32 sample_shake_g;   // largest change between this frame's KPAD samples
    bool motor_on;        // Pro Controller rumble stretching
    bool motor_stop_pending;
    u32 motor_hold_until;
};
RealController s_real[WPAD_MAX_CONTROLLERS];
// Shake to spin: acceleration change (g) that counts as a shake. 1.5 needed a hard flick (tester, 2026-11-17).
constexpr f32 kShakeThresholdG = 1.0f;
constexpr u32 kShakeDebounceFields = 6;
// The Pro Controller motor spins up slowly, so Galaxy's short on/off rumble pulses felt weak. Keep it on
// for at least this many fields after each rumble request.
constexpr u32 kProRumbleMinFields = 6;
u32 s_real_polled_frame = ~0u;
s32 s_player1_real_channel = -1;
#endif

#if defined(TARGET_WIIU)
extern "C" void __real_WPADControlMotor(s32, u32);
#endif

bool gamepad_is_active()
{
#if defined(TARGET_WIIU)
    return s_connected && s_vpad_read_ok && s_player1_real_channel < 0;
#else
    return s_connected;
#endif
}

void drive_gamepad_motor(bool on)
{
    if (on) {
        u8 pattern[120];
        std::memset(pattern, 0xff, sizeof(pattern));
        VPADControlMotor(VPAD_CHAN_0, pattern, sizeof(pattern));
    } else {
        VPADStopMotor(VPAD_CHAN_0);
    }
}

void stop_all_motors()
{
    drive_gamepad_motor(false);
    s_gamepad_motor = false;
    s_gamepad_motor_age = 0;
#if defined(TARGET_WIIU)
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; ++chan) {
        if (s_real[chan].connected)
            __real_WPADControlMotor(chan, WPAD_MOTOR_STOP);
        s_real[chan].motor_on = false;
        s_real[chan].motor_stop_pending = false;
    }
#endif
}

#if defined(TARGET_WIIU)
void service_pro_motors()
{
    const u32 field = sms_wiiu_frame_count();
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; ++chan) {
        RealController& c = s_real[chan];
        if (!c.motor_on || !c.motor_stop_pending || field < c.motor_hold_until)
            continue;
        if (c.connected)
            __real_WPADControlMotor(chan, WPAD_MOTOR_STOP);
        c.motor_on = false;
        c.motor_stop_pending = false;
    }
}
#endif

void service_gamepad_motor()
{
    if (!s_gamepad_motor)
        return;
    if (!gamepad_is_active()) {
        drive_gamepad_motor(false);
        s_gamepad_motor = false;
        s_gamepad_motor_age = 0;
        return;
    }
    if (++s_gamepad_motor_age >= 600) {
        s_gamepad_motor_age = 0;
        drive_gamepad_motor(true);
    }
}

bool channel_valid(s32 chan)
{
    return chan >= 0 && chan < WPAD_MAX_CONTROLLERS;
}

void trim(char* text)
{
    if (!text)
        return;
    char* first = text;
    while (*first == ' ' || *first == '\t')
        ++first;
    if (first != text)
        std::memmove(text, first, std::strlen(first) + 1);
    size_t length = std::strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t'))
        text[--length] = '\0';
}

bool button_control(const char* name, u32* button)
{
    struct ButtonName { const char* name; u32 button; };
    static const ButtonName controls[] = {
        {"a", WPAD_BUTTON_A}, {"b", WPAD_BUTTON_B},
        {"one", WPAD_BUTTON_1}, {"two", WPAD_BUTTON_2},
        {"plus", WPAD_BUTTON_PLUS}, {"minus", WPAD_BUTTON_MINUS},
        {"home", WPAD_BUTTON_HOME}, {"up", WPAD_BUTTON_UP},
        {"down", WPAD_BUTTON_DOWN}, {"left", WPAD_BUTTON_LEFT},
        {"right", WPAD_BUTTON_RIGHT}, {"c", WPAD_BUTTON_C},
        {"z", WPAD_BUTTON_Z},
    };
    for (const ButtonName& control : controls) {
        if (std::strcmp(control.name, name) == 0) {
            *button = control.button;
            return true;
        }
    }
    return false;
}

/* Autoplay tokens use button@frame+hold; spin@frame+hold generates the same
 * Y/X press edge as a GamePad spin without exposing either button as a Wii
 * button. */

bool stick_control(const char* name, f32* x, f32* y)
{
    if (std::strcmp(name, "stick_left") == 0) {
        *x = -1.0f;
        *y = 0.0f;
    } else if (std::strcmp(name, "stick_right") == 0) {
        *x = 1.0f;
        *y = 0.0f;
    } else if (std::strcmp(name, "stick_up") == 0) {
        *x = 0.0f;
        *y = 1.0f;
    } else if (std::strcmp(name, "stick_down") == 0) {
        *x = 0.0f;
        *y = -1.0f;
    } else {
        return false;
    }
    return true;
}

bool parse_uint(const char* text, u32* value)
{
    if (!text || !*text)
        return false;
    char* end = nullptr;
    unsigned long parsed = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed > 0xfffffffful)
        return false;
    *value = static_cast<u32>(parsed);
    return true;
}

bool parse_float(const char* text, f32* value)
{
    if (!text || !*text)
        return false;
    char* end = nullptr;
    float parsed = std::strtof(text, &end);
    if (end == text || *end != '\0')
        return false;
    *value = parsed;
    return true;
}

void log_unknown(const char* token)
{
    port_log("[smg-input] autoplay: unknown token '%s'\n", token);
}

void log_script_full()
{
    if (s_script_full_reported)
        return;
    s_script_full_reported = true;
    port_log("[smg-input] autoplay: script full at %u events, dropping the rest\n",
             (unsigned)s_script_count);
}

#if defined(PORT_GAME_SMG)
int autoplay_run_seconds()
{
    const char* value = std::getenv("SMG_AUTOPLAY_SECS");
    const int seconds = value ? std::atoi(value) : 0;
    return seconds > 0 ? seconds : (s_script_present ? 300 : 180);
}

void check_autoplay_timeout()
{
    if (!s_marker || s_autopress_exit_requested)
        return;
    const int64_t now = sms_wiiu_OSGetTime();
    if (!s_autopress_start) {
        s_autopress_start = now;
        return;
    }
    const int seconds = autoplay_run_seconds();
    if (now - s_autopress_start < static_cast<int64_t>(seconds) * 62156250)
        return;
    s_autopress_exit_requested = true;
    stop_all_motors();
    port_log("[smg-input] autopress run over (%d s): returning to the Menu\n", seconds);
    /* Only flag it: the frame boundary drains the GPU before it asks for the
     * Menu. Asking first released the foreground with six GPU timestamps
     * outstanding and the exit never completed (2026-10-30). */
    sms_wiiu_menu_exit_pending = 1;
}
#endif

bool parse_timing(char* text, u32* at, u32* hold)
{
    char* plus = std::strchr(text, '+');
    if (plus)
        *plus++ = '\0';
    if (!parse_uint(text, at))
        return false;
    *hold = 1;
    if (plus && (!parse_uint(plus, hold) || *hold == 0))
        return false;
    return true;
}

void clamp_pointer(const char* axis, f32* value)
{
    if (*value < -1.0f) {
        port_log("[smg-input] autoplay: pointer %s %f clamped to -1.000000\n", axis,
                 *value);
        *value = -1.0f;
    } else if (*value > 1.0f) {
        port_log("[smg-input] autoplay: pointer %s %f clamped to 1.000000\n", axis,
                 *value);
        *value = 1.0f;
    }
}

void clamp_normalized(f32* value)
{
    if (*value < -1.0f)
        *value = -1.0f;
    else if (*value > 1.0f)
        *value = 1.0f;
}

static const int64_t kWiiuTicksPerSecond = 62156250;
static const int64_t kPointerHideTicks = kWiiuTicksPerSecond * 35 / 10;

void pointer_activity(int64_t now_ticks)
{
    s_pointer_last_activity_ticks = now_ticks;
    s_pointer_hidden = false;
}

void map_touch_to_pointer(const VPADTouchData& touch)
{
    /* VPADGetTPCalibratedPoint reports the GamePad screen in 1280x720 units
     * (hardware 2026-11-15: x=975 logged; the old 854x480 assumption made the
     * pointer run 1.5x too far right/down).  Galaxy's 16:9 pointer viewport is
     * 832/854 of the width, so the side bars are removed before normalizing. */
    static const f32 kDrcWidth = 1280.0f;
    static const f32 kDrcHeight = 720.0f;
    static const f32 kPointerWidth = kDrcWidth * (832.0f / 854.0f);
    static const f32 kDrcViewportLeft = (kDrcWidth - kPointerWidth) * 0.5f;
    s_hardware_pointer_x =
        (static_cast<f32>(touch.x) - kDrcViewportLeft) / kPointerWidth * 2.0f - 1.0f;
    s_hardware_pointer_y = static_cast<f32>(touch.y) / kDrcHeight * 2.0f - 1.0f;
    clamp_normalized(&s_hardware_pointer_x);
    clamp_normalized(&s_hardware_pointer_y);
}

bool touch_menu_context()
{
#if defined(TARGET_WIIU)
    /* StarPieceTarget (feeding a hungry Luma) is in the game's 2P-transparency list but wants Star Bits
     * shot at the target, so a tap there is B, not A (tester report 2026-11-17). */
    if (MR::isStarPointerModeStarPieceTarget != nullptr && MR::isStarPointerModeStarPieceTarget())
        return false;
    return MR::isStarPointer2PTransparencyMode != nullptr &&
        MR::isStarPointer2PTransparencyMode();
#else
    return false;
#endif
}

void finish_touch(int64_t now_ticks)
{
    if (!s_touch_active)
        return;
    const int64_t elapsed_ticks = now_ticks - s_touch_start_ticks;
    const u32 elapsed_ms = static_cast<u32>(elapsed_ticks * 1000 / kWiiuTicksPerSecond);
    const bool is_tap = elapsed_ms < 150 && s_touch_max_moved < 18.0f;
    const bool menu = is_tap && touch_menu_context();
    const bool shoot = is_tap && !menu && s_touch_tap_shoot && !s_touch_fed_this_touch;
    s_tap_pending_button = menu ? WPAD_BUTTON_A : (shoot ? WPAD_BUTTON_B : 0);
    if (s_tap_pending_button)
        s_tap_fire_frame = sms_wiiu_frame_count() + (menu ? kTapSettleFramesA : kTapSettleFramesB);
    if (s_touch_log_count < 30) {
        port_log("[smg-input] tap %u %u ms=%u moved=%.1f -> %s\n",
                 (unsigned)s_touch_last_x, (unsigned)s_touch_last_y,
                 (unsigned)elapsed_ms, (double)s_touch_max_moved,
                 menu ? "A" : (shoot ? "B" : "none"));
        ++s_touch_log_count;
    }
    s_touch_active = false;
    s_touch_release_pending = true;
}

f32 apply_stick_deadzone(f32 value)
{
    static const f32 kDeadzone = 0.12f;
    return std::fabs(value) <= kDeadzone ? 0.0f : value;
}

void apply_stick_deadzone(f32* x, f32* y)
{
    *x = apply_stick_deadzone(*x);
    *y = apply_stick_deadzone(*y);
}

void set_pointer_source(PointerSource source)
{
    if (source == s_pointer_source)
        return;
    s_pointer_source = source;
    if (source == POINTER_SOURCE_TOUCH)
        port_log("[smg-input] hardware pointer source: touch\n");
    else if (source == POINTER_SOURCE_STICK)
        port_log("[smg-input] hardware pointer source: right stick\n");
    else if (source == POINTER_SOURCE_GYRO)
        port_log("[smg-input] hardware pointer source: gyro\n");
}

void update_hardware_pointer(const VPADStatus& vpad)
{
    static const f32 kDeadzone = 0.12f;
    /* Pointer units are -1..1 on both axes of a 16:9 screen: equal pixel
     * speed needs 16/9 more units vertically. Integrate over real time, not
     * frames, so frame-rate dips neither slow nor stutter it (user: faster
     * left/right than up/down, jittery). Two screen widths per second. */
    static const f32 kSpeedX = 4.0f;
    static const f32 kSpeedY = 4.0f * 16.0f / 9.0f;
    static int64_t s_last_ticks;
    const int64_t now_ticks = sms_wiiu_OSGetTime();
    f32 dt = s_last_ticks ? static_cast<f32>(now_ticks - s_last_ticks) / kWiiuTicksPerSecond : 1.0f / 60.0f;
    s_last_ticks = now_ticks;
    if (dt > 0.1f) dt = 0.1f;
    const bool stick_x_moved = vpad.rightStick.x < -kDeadzone || vpad.rightStick.x > kDeadzone;
    const bool stick_y_moved = vpad.rightStick.y < -kDeadzone || vpad.rightStick.y > kDeadzone;
    if (stick_x_moved || stick_y_moved) {
        s_stick_pointer_x += vpad.rightStick.x * kSpeedX * dt;
        s_stick_pointer_y -= vpad.rightStick.y * kSpeedY * dt;
        clamp_normalized(&s_stick_pointer_x);
        clamp_normalized(&s_stick_pointer_y);
        s_stick_pointer_active = true;
        pointer_activity(now_ticks);
    }

    VPADTouchData touch;
    std::memset(&touch, 0, sizeof(touch));
    VPADGetTPCalibratedPoint(VPAD_CHAN_0, &touch, &vpad.tpNormal);
    const bool touch_down = touch.touched && touch.validity == VPAD_VALID;
    if (touch_down) {
        map_touch_to_pointer(touch);
        if (!s_touch_active) {
            s_touch_active = true;
            s_touch_start_x = touch.x;
            s_touch_start_y = touch.y;
            s_touch_max_moved = 0.0f;
            s_touch_start_ticks = now_ticks;
            s_touch_fed_this_touch = false;
            pointer_activity(now_ticks);
        } else if (touch.x != s_touch_last_x || touch.y != s_touch_last_y) {
            pointer_activity(now_ticks);
        }
        const f32 delta_x = static_cast<f32>(touch.x) - s_touch_start_x;
        const f32 delta_y = static_cast<f32>(touch.y) - s_touch_start_y;
        const f32 moved = std::sqrt(delta_x * delta_x + delta_y * delta_y);
        if (moved > s_touch_max_moved)
            s_touch_max_moved = moved;
        s_touch_last_x = touch.x;
        s_touch_last_y = touch.y;
        /* The right-stick pointer continues from where the finger left it;
         * otherwise releasing a drag snapped the cursor back to the old stick
         * position (hardware 2026-11-15, file select). */
        s_stick_pointer_x = s_hardware_pointer_x;
        s_stick_pointer_y = s_hardware_pointer_y;
        s_hardware_pointer_valid = true;
        s_touch_pointer_valid = true;
        set_pointer_source(POINTER_SOURCE_TOUCH);
#if defined(TARGET_WIIU)
        s_touch_feed = s_touch_tap_shoot && MR::isStarPointerModeStarPieceTarget != nullptr &&
            MR::isStarPointerModeStarPieceTarget();
#endif
        if (s_touch_feed) {
            if (!s_touch_fed_this_touch && s_touch_log_count < 30) {
                port_log("[smg-input] touch feed: B held while touching (StarPieceTarget)\n");
                ++s_touch_log_count;
            }
            s_touch_fed_this_touch = true;
            s_touch_feed_x = s_hardware_pointer_x;
            s_touch_feed_y = s_hardware_pointer_y;
            s_touch_feed_until_frame = sms_wiiu_frame_count() + kTouchFeedTailFrames;
        }
    } else {
        finish_touch(now_ticks);
        s_touch_pointer_valid = false;
        bool feed_mode = false;
#if defined(TARGET_WIIU)
        feed_mode = MR::isStarPointerModeStarPieceTarget != nullptr && MR::isStarPointerModeStarPieceTarget();
#endif
        s_touch_feed = s_touch_feed && feed_mode &&
            static_cast<s32>(sms_wiiu_frame_count() - s_touch_feed_until_frame) < 0;
    }

    if (!touch_down && s_stick_pointer_active && !s_touch_release_pending) {
        s_hardware_pointer_x = s_stick_pointer_x;
        s_hardware_pointer_y = s_stick_pointer_y;
        s_hardware_pointer_valid = true;
        set_pointer_source(POINTER_SOURCE_STICK);
    } else if (!touch_down && s_gyro_pointer_enabled) {
        /* The DRC gyro is exposed as a normalized aiming position relative to
         * the current R-button baseline.  R is intentionally not forwarded as
         * a Wii button: it recentres this fallback pointer. */
        if (vpad.hold & VPAD_BUTTON_R) {
            s_gyro_center_x = vpad.gyro.x;
            s_gyro_center_y = vpad.gyro.y;
            s_gyro_pointer_x = 0.0f;
            s_gyro_pointer_y = 0.0f;
        } else {
            s_gyro_pointer_x = vpad.gyro.x - s_gyro_center_x;
            s_gyro_pointer_y = vpad.gyro.y - s_gyro_center_y;
        }
        if (s_have_gyro_sample &&
            (std::fabs(vpad.gyro.x - s_previous_gyro_x) > 0.01f ||
             std::fabs(vpad.gyro.y - s_previous_gyro_y) > 0.01f))
            pointer_activity(now_ticks);
        s_previous_gyro_x = vpad.gyro.x;
        s_previous_gyro_y = vpad.gyro.y;
        s_have_gyro_sample = true;
        clamp_normalized(&s_gyro_pointer_x);
        clamp_normalized(&s_gyro_pointer_y);
        s_hardware_pointer_x = s_gyro_pointer_x;
        s_hardware_pointer_y = s_gyro_pointer_y;
        s_hardware_pointer_valid = true;
        set_pointer_source(POINTER_SOURCE_GYRO);
    }

    if (s_tap_pending_button && static_cast<s32>(sms_wiiu_frame_count() - s_tap_fire_frame) >= 0) {
        s_touch_tap_button = s_tap_pending_button;
        s_tap_pending_button = 0;
    }
    if (s_tap_pending_button)
        pointer_activity(now_ticks);  // keep the pointer shown while the press settles

    if (!s_pointer_last_activity_ticks)
        s_pointer_last_activity_ticks = now_ticks;
    else if (now_ticks - s_pointer_last_activity_ticks >= kPointerHideTicks)
        s_pointer_hidden = true;
}

enum AppendResult {
    APPEND_INVALID,
    APPEND_ADDED,
    APPEND_DROPPED_FULL,
};

AppendResult append_button_or_stick(char* name, char* timing)
{
    u32 button = 0;
    f32 stick_x = 0.0f;
    f32 stick_y = 0.0f;
    ScriptKind kind;
    if (button_control(name, &button)) {
        kind = SCRIPT_BUTTON;
    } else if (stick_control(name, &stick_x, &stick_y)) {
        kind = SCRIPT_STICK;
    } else if (std::strcmp(name, "shot") == 0) {
        kind = SCRIPT_SHOT; /* shot@<field>: one TV+DRC screenshot (needs <app root>shots/) */
    } else if (std::strcmp(name, "spin") == 0) {
        kind = SCRIPT_SPIN;
    } else {
        return APPEND_INVALID;
    }
    u32 at = 0;
    u32 hold = 0;
    if (!parse_timing(timing, &at, &hold))
        return APPEND_INVALID;
    if (s_script_count >= kMaxScriptEvents) {
        log_script_full();
        return APPEND_DROPPED_FULL;
    }
    ScriptEvent& event = s_script[s_script_count++];
    event = {};
    event.kind = kind;
    event.button = button;
    event.stick_x = stick_x;
    event.stick_y = stick_y;
    event.at = at;
    event.until = at + hold;
    std::snprintf(event.name, sizeof(event.name), "%s", name);
    return APPEND_ADDED;
}

AppendResult append_pointer(char* timing)
{
    char* comma = std::strchr(timing, ',');
    if (!comma)
        return APPEND_INVALID;
    *comma++ = '\0';
    char* second_comma = std::strchr(comma, ',');
    if (!second_comma)
        return APPEND_INVALID;
    *second_comma++ = '\0';
    char* plus = std::strchr(second_comma, '+');
    if (plus)
        *plus++ = '\0';
    u32 at = 0;
    u32 hold = 1;
    f32 x = 0.0f;
    f32 y = 0.0f;
    if (!parse_uint(timing, &at) || !parse_float(comma, &x) ||
        !parse_float(second_comma, &y) ||
        (plus && (!parse_uint(plus, &hold) || hold == 0)))
        return APPEND_INVALID;
    if (s_script_count >= kMaxScriptEvents) {
        log_script_full();
        return APPEND_DROPPED_FULL;
    }
    clamp_pointer("x", &x);
    clamp_pointer("y", &y);
    ScriptEvent& event = s_script[s_script_count++];
    event = {};
    event.kind = SCRIPT_POINTER;
    event.pointer_x = x;
    event.pointer_y = y;
    event.at = at;
    event.until = at + hold;
    std::snprintf(event.name, sizeof(event.name), "pointer");
    return APPEND_ADDED;
}

void load_script()
{
    if (s_script_loaded)
        return;
    s_script_loaded = true;

    char marker_path[256];
    std::snprintf(marker_path, sizeof(marker_path), "%sautopress", sms_wiiu_app_root());
    FILE* marker = std::fopen(marker_path, "rb");
    s_marker = marker != nullptr;
    if (marker)
        std::fclose(marker);
    if (!s_marker) {
        s_connected = false;
        port_log("[smg-input] autopress marker absent: port 0 disconnected\n");
        return;
    }

    s_connected = true;
    char script_path[256];
    std::snprintf(script_path, sizeof(script_path), "%ssmg-autoplay", sms_wiiu_app_root());
    FILE* file = std::fopen(script_path, "rb");
    if (file) {
        s_script_present = true;
        static char buffer[16384]; // load_script runs once; keep 16 KB off the thread stack
        size_t length = std::fread(buffer, 1, sizeof(buffer) - 1, file);
        const bool truncated = length == sizeof(buffer) - 1 && std::fgetc(file) != EOF;
        std::fclose(file);
        if (truncated) {
            while (length && buffer[length - 1] != ',' && buffer[length - 1] != '\r' &&
                   buffer[length - 1] != '\n')
                --length;
            port_log("[smg-input] autoplay: script larger than %u bytes, "
                     "truncated at the last complete token\n",
                     (unsigned)(sizeof(buffer) - 1));
        }
        buffer[length] = '\0';
        char* save = nullptr;
        char* token = ::strtok_r(buffer, ",\r\n", &save);
        while (token) {
            trim(token);
            bool advance = true;
            if (*token && *token != '#') {
                char* equals = std::strchr(token, '=');
                if (equals) {
                    *equals = '\0';
                    trim(token);
                    trim(equals + 1);
                    setenv(token, equals + 1, 1);
                    port_log("[smg-input] autoplay env %s=%s\n", token, equals + 1);
                } else if (std::strncmp(token, "pointer@", 8) == 0) {
                    char* x = ::strtok_r(nullptr, ",\r\n", &save);
                    char* y = ::strtok_r(nullptr, ",\r\n", &save);
                    if (!x || std::strchr(x, '@')) {
                        port_log("[smg-input] autoplay: malformed pointer directive '%s'\n",
                                 token);
                        token = x;
                        advance = false;
                    } else {
                        trim(x);
                        if (!y || std::strchr(y, '@')) {
                            port_log("[smg-input] autoplay: malformed pointer directive '%s'\n",
                                     token);
                            token = y;
                            advance = false;
                        } else {
                            trim(y);
                            char pointer_timing[512];
                            if (std::snprintf(pointer_timing, sizeof(pointer_timing),
                                              "%s,%s,%s", token + 8, x, y) >=
                                    static_cast<int>(sizeof(pointer_timing)) ||
                                append_pointer(pointer_timing) == APPEND_INVALID)
                                log_unknown(token);
                        }
                    }
                } else {
                    char* at = std::strchr(token, '@');
                    if (!at) {
                        log_unknown(token);
                    } else {
                        *at++ = '\0';
                        trim(token);
                        if (append_button_or_stick(token, at) == APPEND_INVALID)
                            log_unknown(token);
                    }
                }
            }
            if (advance)
                token = ::strtok_r(nullptr, ",\r\n", &save);
        }
    }
    port_log("[smg-input] autoplay: %u scripted events\n", (unsigned)s_script_count);
}

void map_vpad(const VPADStatus& vpad)
{
    u32 result = 0;
    if (vpad.hold & VPAD_BUTTON_A) result |= WPAD_BUTTON_A;
    if (vpad.hold & VPAD_BUTTON_B) result |= WPAD_BUTTON_B;
    if (vpad.hold & VPAD_BUTTON_ZR) result |= WPAD_BUTTON_B;
    /* Right-stick click shoots a Star Bit too: aim with the right stick
     * (pointer) and click it (user request, 2026-11-18). */
    if (vpad.hold & VPAD_BUTTON_STICK_R) result |= WPAD_BUTTON_B;
    if (vpad.hold & VPAD_BUTTON_ZL) result |= WPAD_BUTTON_Z;
    if (vpad.hold & VPAD_BUTTON_L) result |= WPAD_BUTTON_C;
    if (vpad.hold & VPAD_BUTTON_LEFT) result |= WPAD_BUTTON_LEFT;
    if (vpad.hold & VPAD_BUTTON_RIGHT) result |= WPAD_BUTTON_RIGHT;
    if (vpad.hold & VPAD_BUTTON_UP) result |= WPAD_BUTTON_UP;
    if (vpad.hold & VPAD_BUTTON_DOWN) result |= WPAD_BUTTON_DOWN;
    if (vpad.hold & VPAD_BUTTON_PLUS) result |= WPAD_BUTTON_PLUS;
    if (vpad.hold & VPAD_BUTTON_MINUS) result |= WPAD_BUTTON_MINUS;
    if (vpad.hold & VPAD_BUTTON_HOME) result |= WPAD_BUTTON_HOME;
    s_vpad_hold = result;
    s_vpad_spin_hold = (vpad.hold & (VPAD_BUTTON_X | VPAD_BUTTON_Y)) != 0;
    s_vpad_stick_x = vpad.leftStick.x;
    s_vpad_stick_y = vpad.leftStick.y;
    apply_stick_deadzone(&s_vpad_stick_x, &s_vpad_stick_y);
    s_vpad_active = vpad.hold || vpad.trigger || vpad.release ||
        vpad.leftStick.x < -0.12f || vpad.leftStick.x > 0.12f ||
        vpad.leftStick.y < -0.12f || vpad.leftStick.y > 0.12f ||
        vpad.rightStick.x < -0.12f || vpad.rightStick.x > 0.12f ||
        vpad.rightStick.y < -0.12f || vpad.rightStick.y > 0.12f ||
        (vpad.tpNormal.touched && vpad.tpNormal.validity == VPAD_VALID);
    update_hardware_pointer(vpad);
    if (s_touch_tap_button)
        s_vpad_active = true;
}

void poll_vpad()
{
    VPADStatus vpad;
    std::memset(&vpad, 0, sizeof(vpad));
    VPADReadError error = VPAD_READ_NO_SAMPLES;
    static uint32_t s_vpad_no_sample_reads;
    const bool got = VPADRead(VPAD_CHAN_0, &vpad, 1, &error) > 0 && error == VPAD_READ_SUCCESS;
    if (!got && error == VPAD_READ_NO_SAMPLES && ++s_vpad_no_sample_reads > 30)
        error = static_cast<VPADReadError>(-2);  // half a second with no samples: treat as absent (INVALID_CONTROLLER)
    if (got) {
        s_vpad_no_sample_reads = 0;
        map_vpad(vpad);
        s_vpad_read_ok = true;
        if (!s_marker)
            s_connected = true;
    } else if (error != VPAD_READ_NO_SAMPLES) {
        /* A GamePad that is off or disconnected (VPAD_READ_INVALID_CONTROLLER)
         * must not leave its previous activity selecting player 1 over a
         * readable KPAD controller.  NO_SAMPLES ("nothing new yet") happens
         * with a connected GamePad too and keeps the last state, so a held
         * button is not released for a frame. */
        s_vpad_read_ok = false;
        s_vpad_active = false;
        s_vpad_hold = 0;
        s_vpad_spin_hold = false;
        s_vpad_stick_x = 0.0f;
        s_vpad_stick_y = 0.0f;
    }
}

#if defined(TARGET_WIIU)
u32 map_real_layout_buttons(u32 buttons, bool classic)
{
    const u32 up = classic ? 0x00000001u : 0x00000001u;
    const u32 left = classic ? 0x00000002u : 0x00000002u;
    const u32 down = classic ? 0x00004000u : 0x00004000u;
    const u32 right = classic ? 0x00008000u : 0x00008000u;
    const u32 a = classic ? 0x00000010u : 0x00000010u;
    const u32 b = classic ? 0x00000040u : 0x00000040u;
    const u32 x = classic ? 0x00000008u : 0x00000008u;
    const u32 y = classic ? 0x00000020u : 0x00000020u;
    const u32 zl = classic ? 0x00000080u : 0x00000080u;
    const u32 zr = classic ? 0x00000004u : 0x00000004u;
    const u32 l = classic ? 0x00002000u : 0x00002000u;
    const u32 plus = classic ? 0x00000400u : 0x00000400u;
    const u32 minus = classic ? 0x00001000u : 0x00001000u;
    const u32 home = classic ? 0x00000800u : 0x00000800u;
    u32 result = 0;
    if (buttons & left) result |= WPAD_BUTTON_LEFT;
    if (buttons & right) result |= WPAD_BUTTON_RIGHT;
    if (buttons & down) result |= WPAD_BUTTON_DOWN;
    if (buttons & up) result |= WPAD_BUTTON_UP;
    if (buttons & a) result |= WPAD_BUTTON_A;
    if (buttons & b) result |= WPAD_BUTTON_B;
    if (buttons & zr) result |= WPAD_BUTTON_B;
    /* Pro Controller right-stick click (WPAD_PRO_BUTTON_STICK_R) shoots, as
     * on the GamePad.  The Classic Controller has no stick clicks; the same
     * bit there is a left-stick emulation direction, so Pro only. */
    if (!classic && (buttons & 0x00010000u)) result |= WPAD_BUTTON_B;
    if (buttons & zl) result |= WPAD_BUTTON_Z;
    if (buttons & l) result |= WPAD_BUTTON_C;
    if (buttons & plus) result |= WPAD_BUTTON_PLUS;
    if (buttons & minus) result |= WPAD_BUTTON_MINUS;
    if (buttons & home) result |= WPAD_BUTTON_HOME;
    (void)x;
    (void)y;
    return result;
}

void update_real_pointer(RealController& controller, f32 x, f32 y)
{
    static const f32 kDeadzone = 0.12f;
    static const f32 kSpeed = 4.0f;
    static int64_t last_ticks;
    const int64_t now_ticks = sms_wiiu_OSGetTime();
    f32 dt = last_ticks ? static_cast<f32>(now_ticks - last_ticks) / 62156250.0f : 1.0f / 60.0f;
    last_ticks = now_ticks;
    if (dt > 0.1f) dt = 0.1f;
    if (x < -kDeadzone || x > kDeadzone || y < -kDeadzone || y > kDeadzone) {
        controller.pointer_x += x * kSpeed * dt;
        controller.pointer_y -= y * kSpeed * dt * 16.0f / 9.0f;
        clamp_normalized(&controller.pointer_x);
        clamp_normalized(&controller.pointer_y);
    }
}

bool real_has_activity(const RealController& controller)
{
    if (!controller.sampled)
        return false;
    const WutKPADStatus& raw = controller.raw;
    if ((raw.trig & kWiiRemoteButtonMask) || (raw.release & kWiiRemoteButtonMask) ||
        (raw.hold & kWiiRemoteButtonMask) || controller.shake)
        return true;
    if (raw.extension_type == WPAD_EXT_NUNCHUK || raw.extension_type == WPAD_EXT_MPLUS_NUNCHUK) {
        const u32 nunchuk_buttons = WPAD_BUTTON_Z | WPAD_BUTTON_C;
        return (raw.ext.nunchuk.hold & nunchuk_buttons) ||
               (raw.ext.nunchuk.trig & nunchuk_buttons) ||
               (raw.ext.nunchuk.release & nunchuk_buttons) ||
               raw.ext.nunchuk.stick.x < -0.12f || raw.ext.nunchuk.stick.x > 0.12f ||
               raw.ext.nunchuk.stick.y < -0.12f || raw.ext.nunchuk.stick.y > 0.12f;
    }
    if (raw.extension_type == WPAD_EXT_CLASSIC || raw.extension_type == WPAD_EXT_MPLUS_CLASSIC) {
        return raw.ext.classic.hold || raw.ext.classic.trig || raw.ext.classic.release ||
               raw.ext.classic.left_stick.x < -0.12f || raw.ext.classic.left_stick.x > 0.12f ||
               raw.ext.classic.left_stick.y < -0.12f || raw.ext.classic.left_stick.y > 0.12f ||
               raw.ext.classic.right_stick.x < -0.12f || raw.ext.classic.right_stick.x > 0.12f ||
               raw.ext.classic.right_stick.y < -0.12f || raw.ext.classic.right_stick.y > 0.12f;
    }
    if (raw.extension_type == WPAD_EXT_PRO_CONTROLLER) {
        return raw.ext.pro.hold || raw.ext.pro.trig || raw.ext.pro.release ||
               raw.ext.pro.left_stick.x < -0.12f || raw.ext.pro.left_stick.x > 0.12f ||
               raw.ext.pro.left_stick.y < -0.12f || raw.ext.pro.left_stick.y > 0.12f ||
               raw.ext.pro.right_stick.x < -0.12f || raw.ext.pro.right_stick.x > 0.12f ||
               raw.ext.pro.right_stick.y < -0.12f || raw.ext.pro.right_stick.y > 0.12f;
    }
    return false;
}

f32 acc_delta(const WutKPADVec3& a, const WutKPADVec3& b)
{
    const f32 x = a.x - b.x;
    const f32 y = a.y - b.y;
    const f32 z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

void update_real_shake(RealController& controller, u32 field)
{
    const WutKPADStatus& raw = controller.raw;
    f32 shake_g = std::fmax(raw.acc_variation, controller.sample_shake_g);
    if (controller.have_previous_acc)
        shake_g = std::fmax(shake_g, acc_delta(raw.acc, controller.previous_acc));
    controller.previous_acc = raw.acc;

    if (raw.extension_type == WPAD_EXT_NUNCHUK || raw.extension_type == WPAD_EXT_MPLUS_NUNCHUK) {
        const WutKPADNunchuk& nunchuk = raw.ext.nunchuk;
        if (controller.have_previous_acc)
            shake_g = std::fmax(shake_g,
                                acc_delta(nunchuk.acc, controller.previous_nunchuk_acc));
        shake_g = std::fmax(shake_g, nunchuk.acc_variation);
        controller.previous_nunchuk_acc = nunchuk.acc;
    }
    controller.have_previous_acc = true;
    controller.shake = false;
    if (shake_g < kShakeThresholdG || field < controller.shake_debounce_until)
        return;
    controller.shake = true;
    controller.shake_debounce_until = field + kShakeDebounceFields;
    if (s_shake_log_count < 20) {
        port_log("[smg-input] shake chan %d g=%.2f\n",
                 (int)(&controller - s_real), (double)shake_g);
        ++s_shake_log_count;
    }
}

void translate_real_status(RealController& controller)
{
    const WutKPADStatus& raw = controller.raw;
    KPADStatus& status = controller.status;
    std::memset(&status, 0, sizeof(status));
    status.hold = raw.hold & kWiiRemoteButtonMask;
    status.trig = raw.trig & kWiiRemoteButtonMask;
    status.release = raw.release & kWiiRemoteButtonMask;
    status.acc.x = raw.acc.x;
    status.acc.y = raw.acc.y;
    status.acc.z = raw.acc.z;
    status.acc_value = raw.acc_magnitude;
    status.acc_speed = raw.acc_variation;
    status.pos.x = raw.pos.x;
    status.pos.y = raw.pos.y;
    clamp_normalized(&status.pos.x);
    clamp_normalized(&status.pos.y);
    if (!raw.pos_valid) {
        status.pos.x = 0.0f;
        status.pos.y = 0.0f;
    }
    status.vec.x = raw.pos_diff.x;
    status.vec.y = raw.pos_diff.y;
    status.speed = raw.pos_diff_magnitude;
    status.horizon.x = raw.angle.x;
    status.horizon.y = raw.angle.y;
    status.hori_vec.x = raw.angle_diff.x;
    status.hori_vec.y = raw.angle_diff.y;
    status.hori_speed = raw.angle_diff_magnitude;
    status.dist = raw.dist;
    status.dist_vec = raw.dist_diff;
    status.dist_speed = raw.dist_diff_magnitude;
    status.acc_vertical.x = raw.down.x;
    status.acc_vertical.y = raw.down.y;
    status.dev_type = WPAD_DEV_FREESTYLE;
    status.wpad_err = raw.error;
    status.dpd_valid_fg = raw.pos_valid ? 2 : 0;
    status.data_format = raw.format;
    if (raw.extension_type != WPAD_EXT_CLASSIC &&
        raw.extension_type != WPAD_EXT_MPLUS_CLASSIC &&
        raw.extension_type != WPAD_EXT_PRO_CONTROLLER) {
        controller.pointer_x = status.pos.x;
        controller.pointer_y = status.pos.y;
    }

    if (raw.extension_type == WPAD_EXT_NUNCHUK || raw.extension_type == WPAD_EXT_MPLUS_NUNCHUK) {
        /* wut's Nunchuk word also carries stick emulation (WPAD_NUNCHUK_STICK_EMULATION_*,
         * 0x1-0x8), the same bits as the Wii Remote D-pad: pass only C and Z, or every
         * stick push also presses the D-pad (camera). */
        const u32 nunchuk_buttons = WPAD_BUTTON_Z | WPAD_BUTTON_C; // 0x2000 | 0x4000, as in wut
        status.hold |= raw.ext.nunchuk.hold & nunchuk_buttons;
        status.trig |= raw.ext.nunchuk.trig & nunchuk_buttons;
        status.release |= raw.ext.nunchuk.release & nunchuk_buttons;
        status.ex_status.fs.stick.x = raw.ext.nunchuk.stick.x;
        status.ex_status.fs.stick.y = raw.ext.nunchuk.stick.y;
        apply_stick_deadzone(&status.ex_status.fs.stick.x, &status.ex_status.fs.stick.y);
    } else if (raw.extension_type == WPAD_EXT_CLASSIC || raw.extension_type == WPAD_EXT_MPLUS_CLASSIC) {
        status.hold = map_real_layout_buttons(raw.ext.classic.hold, true);
        status.trig = map_real_layout_buttons(raw.ext.classic.trig, true);
        status.release = map_real_layout_buttons(raw.ext.classic.release, true);
        status.ex_status.fs.stick.x = raw.ext.classic.left_stick.x;
        status.ex_status.fs.stick.y = raw.ext.classic.left_stick.y;
        apply_stick_deadzone(&status.ex_status.fs.stick.x, &status.ex_status.fs.stick.y);
        update_real_pointer(controller, raw.ext.classic.right_stick.x, raw.ext.classic.right_stick.y);
        status.pos.x = controller.pointer_x;
        status.pos.y = controller.pointer_y;
        status.dpd_valid_fg = 2;
    } else if (raw.extension_type == WPAD_EXT_PRO_CONTROLLER) {
        status.hold = map_real_layout_buttons(raw.ext.pro.hold, false);
        status.trig = map_real_layout_buttons(raw.ext.pro.trig, false);
        status.release = map_real_layout_buttons(raw.ext.pro.release, false);
        status.ex_status.fs.stick.x = raw.ext.pro.left_stick.x;
        status.ex_status.fs.stick.y = raw.ext.pro.left_stick.y;
        apply_stick_deadzone(&status.ex_status.fs.stick.x, &status.ex_status.fs.stick.y);
        update_real_pointer(controller, raw.ext.pro.right_stick.x, raw.ext.pro.right_stick.y);
        status.pos.x = controller.pointer_x;
        status.pos.y = controller.pointer_y;
        status.dpd_valid_fg = 2;
    }
    status.ex_status.fs.acc = status.acc;
    status.ex_status.fs.acc_value = status.acc_value;
    status.ex_status.fs.acc_speed = status.acc_speed;
}

/* The device type the game expects for a real controller: Galaxy reads it from the
 * extension callback and WPADProbe, and only FREESTYLE (Remote + Nunchuk) counts as a
 * playable controller. The Pro Controller plays as one, like the GamePad. */
u32 real_dev_type(u8 extension)
{
    if (extension == WPAD_EXT_PRO_CONTROLLER)
        return WPAD_DEV_FREESTYLE;
    if (extension == WPAD_EXT_CLASSIC || extension == WPAD_EXT_MPLUS_CLASSIC)
        return WPAD_DEV_CLASSIC;
    if (extension == WPAD_EXT_NUNCHUK || extension == WPAD_EXT_MPLUS_NUNCHUK)
        return WPAD_DEV_FREESTYLE;
    return WPAD_DEV_CORE;
}

const char* real_extension_name(u8 extension)
{
    if (extension == WPAD_EXT_NUNCHUK || extension == WPAD_EXT_MPLUS_NUNCHUK)
        return "wiimote+nunchuk";
    if (extension == WPAD_EXT_CLASSIC || extension == WPAD_EXT_MPLUS_CLASSIC)
        return "classic";
    if (extension == WPAD_EXT_PRO_CONTROLLER)
        return "pro";
    return "wiimote";
}

void poll_real_inputs()
{
    poll_vpad();
    const u32 field = sms_wiiu_frame_count();
    if (s_real_polled_frame == field)
        return;
    s_real_polled_frame = field;
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; ++chan) {
        RealController& controller = s_real[chan];
        s32 error = KPAD_ERROR_NO_SAMPLES;
        static WutKPADStatus samples[8];
        std::memset(samples, 0, sizeof(samples));
        const u32 count = __real_KPADReadEx(chan, samples, 8, &error);
        const WutKPADStatus& raw = samples[0];  // newest
        controller.sample_shake_g = 0.0f;
        for (u32 i = 0; i < count && i < 8; ++i) {
            controller.sample_shake_g = std::fmax(controller.sample_shake_g, samples[i].acc_variation);
            if (i + 1 < count && i + 1 < 8)
                controller.sample_shake_g =
                    std::fmax(controller.sample_shake_g, acc_delta(samples[i].acc, samples[i + 1].acc));
        }
        if (count && error == KPAD_ERROR_OK) {
            const bool was_connected = controller.connected;
            controller.raw = raw;
            controller.sampled = true;
            controller.connected = true;
            update_real_shake(controller, field);
            controller.extension_type = raw.extension_type;
            if (!was_connected)
                controller.connect_pending = true;
            if (controller.previous_extension_type != controller.extension_type) {
                controller.extension_pending = true;
                controller.previous_extension_type = controller.extension_type;
            }
            translate_real_status(controller);
        } else {
            controller.sampled = false;
            controller.shake = false;
            controller.have_previous_acc = false;
            if (controller.connected && error == -2) {
                controller.connected = false;
                controller.disconnect_pending = true;
                controller.extension_pending = false;
                port_log("[smg-input] chan %d: disconnected\n", (int)chan);
            }
        }
    }

    s32 selected = s_player1_real_channel;
    if (s_connected && s_vpad_read_ok && s_vpad_active)
        selected = -1;
    else if (s_real[WPAD_CHAN0].connected && real_has_activity(s_real[WPAD_CHAN0]))
        selected = WPAD_CHAN0;
    else if (selected >= 0 && !s_real[selected].connected)
        selected = -1;
    s_player1_real_channel = selected;
}
#endif

void report_callbacks()
{
#if defined(TARGET_WIIU)
    for (s32 chan = 0; chan < WPAD_MAX_CONTROLLERS; ++chan) {
        RealController& controller = s_real[chan];
        if (controller.connect_pending) {
            controller.connect_pending = false;
            port_log("[smg-input] chan %d: %s\n", (int)chan,
                     real_extension_name(controller.extension_type));
            if (s_connect_callbacks[chan])
                s_connect_callbacks[chan](chan, WPAD_ERR_NONE);
        }
        if (controller.disconnect_pending) {
            controller.disconnect_pending = false;
            if (s_connect_callbacks[chan])
                s_connect_callbacks[chan](chan, WPAD_ERR_NO_CONTROLLER);
        }
        if (controller.extension_pending) {
            controller.extension_pending = false;
            port_log("[smg-input] chan %d: %s\n", (int)chan,
                     real_extension_name(controller.extension_type));
            if (s_extension_callbacks[chan])
                s_extension_callbacks[chan](chan, real_dev_type(controller.extension_type));
        }
    }
    if (s_connected && s_player1_real_channel < 0) {
        if (!s_connect_reported) {
            s_connect_reported = true;
            port_log("[smg-input] chan 0: gamepad\n");
            if (s_connect_callbacks[WPAD_CHAN0])
                s_connect_callbacks[WPAD_CHAN0](WPAD_CHAN0, WPAD_ERR_NONE);
        }
        if (!s_extension_reported) {
            s_extension_reported = true;
            if (s_extension_callbacks[WPAD_CHAN0])
                s_extension_callbacks[WPAD_CHAN0](WPAD_CHAN0, WPAD_DEV_FREESTYLE);
        }
    }
    return;
#endif
    if (!s_connected)
        return;
    if (!s_connect_reported) {
        s_connect_reported = true;
        if (s_connect_callbacks[WPAD_CHAN0])
            s_connect_callbacks[WPAD_CHAN0](WPAD_CHAN0, WPAD_ERR_NONE);
    }
    if (!s_extension_reported) {
        s_extension_reported = true;
        if (s_extension_callbacks[WPAD_CHAN0])
            s_extension_callbacks[WPAD_CHAN0](WPAD_CHAN0, WPAD_DEV_FREESTYLE);
    }
}

void fill_status(KPADStatus& status, s32 chan)
{
    std::memset(&status, 0, sizeof(status));
    const u32 field = sms_wiiu_frame_count();
    u32 hold = s_vpad_hold;
    f32 stick_x = s_vpad_stick_x;
    f32 stick_y = s_vpad_stick_y;
    const ScriptEvent* pointer = nullptr;
    bool spin = s_vpad_spin_hold;
    bool has_real = false;
#if defined(TARGET_WIIU)
    s32 real_chan = -1;
    if (chan == WPAD_CHAN0)
        real_chan = s_player1_real_channel;
    else if (channel_valid(chan) && s_real[chan].connected)
        real_chan = chan;
    has_real = real_chan >= 0;
    if (has_real) {
        status = s_real[real_chan].status;
        hold = status.hold;
        stick_x = status.ex_status.fs.stick.x;
        stick_y = status.ex_status.fs.stick.y;
        spin = s_real[real_chan].shake;
        const WutKPADStatus& raw = s_real[real_chan].raw;
        if (raw.extension_type == WPAD_EXT_PRO_CONTROLLER)
            spin = (raw.ext.pro.hold & (0x08u | 0x20u)) != 0;
        else if (raw.extension_type == WPAD_EXT_CLASSIC || raw.extension_type == WPAD_EXT_MPLUS_CLASSIC)
            spin = (raw.ext.classic.hold & (0x08u | 0x20u)) != 0;
        if (s_touch_feed && !s_touch_pointer_valid) {
            status.pos.x = s_touch_feed_x;
            status.pos.y = s_touch_feed_y;
            status.dpd_valid_fg = 2;
        } else if (s_touch_pointer_valid || s_touch_tap_button || s_tap_pending_button) {
            status.pos.x = s_hardware_pointer_x;
            status.pos.y = s_hardware_pointer_y;
            status.dpd_valid_fg = 2;
        }
    }
#endif
    const bool allow_script =
#if defined(TARGET_WIIU)
        !has_real;
#else
        true;
#endif
    if (allow_script) for (u32 i = 0; i < s_script_count; ++i) {
        ScriptEvent& event = s_script[i];
        const bool active = field >= event.at && field < event.until;
        if (!active)
            continue;
        if (!event.logged) {
            event.logged = true;
            if (event.kind == SCRIPT_SHOT) {
                sms_wiiu_shot_request = 1;
                port_log("[smg-input] autoplay: shot field %u\n", (unsigned)field);
            } else if (event.kind == SCRIPT_POINTER)
                port_log("[smg-input] autoplay: pointer field %u\n", (unsigned)field);
            else
                port_log("[smg-input] autoplay: press %s field %u\n", event.name,
                         (unsigned)field);
        }
        if (event.kind == SCRIPT_SHOT)
            continue;
        if (event.kind == SCRIPT_BUTTON)
            hold |= event.button;
        else if (event.kind == SCRIPT_STICK) {
            stick_x = event.stick_x;
            stick_y = event.stick_y;
        } else if (event.kind == SCRIPT_SPIN)
            spin = true;
        else {
            pointer = &event;
        }
    }

    if (field != s_spin_last_frame) {
        if (spin && !s_spin_hold)
            s_spin_frames_since_press = 0;
        else if (s_spin_frames_since_press != ~0u)
            ++s_spin_frames_since_press;
        s_spin_hold = spin;
        s_spin_last_frame = field;
    }

    if (!has_real) {
        const u32 touch_hold = s_touch_feed ? WPAD_BUTTON_B : 0u;
        status.hold = hold | touch_hold | s_touch_tap_button;
        status.trig = ((hold | touch_hold) & ~s_previous_hold) | s_touch_tap_button;
    }
    if (status.trig && s_script_count)
        port_log("[smg-input] trig 0x%x field %u pointer=%d\n", (unsigned)status.trig,
                 (unsigned)field, pointer != nullptr);
    if (!has_real) {
        status.release = s_previous_hold & ~status.hold;
        s_previous_hold = status.hold;
        status.acc.x = 0.0f;
        status.acc.y = -1.0f;
        status.acc.z = 0.0f;
        status.acc_value = 1.0f;
        status.pos.x = pointer ? pointer->pointer_x : 0.0f;
        status.pos.y = pointer ? pointer->pointer_y : 0.0f;
        if (!pointer && s_hardware_pointer_valid && !s_pointer_hidden) {
            status.pos.x = s_hardware_pointer_x;
            status.pos.y = s_hardware_pointer_y;
        }
        status.horizon.x = 1.0f;
        status.horizon.y = 0.0f;
        status.dist = 2.0f;
        status.acc_vertical.x = 1.0f;
        status.acc_vertical.y = 0.0f;
        status.dev_type = WPAD_DEV_FREESTYLE;
        status.wpad_err = WPAD_ERR_NONE;
        status.dpd_valid_fg =
            (pointer || (s_hardware_pointer_valid && !s_pointer_hidden)) ? 2 : 0;
        status.data_format = static_cast<u8>(s_data_format);
    }
    status.ex_status.fs.stick.x = stick_x;
    status.ex_status.fs.stick.y = stick_y;
    if (!has_real) {
        status.ex_status.fs.acc.x = 0.0f;
        status.ex_status.fs.acc.y = -1.0f;
        status.ex_status.fs.acc.z = 0.0f;
        status.ex_status.fs.acc_value = 1.0f;
    }
    s_touch_tap_button = 0;
    s_touch_release_pending = false;
}

} // namespace

extern "C" {

#if defined(TARGET_WIIU)
void __wrap_WPADInit(void)
#else
void WPADInit(void)
#endif
{
    if (s_initialized)
        return;
    s_initialized = true;
    s_connected = false;
    s_previous_hold = 0;
    s_spin_hold = false;
    s_vpad_spin_hold = false;
    s_vpad_active = false;
    s_vpad_read_ok = false;
    s_gamepad_motor = false;
    s_gamepad_motor_age = 0;
    s_shake_log_count = 0;
    s_spin_last_frame = ~0u;
    s_spin_frames_since_press = ~0u;
    s_vpad_hold = 0;
    s_vpad_stick_x = 0.0f;
    s_vpad_stick_y = 0.0f;
    s_gyro_pointer_x = 0.0f;
    s_gyro_pointer_y = 0.0f;
    s_gyro_center_x = 0.0f;
    s_gyro_center_y = 0.0f;
    s_stick_pointer_x = 0.0f;
    s_stick_pointer_y = 0.0f;
    s_stick_pointer_active = false;
    s_gyro_pointer_enabled = sms_wiiu_sd_marker("gyropointer") != 0;
    port_log("[smg-input] gyro pointer %s (SD marker gyropointer)\n",
             s_gyro_pointer_enabled ? "enabled" : "disabled");
    s_pointer_source = POINTER_SOURCE_NONE;
    s_hardware_pointer_valid = false;
    s_touch_pointer_valid = false;
    s_touch_feed = false;
    s_hardware_pointer_x = 0.0f;
    s_hardware_pointer_y = 0.0f;
    s_touch_active = false;
    s_touch_release_pending = false;
    s_touch_start_x = 0;
    s_touch_start_y = 0;
    s_touch_last_x = 0;
    s_touch_last_y = 0;
    s_touch_max_moved = 0.0f;
    s_touch_start_ticks = 0;
    s_touch_tap_button = 0;
    s_touch_log_count = 0;
    s_tap_pending_button = 0;
    s_touch_tap_shoot = sms_wiiu_sd_marker("notapshoot") == 0;
    s_pointer_last_activity_ticks = 0;
    s_pointer_hidden = false;
    s_have_gyro_sample = false;
    s_previous_gyro_x = 0.0f;
    s_previous_gyro_y = 0.0f;
    port_log("[smg-input] touch_tap_shoot %s (SD marker notapshoot)\n",
             s_touch_tap_shoot ? "on" : "off");
    std::memset(s_extension_callbacks, 0, sizeof(s_extension_callbacks));
    std::memset(s_connect_callbacks, 0, sizeof(s_connect_callbacks));
    std::memset(s_sampling_callbacks, 0, sizeof(s_sampling_callbacks));
#if defined(TARGET_WIIU)
    std::memset(s_real, 0, sizeof(s_real));
    s_real_polled_frame = ~0u;
    s_player1_real_channel = -1;
#endif
    load_script();
}

#if defined(TARGET_WIIU)
void __wrap_KPADInit(void)
{
    __real_KPADInit();
    __wrap_WPADInit();
    VPADInit();
}
#else
void KPADInit(void)
{
    WPADInit();
    VPADInit();
}
#endif

#if defined(TARGET_WIIU)
void __wrap_KPADReset(void)
#else
void KPADReset(void)
#endif
{
#if defined(TARGET_WIIU)
    __real_KPADReset();
#endif
    s_previous_hold = 0;
    s_spin_hold = false;
    s_vpad_spin_hold = false;
    s_spin_last_frame = ~0u;
    s_spin_frames_since_press = ~0u;
    s_connect_reported = false;
    s_extension_reported = false;
}

#if defined(TARGET_WIIU)
uint32_t sms_wiiu_spin_frames_since_press(void)
{
    return s_spin_frames_since_press;
}
#endif

#if defined(TARGET_WIIU)
void __wrap_KPADSetAccParam(s32 chan, f32 a, f32 b) { __real_KPADSetAccParam(chan, a, b); }
void __wrap_KPADSetBtnRepeat(s32 chan, f32 a, f32 b) { __real_KPADSetBtnRepeat(chan, a, b); }
void __wrap_KPADSetDistParam(s32 chan, f32 a, f32 b) { __real_KPADSetDistParam(chan, a, b); }
void __wrap_KPADSetHoriParam(s32 chan, f32 a, f32 b) { __real_KPADSetHoriParam(chan, a, b); }
void __wrap_KPADSetPosParam(s32 chan, f32 a, f32 b) { __real_KPADSetPosParam(chan, a, b); }
void __wrap_KPADSetSensorHeight(s32 chan, f32 height) { __real_KPADSetSensorHeight(chan, height); }
#else
void KPADSetAccParam(s32, f32, f32) {}
void KPADSetBtnRepeat(s32, f32, f32) {}
void KPADSetDistParam(s32, f32, f32) {}
void KPADSetHoriParam(s32, f32, f32) {}
void KPADSetPosParam(s32, f32, f32) {}
void KPADSetSensorHeight(s32, f32) {}
#endif

#if defined(TARGET_WIIU)
uint32_t __wrap_KPADReadEx(s32 chan, KPADStatus* statuses, uint32_t count, s32* error)
#else
uint32_t KPADReadEx(s32 chan, KPADStatus* statuses, uint32_t count, s32* error)
#endif
{
    if (error)
        *error = KPAD_ERROR_NO_SAMPLES;
    if (!channel_valid(chan) || !statuses || !count)
        return 0;
#if defined(PORT_GAME_SMG)
    check_autoplay_timeout();
#endif
#if defined(TARGET_WIIU)
    poll_real_inputs();
#else
    poll_vpad();
#endif
    service_gamepad_motor();
#if defined(TARGET_WIIU)
    service_pro_motors();
#endif
    report_callbacks();
    if (!s_connected
#if defined(TARGET_WIIU)
        && !(chan > WPAD_CHAN0 && s_real[chan].connected)
        && !(chan == WPAD_CHAN0 && s_player1_real_channel >= 0)
#endif
    )
        return 0;
    fill_status(statuses[0], chan);
    if (error)
        *error = KPAD_ERROR_OK;
    return 1;
}

s32 KPADRead(s32 chan, KPADStatus* statuses, uint32_t count)
{
    s32 error = KPAD_ERROR_NO_SAMPLES;
#if defined(TARGET_WIIU)
    return __wrap_KPADReadEx(chan, statuses, count, &error);
#else
    return KPADReadEx(chan, statuses, count, &error);
#endif
}

WPADExtensionCallback WPADSetExtensionCallback(s32 chan, WPADExtensionCallback callback)
{
    if (!channel_valid(chan))
        return nullptr;
    WPADExtensionCallback old = s_extension_callbacks[chan];
    s_extension_callbacks[chan] = callback;
    /* Delivered by report_callbacks() on the next KPADRead, as the real
     * library does asynchronously: calling it here ran WPad::connectCallback
     * inside the WPadHolder constructor, before MR::getWPad() worked (DSI). */
    return old;
}

WPADConnectCallback WPADSetConnectCallback(s32 chan, WPADConnectCallback callback)
{
    if (!channel_valid(chan))
        return nullptr;
    WPADConnectCallback old = s_connect_callbacks[chan];
    s_connect_callbacks[chan] = callback;
    /* Delivered by report_callbacks() on the next KPADRead, as the real
     * library does asynchronously: calling it here ran WPad::connectCallback
     * inside the WPadHolder constructor, before MR::getWPad() worked (DSI). */
    return old;
}

WPADSamplingCallback WPADSetSamplingCallback(s32 chan, WPADSamplingCallback callback)
{
    if (!channel_valid(chan))
        return nullptr;
    WPADSamplingCallback old = s_sampling_callbacks[chan];
    s_sampling_callbacks[chan] = callback;
    return old;
}

s32 WPADProbe(s32 chan, u32* type)
{
#if defined(TARGET_WIIU)
    poll_real_inputs();
    if (channel_valid(chan) && s_real[chan].connected) {
        if (type)
            *type = real_dev_type(s_real[chan].extension_type);
        return WPAD_ERR_NONE;
    }
#endif
    if (chan == WPAD_CHAN0 && s_connected) {
        if (type)
            *type = WPAD_DEV_FREESTYLE;
        return WPAD_ERR_NONE;
    }
    if (type)
        *type = WPAD_DEV_NOT_FOUND;
    return WPAD_ERR_NO_CONTROLLER;
}

s32 WPADGetInfoAsync(s32 chan, WPADInfo* info, WPADCallback callback)
{
    if (info) {
        std::memset(info, 0, sizeof(*info));
        info->battery = WPAD_BATTERY_LEVEL_MAX;
        info->led = 1;
    }
    if (callback)
        callback(chan, s_connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER);
    return WPAD_ERR_NONE;
}

#if defined(TARGET_WIIU)
void __wrap_WPADControlMotor(s32 chan, u32 command)
#else
void WPADControlMotor(s32 chan, u32 command)
#endif
{
    if (!channel_valid(chan))
        return;
#if defined(TARGET_WIIU)
    if (s_real[chan].connected) {
        RealController& c = s_real[chan];
        if (c.raw.extension_type == WPAD_EXT_PRO_CONTROLLER) {
            const u32 field = sms_wiiu_frame_count();
            if (command == WPAD_MOTOR_RUMBLE) {
                c.motor_hold_until = field + kProRumbleMinFields;
                c.motor_stop_pending = false;
                if (!c.motor_on)
                    __real_WPADControlMotor(chan, command);
                c.motor_on = true;
            } else if (c.motor_on && field < c.motor_hold_until) {
                c.motor_stop_pending = true;  // service_pro_motors() stops it when the hold ends
            } else {
                __real_WPADControlMotor(chan, command);
                c.motor_on = false;
                c.motor_stop_pending = false;
            }
        } else {
            __real_WPADControlMotor(chan, command);
        }
    }
#endif
    if (chan != WPAD_CHAN0 || !gamepad_is_active())
        return;
    if (command == WPAD_MOTOR_RUMBLE) {
        if (!s_gamepad_motor)
            drive_gamepad_motor(true);
        s_gamepad_motor = true;
        s_gamepad_motor_age = 0;
    } else if (command == WPAD_MOTOR_STOP) {
        if (s_gamepad_motor)
            drive_gamepad_motor(false);
        s_gamepad_motor = false;
        s_gamepad_motor_age = 0;
    }
}

void sms_wiiu_input_stop_motors(void)
{
    stop_all_motors();
}
BOOL WPADIsSpeakerEnabled(s32 chan) { return chan == WPAD_CHAN0 && s_connected; }
s32 WPADControlSpeaker(s32 chan, u32, WPADCallback callback)
{
    if (callback)
        callback(chan, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}
u8 WPADGetSpeakerVolume(void) { return 0x60; }
BOOL WPADCanSendStreamData(s32 chan) { return chan == WPAD_CHAN0 && s_connected; }
s32 WPADSendStreamData(s32, void*, u16) { return WPAD_ERR_NONE; }
void WPADDisconnect(s32) {}
u32 WPADGetWorkMemorySize(void) { return 0x10000; }
void WPADRegisterAllocator(WPADAlloc, WPADFree) {}
void WPADSetAutoSleepTime(u8) {}
u8 WPADGetSensorBarPosition(void) { return WPAD_SENSOR_BAR_POS_TOP; }
u8 WPADGetDpdSensitivity(void) { return 0; }
void WPADGetAccGravityUnit(s32, u32, WPADAcc* acc)
{
    if (acc) {
        acc->x = 0;
        acc->y = -256;
        acc->z = 0;
    }
}
BOOL WPADStopSimpleSync(void) { return 1; }
BOOL WPADCancelSyncDevice(void) { return 1; }
BOOL WPADIsDpdEnabled(s32 chan) { return chan == WPAD_CHAN0 && s_connected; }
s32 WPADControlDpd(s32 chan, u32, WPADCallback callback)
{
    if (callback)
        callback(chan, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}
u32 WPADGetDataFormat(s32) { return s_data_format; }
s32 WPADSetDataFormat(s32, u32 format)
{
    s_data_format = format;
    return WPAD_ERR_NONE;
}
void WPADRead(s32, void*) {}
s32 WPADGetStatus(void) { return WPAD_ERR_NONE; }
void WPADSetAutoSamplingBuf(s32, void*, u32) {}
s32 WPADControlLed(s32 chan, u8, WPADCallback callback)
{
    if (callback)
        callback(chan, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}
void WPADGetAddress(s32, u8* address)
{
    if (address)
        std::memset(address, 0, 6);
}

} // extern "C"
