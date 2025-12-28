#pragma once

#include "qrt_system.h"


// STORAGE [A]

int Storage_ObjectExists(const char* name); // BLOCKING
cap_t Storage_FindObject(const char* name); // BLOCKING, allocates a new capability (means App must reserve its cap slots)
size_t Storage_ObjectSize(cap_t handle); // BLOCKING
int Storage_CopyToMemory (cap_t handle, void* address, size_t ofs, size_t len); // BLOCKING
int Storage_CreateObject(const char* name, cap_t buf_cap, size_t size); // BLOCKING
int Storage_DeleteObject(const char* name); // BLOCKING


// FRAMEBUFFER [P]

typedef enum FrameBuffer_OptsE {
    FrameBuffer_DoubleBuffer = 1,  // receive Frame events; Submit rendered frames (otherwise one Frame event)
    FrameBuffer_Palette      = 2,  // require a 256-color palette (host must simulate if no HW)
    FrameBuffer_SendSync     = 4,  // require Sync events at end of display frame
    FrameBuffer_DynamicSize  = 8,  // host can resize the framebuffer (e.g. in a window; single-buffer sends a new Frame)
    FrameBuffer_NoScaleUp    = 16, // avoid scaling up the content (and create window at the requested size)
    FrameBuffer_NoSmooth     = 32, // use nearest-neighbour scaling or similar; prefer integer size multiples
    FrameBuffer_Fullscreen   = 64, // set this to make the framebuffer fullscreen
} FrameBuffer_Opts;

typedef enum FrameBuffer_EventE {
    FrameBuffer_Size,    // sent when the framebuffer size changes
    FrameBuffer_Frame,   // sent to request the next frame for display (once if single-buffered, and on Size change)
    FrameBuffer_Sync,    // sent to notify end of display frame
    FrameBuffer_Dirty,   // sent to notify the current display is damaged (useful if single-buffered)
} FrameBuffer_Event;

typedef struct FrameBuffer_SizeEventE {
    MasqEventHeader h;
    uint16_t width;
    uint16_t height;
} FrameBuffer_SizeEvent;

typedef struct FrameBuffer_FrameEventE {
    MasqEventHeader h;
    size_t dt_ms;
    // Buffer for the next frame.
    // TRANSFER from FrameBuffer device to this App (do we need to accept and map?)
    // That isn't ideal; it would involve another syscall just after queue read.
    cap_t buf_cap;
} FrameBuffer_FrameEvent;

typedef struct FrameBuffer_SyncEventE {
    MasqEventHeader h;
    size_t dt_ms;
} FrameBuffer_SyncEvent;

// The display can be Created again to change configuration; should be a seamless transition.
// Palette changes may apply immediately, or may apply on the next frame submission (if double-buffered)

// Without DynamicSize:
// The host must honour the requested size, scaling and/or filtering as necessary.
// It is reasonable to add borders to avoid small scale amounts, e.g. 1.1x scale.
// The host should aim to cover 'most of' the display with the framebuffer.
// The host should allow the user to override the chosen scaling/filtering settings,
// and should save those settings for future runs if possible.
// In a windowed environment, the host should create a window with the requested
// client area size, or some multiple that fits comfortably on-screen, to avoid
// creating a tiny window for small content.
// The host should provide a fullscreen toggle.

// With DynamicSize:
// The host is permitted to choose the framebuffer size.
// In a windowing environment, DynamicSize creates a resizeable window at the requested size,
// or some multiple that fits comfortably on-screen. Without DynamicSize, the window instead
// behaves like a fullscreen presentation inside a window, using scaling/filtering and adding
// borders as necessary.
// In a non-desktop environment, the host should create a fullscreen presentation
// at or above the requested size (if possible)
// Display mode selection should be on the basis of the requested features, e.g. palette,
// double-buffer vs available VRAM, native resolutions supported by the hardware; it is
// better to choose a lower native resolution that fully contains the content, unless
// filter effects require a higher resolution multiple.

void FrameBuffer_Create(cap_t fb_cap, FrameBuffer_Opts opts, size_t width, size_t height, size_t bpp, cap_t queue_cap);
void FrameBuffer_Configure(cap_t fb_cap, FrameBuffer_Opts opts, size_t width, size_t height, size_t bpp, cap_t queue_cap);
void FrameBuffer_SetTitle(cap_t fb_cap, const char* title);
void FrameBuffer_SetFullscreen(cap_t fb_cap, int fullscreen);
void FrameBuffer_SetPalette(cap_t fb_cap, cap_t buf_cap); // XXX transfer or share buffer?
void FrameBuffer_Submit(cap_t fb_cap, cap_t buf_cap); // TRANSFER buffer from Video 'Frame' event


// AUDIO

typedef enum Audio_OptsE {
    Audio_None,
    Audio_NoFmtConversion,
    Audio_Fmt_U8,
    Audio_Fmt_S16, // LE
    Audio_Fmt_S24,
    Audio_Fmt_S32,
} Audio_Opts;

// AC97: mono, stereo, 16-bit
// AC97: 8, 11.025, 16, 22.05, 32, 44.1, 48 kHz
// Optional: 18-bit, 20-bit, 4/6 channel, 5.1 S/PDIF

typedef struct Audio_FrameEventE {
    MasqEventHeader h;
    size_t dt_ms;
    // Buffer for the next Audio frame.
    // TRANSFER from Audio device to this App (do we need to accept and map?)
    // That isn't ideal; it would involve another syscall just after queue read.
    cap_t buf_cap;
} Audio_FrameEvent;

// Push Mode
void Audio_Create(cap_t au_cap, cap_t s_queue, Audio_Opts opts, size_t channels, size_t sample_rate, size_t samples_per_frame);
void Audio_Submit(cap_t au_cap, cap_t buf_cap); // TRANSFER buffer from Audio 'Frame' event

// Pull Mode
typedef void(*Audio_StreamCallback)(void* userdata, uint8_t* buffer, int size_in_bytes);
void Audio_CreateStream(cap_t au_cap, Audio_StreamCallback callback, Audio_Opts opts, size_t channels, size_t sample_rate, size_t samples_per_chunk);
size_t Audio_FrameCount(cap_t au_cap);
void Audio_Start(cap_t au_cap);
void Audio_Stop(cap_t au_cap);


// INPUT

typedef enum Input_EventE {
    Input_None = 0,
    Input_KeyDown = 1,
    Input_KeyUp = 2,
    Input_ButtonDown = 3,
    Input_ButtonUp = 4,
    Input_PointerMove = 5,
    Input_Wheel = 6,
    Input_TouchPan = 7,
    Input_TouchZoom = 8,
    Input_TouchRotate = 9,
    Input_TouchBegin = 10,
    Input_TouchMove = 11,
    Input_TouchEnd = 12,
} Input_Event;

typedef enum Input_OptsE {
    InputOpt_Key = 1,
    InputOpt_Button = 2,
    InputOpt_Pointer = 4,
    InputOpt_Wheel = 8,
    InputOpt_Touch = 16,
    InputOpt_TouchPoints = 32,
    InputOpt_Joystick = 64
} Input_Opts;

// How is input associated with a visual? (window, framebuffer)

void Input_Subscribe(cap_t i_cap, Input_Opts opts, cap_t queue_cap);


// MOUSE

typedef enum Input_ButtonStateE {
    Input_ButtonLeft   = 1,
    Input_ButtonRight  = 2,
    Input_ButtonMiddle = 4,
    Input_Button4      = 8,
    Input_Button5      = 16,
    Input_Button6      = 32,
    Input_Button7      = 64,
    Input_Button8      = 128,
    SIZE_Input_ButtonState = 0xFFFF
} Input_ButtonState;

typedef struct Input_PointerEventE {
    MasqEventHeader h;
    uint16_t device;
    uint16_t buttons;  // Input_ButtonState
    int32_t x;
    int32_t y;
} Input_PointerEvent;

typedef struct Input_TouchEventE {
    MasqEventHeader h;
    uint16_t device;
    uint16_t touch;
    int32_t x;
    int32_t y;
} Input_TouchEvent;


// KEYBOARD

typedef struct Input_KeyEventE {
    MasqEventHeader h;
    uint16_t keycode;
    uint16_t modifiers;  // Input_KeyModifiers
} Input_KeyEvent;

// Modifier bits as per USB HID report (modifier byte)
typedef enum Input_KeyModifiersE {
    MasqKeyModifierLCtrl    = 1,
    MasqKeyModifierLShift   = 2,
    MasqKeyModifierLAlt     = 4,
    MasqKeyModifierLMeta    = 8,
    MasqKeyModifierRCtrl    = 16,
    MasqKeyModifierRShift   = 32,
    MasqKeyModifierRAlt     = 64,
    MasqKeyModifierRMeta    = 128,
    // Extended modifier bits:
    MasqKeyModifierNumLock  = 256,
    MasqKeyModifierCapsLock = 512,
    MasqKeyModifierScrollLock = 1024,
    MasqKeyModifierCompose = 2048,
    MasqKeyModifierKana = 4096,
    SIZE_Input_KeyModifiers = 0xFFFF
} Input_KeyModifiers;

// Key Codes as per USB HID usage page 7.
typedef enum Input_KeyCodeE {
    MasqKey_None                    = 0,
    MasqKey_KeyboardConflict        = 1,
    MasqKey_KeyboardError           = 3,
    MasqKey_A                       = 4,
    MasqKey_B                       = 5,
    MasqKey_C                       = 6,
    MasqKey_D                       = 7,
    MasqKey_E                       = 8,
    MasqKey_F                       = 9,
    MasqKey_G                       = 10,
    MasqKey_H                       = 11,
    MasqKey_I                       = 12,
    MasqKey_J                       = 13,
    MasqKey_K                       = 14,
    MasqKey_L                       = 15,
    MasqKey_M                       = 16,
    MasqKey_N                       = 17,
    MasqKey_O                       = 18,
    MasqKey_P                       = 19,
    MasqKey_Q                       = 20,
    MasqKey_R                       = 21,
    MasqKey_S                       = 22,
    MasqKey_T                       = 23,
    MasqKey_U                       = 24,
    MasqKey_V                       = 25,
    MasqKey_W                       = 26,
    MasqKey_X                       = 27,
    MasqKey_Y                       = 28,
    MasqKey_Z                       = 29,
    MasqKey_1                       = 30,
    MasqKey_2                       = 31,
    MasqKey_3                       = 32,
    MasqKey_4                       = 33,
    MasqKey_5                       = 34,
    MasqKey_6                       = 35,
    MasqKey_7                       = 36,
    MasqKey_8                       = 37,
    MasqKey_9                       = 38,
    MasqKey_0                       = 39,
    MasqKey_Return                  = 40,
    MasqKey_Escape                  = 41,
    MasqKey_Backspace               = 42,
    MasqKey_Tab                     = 43,
    MasqKey_Space                   = 44,
    MasqKey_Minus                   = 45,
    MasqKey_Equal                   = 46,
    MasqKey_LeftBracket             = 47,
    MasqKey_RightBracket            = 48,
    MasqKey_Backslash               = 49, // \| or #~ or $£ or #' or `£ or *µ
    MasqKey_NonUSHash               = 50, // mapped to MasqKey_Backslash
    MasqKey_Semi                    = 51,
    MasqKey_Quote                   = 52,
    MasqKey_Grave                   = 53, // `~ or `¬ or §± or §° or ^° or @# or <>
    MasqKey_Comma                   = 54,
    MasqKey_Dot                     = 55,
    MasqKey_Slash                   = 56,
    MasqKey_CapsLock                = 57,
    MasqKey_F1                      = 58,
    MasqKey_F2                      = 59,
    MasqKey_F3                      = 60,
    MasqKey_F4                      = 61,
    MasqKey_F5                      = 62,
    MasqKey_F6                      = 63,
    MasqKey_F7                      = 64,
    MasqKey_F8                      = 65,
    MasqKey_F9                      = 66,
    MasqKey_F10                     = 67,
    MasqKey_F11                     = 68,
    MasqKey_F12                     = 69,
    MasqKey_PrintScreen             = 70,
    MasqKey_ScrollLock              = 71,
    MasqKey_Pause                   = 72,
    MasqKey_Insert                  = 73, // help on some mac keyboards (relabelled)
    MasqKey_Home                    = 74,
    MasqKey_PageUp                  = 75,
    MasqKey_Delete                  = 76,
    MasqKey_End                     = 77,
    MasqKey_PageDown                = 78,
    MasqKey_RightArrow              = 79,
    MasqKey_LeftArrow               = 80,
    MasqKey_DownArrow               = 81,
    MasqKey_UpArrow                 = 82,
    MasqKey_NumLockClear            = 83, // numlock on pc, clear on mac
    MasqKey_KeypadSlash             = 84,
    MasqKey_KeypadStar              = 85,
    MasqKey_KeypadMinus             = 86,
    MasqKey_KeypadPlus              = 87,
    MasqKey_KeypadEnter             = 88,
    MasqKey_Keypad1                 = 89,
    MasqKey_Keypad2                 = 90,
    MasqKey_Keypad3                 = 91,
    MasqKey_Keypad4                 = 92,
    MasqKey_Keypad5                 = 93,
    MasqKey_Keypad6                 = 94,
    MasqKey_Keypad7                 = 95,
    MasqKey_Keypad8                 = 96,
    MasqKey_Keypad9                 = 97,
    MasqKey_Keypad0                 = 98,
    MasqKey_KeypadDot               = 99,
    MasqKey_NonUSBackslash          = 100, // ISO-only `~ or \| or <>
    MasqKey_Application             = 101, // windows menu-key, compose
    MasqKey_Power                   = 102, // mac power key?
    MasqKey_KeypadEqual             = 103,
    MasqKey_F13                     = 104,
    MasqKey_F14                     = 105,
    MasqKey_F15                     = 106,
    MasqKey_F16                     = 107,
    MasqKey_F17                     = 108,
    MasqKey_F18                     = 109,
    MasqKey_F19                     = 110,
    MasqKey_F20                     = 111,
    MasqKey_F21                     = 112,
    MasqKey_F22                     = 113,
    MasqKey_F23                     = 114,
    MasqKey_F24                     = 115,
    MasqKey_Execute                 = 116,
    MasqKey_Help                    = 117,
    MasqKey_Menu                    = 118,
    MasqKey_Select                  = 119,
    MasqKey_Stop                    = 120,
    MasqKey_Again                   = 121, // Redo/Repeat
    MasqKey_Undo                    = 122,
    MasqKey_Cut                     = 123,
    MasqKey_Copy                    = 124,
    MasqKey_Paste                   = 125,
    MasqKey_Find                    = 126,
    MasqKey_Mute                    = 127,
    MasqKey_VolumeUp                = 128,
    MasqKey_VolumeDown              = 129,
    MasqKey_LockingCapsLock         = 130,
    MasqKey_LockingNumLock          = 131,
    MasqKey_LockingScrollLock       = 132,
    MasqKey_KeypadComma             = 133,
    MasqKey_KeypadEqualSign         = 134,
    MasqKey_International1          = 135,
    MasqKey_International2          = 136,
    MasqKey_International3          = 137,
    MasqKey_International4          = 138,
    MasqKey_International5          = 139,
    MasqKey_International6          = 140,
    MasqKey_International7          = 141,
    MasqKey_International8          = 142,
    MasqKey_International9          = 143,
    MasqKey_LANG1                   = 144, // Hangul
    MasqKey_LANG2                   = 145,
    MasqKey_LANG3                   = 146,
    MasqKey_LANG4                   = 147,
    MasqKey_LANG5                   = 148,
    MasqKey_LANG6                   = 149,
    MasqKey_LANG7                   = 150,
    MasqKey_LANG8                   = 151,
    MasqKey_LANG9                   = 152,
    MasqKey_AltErase                = 153,
    MasqKey_SysReq                  = 154,
    MasqKey_Cancel                  = 155,
    MasqKey_Clear                   = 156,
    MasqKey_Prior                   = 157,
    MasqKey_Return2                 = 158,
    MasqKey_Separator               = 159,
    MasqKey_Out                     = 160,
    MasqKey_Oper                    = 161,
    MasqKey_Clear_Again             = 162,
    MasqKey_CrSel_Props             = 163,
    MasqKey_ExSel                   = 164,
    // 165-175 Reserved
    MasqKey_Keypad00                = 176,
    MasqKey_Keypad000               = 177,
    MasqKey_ThousandsSeparator      = 178,
    MasqKey_DecimalSeparator        = 179,

    MasqKey_CurrencyUnit            = 180,
    MasqKey_CurrencySubUnit         = 181,
    MasqKey_KeypadLeftParen         = 182,
    MasqKey_KeypadRightParen        = 183,
    MasqKey_KeypadLeftBrace         = 184,
    MasqKey_KeypadRightBrace        = 185,
    MasqKey_KeypadTab               = 186,
    MasqKey_KeypadBackspace         = 187,
    MasqKey_KeypadA                 = 188,
    MasqKey_KeypadB                 = 189,
    MasqKey_KeypadC                 = 190,
    MasqKey_KeypadD                 = 191,
    MasqKey_KeypadE                 = 192,
    MasqKey_KeypadF                 = 193,
    MasqKey_KeypadXOR               = 194,
    MasqKey_KeypadCaret             = 195,
    MasqKey_KeypadPercent           = 196,
    MasqKey_KeypadLeftAngleBracket  = 197,
    MasqKey_KeypadRightAngleBracket = 198,
    MasqKey_KeypadAND               = 199,
    MasqKey_KeypadDoubleAND         = 200,
    MasqKey_KeypadOR                = 201,
    MasqKey_KeypadDoubleOR          = 202,
    MasqKey_KeypadColon             = 203,
    MasqKey_KeypadHash              = 204,
    MasqKey_KeypadSpace             = 205,
    MasqKey_KeypadAt                = 206,
    MasqKey_KeypadPling             = 207,
    MasqKey_KeypadMemStore          = 208,
    MasqKey_KeypadMemRecall         = 209,
    MasqKey_KeypadMemClear          = 210,
    MasqKey_KeypadMemAdd            = 211,
    MasqKey_KeypadMemSubtract       = 212,
    MasqKey_KeypadMemMultiply       = 213,
    MasqKey_KeypadMemDivide         = 214,
    MasqKey_KeypadPlusMinus         = 215,
    MasqKey_KeypadClear             = 216,
    MasqKey_KeypadClearEntry        = 217,
    MasqKey_KeypadBinary            = 218,
    MasqKey_KeypadOctal             = 219,
    MasqKey_KeypadDecmial           = 220,
    MasqKey_KeypadHexadecimal       = 221,
    // 222-223 Reserved
    MasqKey_LeftCtrl                = 224,
    MasqKey_LeftShift               = 225,
    MasqKey_LeftAlt                 = 226,
    MasqKey_LeftMeta                = 227,
    MasqKey_RightCtrl               = 228,
    MasqKey_RightShift              = 229,
    MasqKey_RightAlt                = 230,
    MasqKey_RightMeta               = 231,
    // 232-65535 Reserved    
} Input_KeyCode;

// https://www.scs.stanford.edu/10wi-cs140/pintos/specs/kbd/scancodes.html#toc7

// Call I_Quit on CTRL+C or "Quit" message.
