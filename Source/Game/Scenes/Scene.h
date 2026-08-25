#pragma once
#include <string_view>

// シーン基底
class Scene
{
public:
	Scene() = default;
	virtual ~Scene() = default;

	// 更新処理
	virtual void Update(float elapsedTime) {}

	// 描画処理
	virtual void Render(float dt, class Camera* camera = nullptr) = 0;
	// GUI描画処理
	virtual void DrawGUI() {}

	// Allows the editor to access post-processing universally
	[[nodiscard]] virtual class PostProcessManager* GetPostProcessManager() const noexcept { return nullptr; }

	// Tells the engine where this specific scene saves its post-process settings
	[[nodiscard]] virtual std::string_view GetPostProcessProfilePath() const noexcept
	{
		return "Data/Config/PostProcess_Default.json";
	}

	virtual void OnResize(int width, int height) {}
};
