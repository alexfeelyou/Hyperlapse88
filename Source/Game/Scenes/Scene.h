#pragma once
#include <memory>
#include <string_view>
#include "GameObject.h"

// ÉVÅ[ÉìäÓíÍ
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

	// ï`âÊèàóù
	virtual void Render(float dt, class Camera* camera = nullptr) = 0;

	// GUIï`âÊèàóù
	virtual void DrawGUI() {}

	// Allows the editor to access post-processing universally
	[[nodiscard]] virtual class PostProcessManager* GetPostProcessManager() const noexcept { return nullptr; }

	// Tells the engine where this specific scene saves its state
	[[nodiscard]] virtual std::string_view GetSceneSavePath() const noexcept
	{
		return "Data/Scenes/Scene_Default.json";
	}

	// Tells the engine where this specific scene saves its post-process settings
	[[nodiscard]] virtual std::string_view GetPostProcessProfilePath() const noexcept
	{
		return "Data/Config/PostProcess_Default.json";
	}

	// Tells the engine the human-readable name of this scene for the Editor
	[[nodiscard]] virtual std::string_view GetSceneName() const noexcept
	{
		return "Scene_Default";
	}

	virtual void OnResize(int width, int height) {}
	// Expose the Root GameObject to the EditorManager
	[[nodiscard]] GameObject* GetRootGameObject() const noexcept { return m_sceneRoot.get(); }

protected: 
	std::unique_ptr<GameObject> m_sceneRoot{};
};
