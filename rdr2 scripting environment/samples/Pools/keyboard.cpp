/*
	THIS FILE IS A PART OF RDR 2 SCRIPT HOOK SDK
				http://dev-c.com
			(C) Alexander Blade 2019
*/

#include "keyboard.h"

const int KEYS_SIZE = 255;

struct {
	DWORD time;
	BOOL hasMessage;
	BOOL isWithAlt;
	BOOL wasDownBefore;
	BOOL isUpNow;
} keyStates[KEYS_SIZE];

void OnKeyboardMessage(DWORD key, WORD repeats, BYTE scanCode, BOOL isExtended, BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow)
{
	if (key < KEYS_SIZE)
	{
		keyStates[key].time = GetTickCount();
		keyStates[key].hasMessage = true;
		keyStates[key].isWithAlt = isWithAlt;
		keyStates[key].wasDownBefore = wasDownBefore;
		keyStates[key].isUpNow = isUpNow;
	}
}

const int NOW_PERIOD = 100, MAX_DOWN = 5000, MAX_DOWN_LONG = 30000; // ms

static bool IsKeyRecent(DWORD key, DWORD period)
{
	// Unsigned elapsed time remains correct when the 32-bit tick counter wraps.
	// A reset/unseen key has no event, including near startup or a later wrap.
	return key < KEYS_SIZE && keyStates[key].hasMessage &&
		static_cast<DWORD>(GetTickCount() - keyStates[key].time) < period;
}

bool IsKeyDown(DWORD key)
{
	return IsKeyRecent(key, MAX_DOWN) && !keyStates[key].isUpNow;
}

bool IsKeyDownLong(DWORD key)
{
	return IsKeyRecent(key, MAX_DOWN_LONG) && !keyStates[key].isUpNow;
}

bool IsKeyJustUp(DWORD key, bool exclusive)
{
	bool b = IsKeyRecent(key, NOW_PERIOD) && keyStates[key].isUpNow;
	if (b && exclusive)
		ResetKeyState(key);
	return b;
}

void ResetKeyState(DWORD key)
{
	if (key < KEYS_SIZE)
		memset(&keyStates[key], 0, sizeof(keyStates[0]));
}
