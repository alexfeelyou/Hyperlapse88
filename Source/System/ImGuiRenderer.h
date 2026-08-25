#pragma once

#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <ImGuizmo.h>
#include <implot.h>
#include <Windows.h>

class ImGuiRenderer
{
public:
	// 初期化
	static void Initialize(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* dc);
	
	// 終了化
	static void Finalize();

	// フレーム開始処理
	static void NewFrame();

	// 描画実行
	static void Render(ID3D11DeviceContext* context);

	// WIN32メッセージハンドラー
	static LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
