#pragma once
#include <windows.h>
#include <vector>

class Keyboard
{
public:
	Keyboard() = default;
	~Keyboard() = default;

	// Update state setiap frame
	void Update();

	// Cek apakah tombol DITEKAN (sekali saja saat turun)
	bool IsTriggered(int key) const;

	// Cek apakah tombol SEDANG DITAHAN
	bool IsPress(int key) const;

	// Cek apakah tombol DILEPAS (sekali saja saat naik)
	bool IsReleased(int key) const;

private:
	// Menyimpan status tombol (0 = Up, 1 = Down)
	// Kita pakai array 256 karena kode ASCII/Virtual Key max 256
	BYTE currentKeys[256] = { 0 };
	BYTE previousKeys[256] = { 0 };
};