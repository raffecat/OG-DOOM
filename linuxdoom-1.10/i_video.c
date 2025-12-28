// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char __attribute__((unused))
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <errno.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include "doomdef.h"
#include "i_device.h"

// XXX needs a spec, reinterpreted as uint32
typedef struct { byte blue, green, red, x; } RGB;

size_t  VFrameCap;
size_t  VPalCap;
RGB     *VPalBuf;


//
//  Translates the key currently in X_event
//

int xlatekey(int code, int mod, int down)
{

    int rc;
    static int fullscreen = 0;

    switch(code & 0xFFF)
    {
	case MasqKey_LeftArrow:	rc = KEY_LEFTARROW;	break;
	case MasqKey_RightArrow:rc = KEY_RIGHTARROW;	break;
	case MasqKey_DownArrow:	rc = KEY_DOWNARROW;	break;
	case MasqKey_UpArrow:	rc = KEY_UPARROW;	break;
	// case MasqKey_A:	rc = KEY_LEFTARROW;	break;
	// case MasqKey_D:	rc = KEY_RIGHTARROW;	break;
	// case MasqKey_S:	rc = KEY_DOWNARROW;	break;
	// case MasqKey_W:	rc = KEY_UPARROW;	break;
	case MasqKey_Escape:	rc = KEY_ESCAPE;	break;
	case MasqKey_Return:
		rc = KEY_ENTER;
		if (mod & MasqKeyModifierLAlt && down) {
			fullscreen ^= 1;
			FrameBuffer_SetFullscreen(ddev_fb, fullscreen);
		}
		break;
	case MasqKey_Tab:	rc = KEY_TAB;		break;
	case MasqKey_F1:	rc = KEY_F1;		break;
	case MasqKey_F2:	rc = KEY_F2;		break;
	case MasqKey_F3:	rc = KEY_F3;		break;
	case MasqKey_F4:	rc = KEY_F4;		break;
	case MasqKey_F5:	rc = KEY_F5;		break;
	case MasqKey_F6:	rc = KEY_F6;		break;
	case MasqKey_F7:	rc = KEY_F7;		break;
	case MasqKey_F8:	rc = KEY_F8;		break;
	case MasqKey_F9:	rc = KEY_F9;		break;
	case MasqKey_F10:	rc = KEY_F10;		break;
	case MasqKey_F11:	rc = KEY_F11;		break;
	case MasqKey_F12:	rc = KEY_F12;		break;

	case MasqKey_Backspace:
	case MasqKey_Delete:	rc = KEY_BACKSPACE;	break;

	case MasqKey_Pause:	rc = KEY_PAUSE;		break;

	case MasqKey_KeypadEqual:
	case MasqKey_Equal:	rc = KEY_EQUALS;	break;

	case MasqKey_KeypadMinus:
	case MasqKey_Minus:	rc = KEY_MINUS;		break;

	// Sprint
	case MasqKey_LeftShift:
	case MasqKey_RightShift:rc = KEY_RSHIFT;	break;

	// Fire
	case MasqKey_LeftCtrl:
	case MasqKey_RightCtrl:	rc = KEY_RCTRL;		break;

	case MasqKey_LeftAlt:
	case MasqKey_LeftMeta:
	case MasqKey_RightAlt:
	case MasqKey_RightMeta:	rc = KEY_RALT;		break;

	// ASCII range (XK_space = 32 ; XK_asciitilde = 126)
	case MasqKey_A:            rc = 'a'; break;
	case MasqKey_B:            rc = 'b'; break;
	case MasqKey_C:            rc = 'c'; break;
	case MasqKey_D:            rc = 'd'; break;
	case MasqKey_E:            rc = 'e'; break;
	case MasqKey_F:            rc = 'f'; break;
	case MasqKey_G:            rc = 'g'; break;
	case MasqKey_H:            rc = 'h'; break;
	case MasqKey_I:            rc = 'i'; break;
	case MasqKey_J:            rc = 'j'; break;
	case MasqKey_K:            rc = 'k'; break;
	case MasqKey_L:            rc = 'l'; break;
	case MasqKey_M:            rc = 'm'; break;
	case MasqKey_N:            rc = 'n'; break;
	case MasqKey_O:            rc = 'o'; break;
	case MasqKey_P:            rc = 'p'; break;
	case MasqKey_Q:            rc = 'q'; break;
	case MasqKey_R:            rc = 'r'; break;
	case MasqKey_S:            rc = 's'; break;
	case MasqKey_T:            rc = 't'; break;
	case MasqKey_U:            rc = 'u'; break;
	case MasqKey_V:            rc = 'v'; break;
	case MasqKey_W:            rc = 'w'; break;
	case MasqKey_X:            rc = 'x'; break;
	case MasqKey_Y:            rc = 'y'; break;
	case MasqKey_Z:            rc = 'z'; break;
	case MasqKey_1:            rc = '1'; break;
	case MasqKey_2:            rc = '2'; break;
	case MasqKey_3:            rc = '3'; break;
	case MasqKey_4:            rc = '4'; break;
	case MasqKey_5:            rc = '5'; break;
	case MasqKey_6:            rc = '6'; break;
	case MasqKey_7:            rc = '7'; break;
	case MasqKey_8:            rc = '8'; break;
	case MasqKey_9:            rc = '9'; break;
	case MasqKey_0:            rc = '0'; break;
	case MasqKey_Space:        rc = ' '; break;
	case MasqKey_LeftBracket:  rc = '('; break;
	case MasqKey_RightBracket: rc = ')'; break;
	case MasqKey_Backslash:    rc = '\\'; break;
	case MasqKey_NonUSHash:    rc = '#'; break;
	case MasqKey_Semi:         rc = ';'; break;
	case MasqKey_Quote:        rc = '\''; break;
	case MasqKey_Grave:        rc = '`'; break;
	case MasqKey_Comma:        rc = ','; break;
	case MasqKey_Dot:          rc = '.'; break;
	case MasqKey_Slash:        rc = '/'; break;

	default:
		rc=0;
    }

    return rc;

}

void I_ShutdownGraphics(void)
{
	System_DropCapability(ddev_fb);
	System_DropCapability(ddev_input);
	System_DropCapability(ddev_sound);
}


void I_GetEvent(void);

//
// I_StartFrame
//
void I_StartFrame (void)
{
    // Wait for the next FrameBuffer frame request.
    while (!VFrameCap) {
	Queue_Wait(ddev_main_q);
	I_GetEvent();
    }
}

// static int	lastmousex = 0;
// static int	lastmousey = 0;
// boolean	mousemoved = false;

void I_GetEvent(void)
{
    event_t event;
    MasqEventHeader *qev;
    Input_KeyEvent *kev;
    Input_PointerEvent *pev;
    FrameBuffer_FrameEvent *fev;

    // put event-grabbing stuff in here
    qev = Queue_Read(ddev_main_q);
    switch (qev->cap)
    {
	  case ddev_sys:
		switch (qev->event)
		{
			case System_Quit:
				I_Quit ();
				break;
		}
		break;
	  case ddev_fb:
		switch (qev->event)
		{
			case FrameBuffer_Frame:
				fev = (FrameBuffer_FrameEvent*)qev;
				VFrameCap = fev->buf_cap;
				break;
		}
		break;
	  case ddev_input:
		switch (qev->event)
		{
			case Input_KeyDown:
				kev = (Input_KeyEvent*)qev;
				event.type = ev_keydown;
				event.data1 = xlatekey(kev->keycode, kev->modifiers, 0);
				D_PostEvent(&event);
				// fprintf(stderr, "k");
				break;
			case Input_KeyUp:
				kev = (Input_KeyEvent*)qev;
				event.type = ev_keyup;
				event.data1 = xlatekey(kev->keycode, kev->modifiers, 1);
				D_PostEvent(&event);
				// fprintf(stderr, "ku");
				break;
			case Input_ButtonDown:
				pev = (Input_PointerEvent*)qev;
				event.type = ev_mouse;
				event.data1 = pev->buttons & 7; // 1,2,4
				event.data2 = event.data3 = 0;
				D_PostEvent(&event);
				// fprintf(stderr, "b");
				break;
			case Input_ButtonUp:
				pev = (Input_PointerEvent*)qev;
				event.type = ev_mouse;
				event.data1 = pev->buttons & 7; // 1,2,4
				event.data2 = event.data3 = 0;
				D_PostEvent(&event);
				// fprintf(stderr, "bu");
				break;
			case Input_PointerMove:
				pev = (Input_PointerEvent*)qev;
				event.type = ev_mouse;
				event.data1 = pev->buttons & 7; // 1,2,4
				event.data2 = pev->x << 2;      // relative mouse movement
				event.data3 = pev->y << 2;

				if (event.data2 || event.data3)
				{
					D_PostEvent(&event);
				}
				break;
		}
		break;
	  case ddev_sound:
		break;
          default:
		break;
    }

    // XXX conditional on what?
    if (qev->cap != -1) {
    	Queue_Advance(ddev_main_q);
    }
}

//
// I_StartTic
//
void I_StartTic (void)
{

    while (!Queue_Empty(ddev_main_q))
		I_GetEvent();

    // mousemoved = false;

}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{

    static int	lasttic;
    int		tics;
    int		i;

    // draws little dots on the bottom of the screen
    if (devparm)
    {
		i = I_GetTime();
		tics = i - lasttic;
		lasttic = i;
		if (tics > 20) tics = 20;

		for (i=0 ; i<tics*2 ; i+=2)
			screens[0][ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0xff;
		for ( ; i<20*2 ; i+=2)
			screens[0][ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0x0;
    }

	// draw the image
	if (VFrameCap)
	{
		// XXX for now copying screens[0] into the FB Buffer
		// at the end of each frame; this fixes the melt effect
		// which otherwise stalls after 1-2 frames.
		// CANNOT change screens[0], see R_InitBuffer.
		memcpy(Buffer_Address(VFrameCap), screens[0], SCREENWIDTH*SCREENHEIGHT);

		// Transfer the render buffer to the framebuffer service.
		// this means we no longer have access to the buffer,
		// until we receive another one via FrameBuffer_Frame event.
		// However DOOM assumes it can still read from screens[0]
		// Hacking around this for now (buffer remains accessible)
		FrameBuffer_Submit(ddev_fb, VFrameCap);

		// Clear framebuffer capability until we receive the next
		// fb_frame event.
		VFrameCap = 0;
	}
}


//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, screens[0], SCREENWIDTH*SCREENHEIGHT);
}

//
// Palette stuff.
//

void UploadNewPalette(byte *palette)
{
    register int	i;
    register int	c;
	RGB* colors = (RGB*) VPalBuf;

	// set the colormap entries
	for (i=0 ; i<256 ; i++)
	{
		c = gammatable[usegamma][*palette++];
		colors[i].red = c;
		c = gammatable[usegamma][*palette++];
		colors[i].green = c;
		c = gammatable[usegamma][*palette++];
		colors[i].blue = c;
	}

	// store the colors to the current colormap
	FrameBuffer_SetPalette(ddev_fb, ddev_palette);
}

//
// I_SetPalette
//
void I_SetPalette (byte* palette)
{
    UploadNewPalette(palette);
}

void I_WaitOKToDraw(void)
{
	// WAITING here... but DOOM assumes screens[0]stays valid after this..
	do {
		Queue_Wait(ddev_main_q);
		I_GetEvent();
	} while (!VFrameCap);

	// screens[0] = Buffer_Address(VFrameCap);
}

void I_InitGraphics(void)
{

    // char*		d;
    // int			n;
    // int			pnum;
    // int			x=0;
    // int			y=0;
    
    // warning: char format, different type arg
    // char		xsign=' ';
    // char		ysign=' ';
    
    static int		firsttime=1;

    if (!firsttime)
		return;
    firsttime = 0;

    //signal(SIGINT, (void (*)(int)) I_Quit);

    // if (M_CheckParm("-2"))
	// multiply = 2;

    // if (M_CheckParm("-3"))
	// multiply = 3;

    // if (M_CheckParm("-4"))
	// multiply = 4;

    // X_width = SCREENWIDTH; // * multiply;
    // X_height = SCREENHEIGHT; // * multiply;

    // check for command-line geometry
    // if ( (pnum=M_CheckParm("-geom")) ) // suggest parentheses around assignment
    // {
	// // warning: char format, different type arg 3,5
	// n = sscanf(myargv[pnum+1], "%c%d%c%d", &xsign, &x, &ysign, &y);
	
	// if (n==2)
	//     x = y = 0;
	// else if (n==6)
	// {
	//     if (xsign == '-')
	// 	x = -x;
	//     if (ysign == '-')
	// 	y = -y;
	// }
	// else
	//     I_Error("bad -geom parameter");
    // }

    // setup attributes for main window
	Input_Subscribe(ddev_input, InputOpt_Key|InputOpt_Button|InputOpt_Pointer, ddev_main_q);

    // create the main window
	FrameBuffer_Create(ddev_fb, FrameBuffer_DoubleBuffer|FrameBuffer_Palette|FrameBuffer_NoSmooth, SCREENWIDTH, SCREENHEIGHT, 8, ddev_main_q);
	FrameBuffer_SetTitle(ddev_fb, "the OG, DOOM");

	// create palette
	// XXX do we create this buffer or does the FrameBuffer?
	Buffer_Create(ddev_palette, 256*4, 0);
	VPalBuf = Buffer_Address(ddev_palette);

    // wait until it is OK to draw
	I_WaitOKToDraw();

    // if (multiply == 1)
	// screens[0] = buffer;
    // else
	// screens[0] = (unsigned char *) malloc (SCREENWIDTH * SCREENHEIGHT);

}
