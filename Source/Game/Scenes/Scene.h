#pragma once
#include <memory>
#include <string_view>
#include "GameObject.h"

// ƒV[ƒ“Šî’ê
class Scene
{
public:
	Scene()
	{
		// Initialize the root node for the Scene Graph
		m_sceneRoot = std::make_unique<GameObject>("Scene Root");
	}
	virtual ~Scene() = default;

	// Updates gameplay logic and the GameObject hierarchy
	virtual void Update(float elapsedTime)
	{
		if (m_sceneRoot) m_sceneRoot->Update(elapsedTime);
	}

	// •`‰æˆ—
	virtual void Render(float dt, class Camera* camera = nullptr) = 0;

	// GUI•`‰æˆ—
	virtual void DrawGUI() {}

	// Allows the editor to access post-processing universally
	[[nodiscard]] virtual class PostProcessManager* GetPostProcessManager() const noexcept { return nullptr; }

	// Tells the engine where this specific scene saves its post-process settings
	[[nodiscard]] virtual std::string_view GetPostProcessProfilePath() const noexcept
	{
		return "Data/Config/PostProcess_Default.json";
	}

	virtual void OnResize(int width, int height) {}
	// Expose the Root GameObject to the EditorManager
	[[nodiscard]] GameObject* GetRootGameObject() const noexcept { return m_sceneRoot.get(); }

protected: 
	std::unique_ptr<GameObject> m_sceneRoot{};
};
