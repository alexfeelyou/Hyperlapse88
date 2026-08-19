#include "Keyboard.h"

void Keyboard::Update()
{
	// Simpan status frame lalu ke variabel previous
	memcpy(previousKeys, currentKeys, sizeof(currentKeys));

	// Ambil status keyboard terbaru dari Windows
	if (!GetKeyboardState(currentKeys))
	{
		// Jika gagal (jarang terjadi), reset
		memset(currentKeys, 0, sizeof(currentKeys));
	}
}

bool Keyboard::IsTriggered(int key) const
{
	// Triggered = Sekarang ditekan (High bits), Sebelumnya tidak
	return (currentKeys[key] & 0x80) && !(previousKeys[key] & 0x80);
}

bool Keyboard::IsPress(int key) const
{
	// Press = Sekarang ditekan
	return (currentKeys[key] & 0x80);
}

bool Keyboard::IsReleased(int key) const
{
	// Released = Sekarang tidak ditekan, Sebelumnya ditekan
	return !(currentKeys[key] & 0x80) && (previousKeys[key] & 0x80);
}