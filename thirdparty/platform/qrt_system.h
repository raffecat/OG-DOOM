#pragma once

#include <stdint.h>
#include <stddef.h>


typedef enum System_EventE {
    System_Cap = 0,
    System_None = 0,
    System_Quit = 1,
} System_Event;


// Nominates a capability known to this process/context.
// Capabilities are local names for devices and buffers.
typedef size_t cap_t;
typedef size_t cap_token_t;


// SYSTEM

void System_Init(void);

void System_DropCapability(cap_t cap); // SYSCALL

// Offers are allocated in the sender (token slots)
void System_OfferCapability(cap_t cap, cap_t recipient); // SYSCALL
void System_AcceptCapability(cap_t sender, size_t token, void* io_address, size_t len); // SYSCALL


// TASKS

void Task_Create(int (*fn)(void* args), void* args);


// MUTEXES

typedef struct mutex_s {
	void* m;
} mutex_t;

void Mutex_Init(mutex_t* mu);
void Mutex_Lock(mutex_t* mu);
void Mutex_Unlock(mutex_t* mu);


// ATOMICS

typedef struct Atomic_Int_S { int value; } Atomic_Int;
void Atomic_Set_Int(Atomic_Int* var, int value);
int Atomic_Get_Int(Atomic_Int* var);
int Atomic_CAS_Int(Atomic_Int* var, int old_val, int new_val);

typedef struct Atomic_Ptr_S { void* ptr; } Atomic_Ptr;
void Atomic_Set_Ptr(Atomic_Ptr* var, void* ptr);
void Atomic_Set_Ptr_Release(Atomic_Ptr* var, void* ptr);
void* Atomic_Get_Ptr(Atomic_Ptr* var);
void* Atomic_Get_Ptr_Acquire(Atomic_Ptr* var);

// Returns 1 if old_ptr matched and the swap was performed, 0 otherwise.
int Atomic_CAS_Ptr(Atomic_Ptr* var, void* old_ptr, void* new_ptr);


// BUFFER [P]

// Uses placement capabilities, i.e. you assign them.
// Good for resources known at compile-time.
// Not so good for (unbounded) dynamic allocations.

void* Buffer_Create(cap_t cap, size_t size, cap_t io_cap); // SYSCALL
void* Buffer_Address(cap_t cap); // in memory
size_t Buffer_Size(cap_t cap); // in memory

void* Buffer_CreateShared(cap_t sb_cap, size_t size_pg); // SYSCALL
void Buffer_MapShared(cap_t sb_cap, size_t io_area_ofs, cap_t io_cap); // SYSCALL

void Buffer_Destroy(cap_t cap);


// QUEUES [P]

typedef struct MasqEventHeaderE {
    uint32_t cap;
    uint16_t size;
    uint16_t event;
} MasqEventHeader;

typedef struct MasqEventE {
    MasqEventHeader h;
} MasqEvent;

void Queue_New(cap_t q_cap, size_t io_area_ofs, uint32_t size_pow2);
void Queue_Wait(cap_t q_cap);
MasqEventHeader* Queue_Read(cap_t q_cap);
void Queue_Advance(cap_t q_cap);
int Queue_Empty(cap_t q_cap);
