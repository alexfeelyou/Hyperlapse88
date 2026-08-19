#include "System/Mouse.h"

static const int KeyMap[] =
{
	VK_LBUTTON,		// 左ボタン
	VK_MBUTTON,		// 中ボタン
	VK_RBUTTON,		// 右ボタン
};

// コンストラクタ
Mouse::Mouse(HWND hWnd)
	: hWnd(hWnd)
{
	RECT rc;
	GetClientRect(hWnd, &rc);
	screenWidth = rc.right - rc.left;
	screenHeight = rc.bottom - rc.top;
}

// 更新
void Mouse::Update()
{
	// スイッチ情報
	MouseButton newButtonState = 0;

	for (int i = 0; i < ARRAYSIZE(KeyMap); ++i)
	{
		if (::GetAsyncKeyState(KeyMap[i]) & 0x8000)
		{
			newButtonState |= (1 << i);
		}
	}

	// ホイール
	wheel[1] = wheel[0];
	wheel[0] = 0;

	// ボタン情報更新
	buttonState[1] = buttonState[0];	// スイッチ履歴
	buttonState[0] = newButtonState;

	buttonDown = ~buttonState[1] & newButtonState;	// 押した瞬間
	buttonUp = ~newButtonState & buttonState[1];	// 離した瞬間

	// カーソル位置の取得
	POINT cursor;
	::GetCursorPos(&cursor);
	::ScreenToClient(hWnd, &cursor);

	// Perbaikan: Logika kunci kursor yang baru
	if (isCursorLocked)
	{
		// Hitung posisi tengah layar
		RECT clientRect;
		GetClientRect(hWnd, &clientRect);
		centerPosition.x = (clientRect.right - clientRect.left) / 2;
		centerPosition.y = (clientRect.bottom - clientRect.top) / 2;

		// Hitung delta berdasarkan pergerakan dari posisi tengah
		deltaX = static_cast<float>(cursor.x - centerPosition.x);
		deltaY = static_cast<float>(cursor.y - centerPosition.y);

		// Posisikan ulang kursor ke tengah layar
		ClientToScreen(hWnd, &centerPosition);
		SetCursorPos(centerPosition.x, centerPosition.y);
	}
	else
	{
		// Jika tidak dilock, hitung delta seperti biasa
		deltaX = static_cast<float>(cursor.x - positionX[0]);
		deltaY = static_cast<float>(cursor.y - positionY[0]);
	}


	// 画面のサイズを取得する。
	RECT rc;
	GetClientRect(hWnd, &rc);
	UINT screenW = rc.right - rc.left;
	UINT screenH = rc.bottom - rc.top;
	UINT viewportW = screenWidth;
	UINT viewportH = screenHeight;

	// 画面補正
	positionX[1] = positionX[0];
	positionY[1] = positionY[0];
	positionX[0] = (LONG)(cursor.x / static_cast<float>(viewportW) * static_cast<float>(screenW));
	positionY[0] = (LONG)(cursor.y / static_cast<float>(viewportH) * static_cast<float>(screenH));

	//if (isCursorLocked)
	//{
	//	// Hitung posisi tengah layar
	//	RECT clientRect;
	//	GetClientRect(hWnd, &clientRect);
	//	centerPosition.x = (clientRect.right - clientRect.left) / 2;
	//	centerPosition.y = (clientRect.bottom - clientRect.top) / 2;

	//	// Posisikan kursor ke tengah layar
	//	ClientToScreen(hWnd, &centerPosition);
	//	SetCursorPos(centerPosition.x, centerPosition.y);
	//}
}

void Mouse::LockCursor(bool lock)
{
	if (isCursorLocked != lock) {
		isCursorLocked = lock;
		ShowCursor(!isCursorLocked);
	}
}

DirectX::XMFLOAT3 Mouse::GetWorldPosition(const DirectX::XMFLOAT4X4& viewMatrix, const DirectX::XMFLOAT4X4& projMatrix) const
{
	// 1. Ambil posisi yang sudah disesuaikan dengan viewport (menggunakan variabel internal Mouse)
	float mouseX = static_cast<float>(positionX[0]);
	float mouseY = static_cast<float>(positionY[0]);

	// 2. Setup vektor awal
	DirectX::XMVECTOR mouseNear = DirectX::XMVectorSet(mouseX, mouseY, 0.0f, 1.0f);
	DirectX::XMVECTOR mouseFar = DirectX::XMVectorSet(mouseX, mouseY, 1.0f, 1.0f);

	// 3. Load matriks
	DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&viewMatrix);
	DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(&projMatrix);
	DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();

	// 4. Unproject (Gunakan screenWidth dan screenHeight yang tersimpan di class Mouse)
	DirectX::XMVECTOR pNear = DirectX::XMVector3Unproject(
		mouseNear, 0, 0, static_cast<float>(screenWidth), static_cast<float>(screenHeight),
		0.0f, 1.0f, proj, view, world
	);
	DirectX::XMVECTOR pFar = DirectX::XMVector3Unproject(
		mouseFar, 0, 0, static_cast<float>(screenWidth), static_cast<float>(screenHeight),
		0.0f, 1.0f, proj, view, world
	);

	// 5. Kalkulasi Raycast ke lantai (Y = 0)
	DirectX::XMVECTOR rayDir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(pFar, pNear));

	// Cegah division by zero jika kamera menatap lurus sejajar lantai (jarang terjadi di top-down)
	float dirY = DirectX::XMVectorGetY(rayDir);
	if (std::abs(dirY) < 0.0001f) {
		return { 0.0f, 0.0f, 0.0f };
	}

	float t = -DirectX::XMVectorGetY(pNear) / dirY;
	DirectX::XMVECTOR groundPoint = DirectX::XMVectorAdd(pNear, DirectX::XMVectorScale(rayDir, t));

	DirectX::XMFLOAT3 result;
	DirectX::XMStoreFloat3(&result, groundPoint);
	return result;
}