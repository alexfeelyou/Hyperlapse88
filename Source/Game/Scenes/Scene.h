#pragma once

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

	virtual void OnResize(int width, int height) {}
};
