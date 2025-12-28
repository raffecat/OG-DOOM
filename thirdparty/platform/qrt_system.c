#include "platform.h"

#include <SDL2/SDL.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct capinfoE {
    void* buf;
    size_t size;
    int fd;
    uint32_t aud;
} capinfo;

static capinfo caps[1000] = {0};
static int next_cap = 100;

static SDL_mutex* qrt_main_mutex = 0;
static uint32_t qrt_main_thread_id = 0;

static uint32_t user_sdl_events = 0;
enum user_eventsE {
    uev_fb_frame = 0,
} user_events;

static int fb_fullscreen = 0;
static uint32_t fb_cap = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_disp_width = 0;
static uint32_t fb_disp_height = 0;
static cap_t fb_buffer = 0;
static SDL_Window* window = 0;
static SDL_Renderer* renderer = 0;
static SDL_Texture* texture = 0;
static uint32_t palette[256] = {0};

static cap_t snd_queue = 0;
static SDL_AudioDeviceID snd_device = 0;
static int snd_playing = 0;

static SDL_Event event;

static FrameBuffer_FrameEvent fb_frame;
static Input_KeyEvent key_event;
//static Input_ButtonEvent btn_event;
//static Input_PointerEvent ptr_event;
static MasqEvent gen_event;

static void masq_sdl_exit(void) {
    if (snd_device) {
        SDL_CloseAudio();
        snd_device = 0;
    }
    SDL_Quit();
}


// SYSTEM

void System_Init(void) {
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS) < 0) {
        printf("[RT] SDL_Init: %s\n", SDL_GetError());
    }
    user_sdl_events = SDL_RegisterEvents(1);
    qrt_main_mutex = SDL_CreateMutex();
    qrt_main_thread_id = SDL_ThreadID();
    atexit(masq_sdl_exit);
}

void System_DropCapability(cap_t cap) {
    if (caps[cap].fd) {
        close(caps[cap].fd);
        caps[cap].fd = 0;
    }
    if (caps[cap].aud) {
        SDL_CloseAudioDevice(caps[cap].aud);
        caps[cap].aud = 0;
    }
}

void System_OfferCapability(cap_t cap, cap_t recipient) {
}

void System_AcceptCapability(cap_t sender, size_t token, void* io_address, size_t len) {
}


// TASKS

// Queue a new task in the scheduler.
void Task_Create(int (*fn)(void* args), void* args) {
    // hacks, no scheduler yet.
    // XXX returning 'int' for SDL compatibility.
    if (!SDL_CreateThread(fn, "task", args)) {
        printf("[RT] SDL_CreateThread: %s\n", SDL_GetError());
    }
}


// MUTEXES

void Mutex_Init(mutex_t* mu) {
    mu->m = (void*) SDL_CreateMutex();
    if (mu->m == NULL) {
        printf("fatal: SDL_CreateMutex failed: %s\n", SDL_GetError());
        exit(1);
    }
}

void Mutex_Lock(mutex_t* mu) {
    if (SDL_LockMutex((SDL_mutex*)(mu->m)) < 0) {
        printf("fatal: SDL_LockMutex failed: %s\n", SDL_GetError());
        exit(1);
    }
}

void Mutex_Unlock(mutex_t* mu) {
    if (SDL_UnlockMutex((SDL_mutex*)(mu->m)) < 0) {
        printf("fatal: SDL_UnlockMutex failed: %s\n", SDL_GetError());
        exit(1);
    }
}


// ATOMICS

// Yeah they're just wrapped, not sure how atomics will play out yet.

void Atomic_Set_Int(Atomic_Int* var, int value) {
        // "This function also acts as a full memory barrier"
        SDL_AtomicSet((SDL_atomic_t*)var, value);
}
int Atomic_Get_Int(Atomic_Int* var) {
        return SDL_AtomicGet((SDL_atomic_t*)var);
}
int Atomic_CAS_Int(Atomic_Int* var, int old_val, int new_val) {
        return SDL_AtomicCAS((SDL_atomic_t*)var, old_val, new_val);
}

void Atomic_Set_Ptr(Atomic_Ptr* var, void* ptr) {
        // "This function also acts as a full memory barrier"
        SDL_AtomicSetPtr(&var->ptr, ptr);
}
void Atomic_Set_Ptr_Release(Atomic_Ptr* var, void* ptr) {
        // "insert a release barrier between writing the data and the flag"
        SDL_MemoryBarrierReleaseFunction();
        // (makes no claim about barriers:)
        SDL_AtomicSetPtr(&var->ptr, ptr);
}
void* Atomic_Get_Ptr(Atomic_Ptr* var) {
        return SDL_AtomicGetPtr(&var->ptr);
}
void* Atomic_Get_Ptr_Acquire(Atomic_Ptr* var) {
        void* p = SDL_AtomicGetPtr(&var->ptr);
        // (makes no claim about barriers:)
        SDL_MemoryBarrierAcquireFunction();
        return p;
}
int Atomic_CAS_Ptr(Atomic_Ptr* var, void* old_ptr, void* new_ptr) {
        return SDL_AtomicCASPtr(&var->ptr, old_ptr, new_ptr);
}



// BUFFERS

void* Buffer_Create(cap_t cap, size_t size, cap_t io_cap) {
    caps[cap].buf = malloc(size);
    caps[cap].size = size;
    return caps[cap].buf;
}

void* Buffer_Address(cap_t cap) {
    return caps[cap].buf;
}

size_t Buffer_Size(cap_t cap) {
    return caps[cap].size;
}

void* Buffer_CreateShared(cap_t cap, size_t size_pg) {
    return Buffer_Create(cap, size_pg<<12, 0);
}

void Buffer_MapShared(cap_t sb_cap, size_t io_area_ofs, cap_t io_cap) {
}

void Buffer_Destroy(cap_t cap) {
    if (caps[cap].buf) {
        free(caps[cap].buf);
        caps[cap].buf = 0;
        caps[cap].size = 0;
    }
}


// QUEUES

typedef struct qrt_queue_hdrS {
    SDL_mutex* mutex;    // queue lock (ugh)
    uint32_t read;       // read pointer within queue area.
    uint32_t write;      // write pointer within queue area.
    uint32_t size_mask;  // size bitmask (power of two, minus 1)
} qrt_queue_hdr;

void Queue_New(cap_t cap, size_t io_area_ofs, uint32_t size_pow2) {
    if (size_pow2 < 12) size_pow2 = 12; // minimum 4096
    qrt_queue_hdr* q = Buffer_Create(cap, sizeof(qrt_queue_hdr) + (1 << size_pow2), 0);
    q->mutex = SDL_CreateMutex();
    q->read = 0;
    q->write = 0;
    q->size_mask = (1 << size_pow2)-1;
    return;
}

void Queue_Wait(cap_t q_cap) {
    SDL_WaitEvent(NULL);
}

static MasqEvent no_event = {{-1,0,0}};

static uint16_t hid_mods(uint16_t mod) {
    // SDL has the same idea, but a different mapping.
    uint16_t mods = 0;
    if (mod & KMOD_LSHIFT) mods |= MasqKeyModifierLShift;
    if (mod & KMOD_LCTRL) mods |= MasqKeyModifierLCtrl;
    if (mod & KMOD_LALT) mods |= MasqKeyModifierLAlt;
    if (mod & KMOD_LGUI) mods |= MasqKeyModifierLMeta;
    if (mod & KMOD_RSHIFT) mods |= MasqKeyModifierRShift;
    if (mod & KMOD_RCTRL) mods |= MasqKeyModifierRCtrl;
    if (mod & KMOD_RALT) mods |= MasqKeyModifierRAlt;
    if (mod & KMOD_RGUI) mods |= MasqKeyModifierRMeta;
    if (mod & KMOD_CAPS) mods |= MasqKeyModifierCapsLock;
    if (mod & KMOD_CAPS) mods |= MasqKeyModifierCapsLock;
    return mods;
}

MasqEventHeader* Queue_Read(cap_t q_cap) {
    // Pump SDL events.
    if (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT: {
                gen_event.h.cap = System_Cap;
                gen_event.h.event = System_Quit;
                gen_event.h.size = sizeof(MasqEvent);
                return &gen_event.h;
            }
            case SDL_KEYDOWN: {
                key_event.h.cap = 4; // ddev_input
                key_event.h.event = Input_KeyDown;
                key_event.h.size = sizeof(Input_KeyEvent);
                key_event.keycode = event.key.keysym.scancode; // USB usage (same as Input_KeyCode)
                key_event.modifiers = hid_mods(event.key.keysym.mod);
                return &key_event.h;
            }
            case SDL_KEYUP: {
                key_event.h.cap = 4; // ddev_input
                key_event.h.event = Input_KeyUp;
                key_event.h.size = sizeof(Input_KeyEvent);
                key_event.keycode = event.key.keysym.scancode; // USB usage (same as Input_KeyCode)
                key_event.modifiers = hid_mods(event.key.keysym.mod);
                return &key_event.h;
            }
            case SDL_MOUSEMOTION: {

            }
            case SDL_MOUSEBUTTONDOWN: {
            }
            case SDL_MOUSEBUTTONUP: {

            }
            case SDL_MOUSEWHEEL: {

            }
            case SDL_WINDOWEVENT_FOCUS_GAINED: {
                // SDL_SetRelativeMouseMode(SDL_TRUE);
            }
            case SDL_WINDOWEVENT_FOCUS_LOST: {
                // SDL_SetRelativeMouseMode(SDL_FALSE);
            }
            default: {
                if (event.type == user_sdl_events + uev_fb_frame) {
                    fb_frame.h.cap = fb_cap;
                    fb_frame.h.event = FrameBuffer_Frame;
                    fb_frame.h.size = sizeof(FrameBuffer_FrameEvent);
                    fb_frame.buf_cap = (size_t) event.user.data1;
                    fb_frame.dt_ms = 1;
                    return &fb_frame.h;
                }
            }
        }
    }
    return &no_event.h;
}

void Queue_Advance(cap_t q_cap) {
}

int Queue_Empty(cap_t q_cap) {
    return !(SDL_PollEvent(NULL));
}


// STORAGE

int Storage_ObjectExists(const char* name) {
    if (access(name, 0) != -1) return 1;
    return 0;
}

cap_t Storage_FindObject(const char* name) {
    int fd = open(name, O_RDONLY, 0);
    if (fd == -1) return 0;
    off_t size = lseek(fd, 0, SEEK_END);
    cap_t handle = next_cap++;
    caps[handle].buf = 0;
    caps[handle].size = (size_t) size;
    caps[handle].fd = fd;
    return handle;
}

size_t Storage_ObjectSize(cap_t handle) {
    return caps[handle].size;
}

int Storage_CopyToMemory(cap_t handle, void* address, size_t ofs, size_t len) {
    ssize_t n;
    char* to = address;
    lseek(caps[handle].fd, (off_t)ofs, SEEK_SET);
    do {
        n = read(caps[handle].fd, to, len);
        if (n < 1) return -1; // early EOF or error reading
        len -= n;
        to += n;
    } while (len>0);
    return 0;
}

int Storage_CreateObject(const char* name, cap_t buf_cap, size_t size) {
    return -1;
}

int Storage_DeleteObject(const char* name) {
    return -1;
}


// FRAMEBUFFER

#define FB_SCALE 3

void FrameBuffer_Create(cap_t cap, FrameBuffer_Opts opts, size_t width, size_t height, size_t bpp, cap_t queue) {
    fb_cap = cap;
    fb_width = width;
    fb_height = height;
    fb_disp_width = width * FB_SCALE;
    fb_disp_height = height * FB_SCALE;
    window = SDL_CreateWindow(
        "Framebuffer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        fb_disp_width, (int)(fb_disp_height * 1.2),
        SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        printf("[RT] SDL_CreateWindow: %s\n", SDL_GetError());
        return;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC); // SDL_RENDERER_SOFTWARE
    if (!renderer) return;
    // if (!SDL_RenderSetLogicalSize(renderer, width, height)) return 0;
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        fb_disp_width, fb_disp_height
    );
    if (!texture) {
        printf("[RT] SDL_CreateTexture: %s\n", SDL_GetError());
        return;
    }
    // allocate framebuffer storage buffer.
    fb_buffer = next_cap++;
    size_t sz = fb_width * fb_height;
    Buffer_Create(fb_buffer, sz, 0);
    // send one Frame event.
    SDL_Event frame_event = {0};
    frame_event.user.type = user_sdl_events+uev_fb_frame;
    frame_event.user.data1 = (void*) fb_buffer;
    SDL_PushEvent(&frame_event); // thread-safe
}

void FrameBuffer_Configure(cap_t fb_cap, FrameBuffer_Opts opts, size_t width, size_t height, size_t bpp, cap_t queue_cap) {
        if (opts & FrameBuffer_Fullscreen) {
                if (!fb_fullscreen) {
                        fb_fullscreen = 1;
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
        } else {
                if (fb_fullscreen) {
                        fb_fullscreen = 0;
                        SDL_SetWindowFullscreen(window, 0);
                }
        }
}

void FrameBuffer_SetTitle(cap_t fb_cap, const char* title) {
        SDL_SetWindowTitle(window, title);
}

void FrameBuffer_SetFullscreen(cap_t fb_cap, int fullscreen) {
        if (fb_fullscreen != !!fullscreen) {
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                fb_fullscreen = !!fullscreen;
        }
}

void FrameBuffer_SetPalette(cap_t fb_cap, cap_t buf_cap) {
    uint32_t* pal = caps[buf_cap].buf;
    if (caps[buf_cap].size == 256*4) {
        memcpy(palette, pal, 256*4);
    }
}

void FrameBuffer_Submit(cap_t fb_cap, cap_t buf_cap) {
    void* pixels;
    int pitch;
    if (SDL_LockTexture(texture, NULL, &pixels, &pitch) != 0) {
        printf("[RT] SDL_LockTexture: %s\n", SDL_GetError());
        return;
    }
    // fill the texture (perform palette mapping)
    uint8_t* src_buf = caps[buf_cap].buf; // submitted buffer
    uint32_t row_ofs = 0, col_ofs = 0, one_step = 65536/FB_SCALE;
    if (!src_buf) return;
    char* dst_row = pixels; // pitch is in bytes
    for (int y=0; y<fb_disp_height; y++) {
        // first WINDOW row
        uint32_t* to = (uint32_t*)dst_row;
        uint8_t* from = src_buf + (row_ofs>>16) * fb_width;
        for (int x=0; x<fb_disp_width; x++) {
            *to++ = palette[from[col_ofs>>16]];
            col_ofs += one_step;
        }
        dst_row += pitch;
        row_ofs += one_step;
        col_ofs = 0;
    }
    // display the frame.
    SDL_UnlockTexture(texture);
    if (SDL_RenderClear(renderer) < 0) {
        printf("[RT] SDL_RenderClear: %s\n", SDL_GetError());
    }
    if (SDL_RenderCopy(renderer, texture, NULL, NULL) < 0) {
        printf("[RT] SDL_RenderCopy: %s\n", SDL_GetError());
    }
    SDL_RenderPresent(renderer); // main-thread only!
    // send a new frame event.
    SDL_Event frame_event = {0};
    frame_event.user.type = user_sdl_events+uev_fb_frame;
    frame_event.user.data1 = (void*) fb_buffer;
    SDL_PushEvent(&frame_event); // thread-safe
}


// AUDIO

// According to some libsdl forum posts, SDL_OpenAudioDevice has an undocumented
// limitation: you can only have one callback attached to each device; in other
// words, you only get one audio stream per device.

void Audio_Create(cap_t au_cap, cap_t s_queue, Audio_Opts opts, size_t channels, size_t sample_rate, size_t samples_per_chunk) {
    snd_queue = s_queue;
    snd_playing = 0;
    // static SDL_AudioStream* snd_stream = 0;
    // snd_stream = SDL_NewAudioStream(AUDIO_S16, channels, sample_rate, );
    SDL_AudioSpec spec = {0};
    // SDL_AudioSpec obtained = {0};
    spec.freq = sample_rate;
    spec.format = AUDIO_S16;
    spec.channels = channels;
    // "This number should be a power of two":
    // measured in sample-frames (groups of samples for all channels)
    spec.samples = samples_per_chunk; // XXX doom passes 512, but we should send back a msg with actual size?
    if (SDL_OpenAudio(&spec, NULL) < 0) {
        printf("[RT] SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return;
    }
    snd_device = 1; // SDL_OpenAudio always sets up device 1.
}

void Audio_Submit(cap_t au_cap, cap_t buf_cap) {
    // XXX push model: queue more audio whenever DOOM supplies it.
    // XXX will move to a timer + SDL_GetQueuedAudioSize later, on a different task?
    // this function copies the data!
    if (snd_device) {
        if (SDL_QueueAudio(snd_device, Buffer_Address(buf_cap), Buffer_Size(buf_cap)) < 0) {
            printf("[RT] SDL_QueueAudio: %s\n", SDL_GetError());
        }
        if (!snd_playing) {
            SDL_PauseAudioDevice(snd_device, 0);
            snd_playing = 1;
        }
    }
}

void Audio_CreateStream(cap_t au_cap, Audio_StreamCallback callback, Audio_Opts opts, size_t channels, size_t sample_rate, size_t samples_per_chunk) {
    SDL_AudioSpec spec = {0};
    SDL_AudioSpec obtained = {0};
    spec.freq = sample_rate;
    spec.format = AUDIO_S16;
    spec.channels = channels;
    // "This number should be a power of two":
    // measured in sample-frames (groups of samples for all channels)
    spec.samples = samples_per_chunk;
    spec.callback = callback;
    uint32_t device = SDL_OpenAudioDevice(NULL, 0, &spec, &obtained, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (device <= 0) {
        printf("[RT] SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return;
    }
    caps[au_cap].aud = device;
    caps[au_cap].size = obtained.samples;
}

size_t Audio_FrameCount(cap_t au_cap) {
    if (caps[au_cap].aud != 0) {
        return caps[au_cap].size;
    }
    return 0;
}

void Audio_Start(cap_t au_cap) {
    // start pull-mode audio playback.
    if (caps[au_cap].aud != 0) {
        SDL_PauseAudioDevice(caps[au_cap].aud, 0);
    }
}

void Audio_Stop(cap_t au_cap) {
    // stop pull-mode audio playback.
    if (caps[au_cap].aud != 0) {
        SDL_PauseAudioDevice(caps[au_cap].aud, 1);
    }
}



// INPUT

void Input_Subscribe(cap_t i_cap, Input_Opts opts, cap_t queue_cap) {
}
