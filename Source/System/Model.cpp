#include <filesystem>
#include <fstream>
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include "Misc.h"
#include "GLTFImporter.h"
#include "GpuResourceUtils.h"
#include "Model.h"

const std::vector<D3D11_INPUT_ELEMENT_DESC> Model::InputElementDescs =
{
	// Slot 0: Per-Vertex Data
	{ "POSITION",                0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL",                  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TANGENT",                 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD",                0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "BONE_WEIGHTS",            0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "BONE_INDICES",            0, DXGI_FORMAT_R32G32B32A32_UINT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },

	// Slot 1: Per-Instance Data (Advance per 1 instance)
	{ "INSTANCE_TRANSFORM",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTANCE_TRANSFORM",      1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTANCE_TRANSFORM",      2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTANCE_TRANSFORM",      3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTANCE_PREV_TRANSFORM", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTANCE_PREV_TRANSFORM", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTANCE_PREV_TRANSFORM", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTANCE_PREV_TRANSFORM", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "INSTANCE_COLOR",          0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 }
};

namespace DirectX
{
	template<class Archive>
	void serialize(Archive& archive, XMUINT4& v)
	{
		archive(
			cereal::make_nvp("x", v.x),
			cereal::make_nvp("y", v.y),
			cereal::make_nvp("z", v.z),
			cereal::make_nvp("w", v.w)
		);
	}

	template<class Archive>
	void serialize(Archive& archive, XMFLOAT2& v)
	{
		archive(
			cereal::make_nvp("x", v.x),
			cereal::make_nvp("y", v.y)
		);
	}

	template<class Archive>
	void serialize(Archive& archive, XMFLOAT3& v)
	{
		archive(
			cereal::make_nvp("x", v.x),
			cereal::make_nvp("y", v.y),
			cereal::make_nvp("z", v.z)
		);
	}

	template<class Archive>
	void serialize(Archive& archive, XMFLOAT4& v)
	{
		archive(
			cereal::make_nvp("x", v.x),
			cereal::make_nvp("y", v.y),
			cereal::make_nvp("z", v.z),
			cereal::make_nvp("w", v.w)
		);
	}

	template<class Archive>
	void serialize(Archive& archive, XMFLOAT4X4& m)
	{
		archive(
			cereal::make_nvp("_11", m._11),
			cereal::make_nvp("_12", m._12),
			cereal::make_nvp("_13", m._13),
			cereal::make_nvp("_14", m._14),
			cereal::make_nvp("_21", m._21),
			cereal::make_nvp("_22", m._22),
			cereal::make_nvp("_23", m._23),
			cereal::make_nvp("_24", m._24),
			cereal::make_nvp("_31", m._31),
			cereal::make_nvp("_32", m._32),
			cereal::make_nvp("_33", m._33),
			cereal::make_nvp("_34", m._34),
			cereal::make_nvp("_41", m._41),
			cereal::make_nvp("_42", m._42),
			cereal::make_nvp("_43", m._43),
			cereal::make_nvp("_44", m._44)
		);
	}
}

template<class Archive>
void Model::Node::serialize(Archive& archive)
{
	archive(
		CEREAL_NVP(name),
		CEREAL_NVP(parentIndex),
		CEREAL_NVP(position),
		CEREAL_NVP(rotation),
		CEREAL_NVP(scale)
	);
}

template<class Archive>
void Model::Vertex::serialize(Archive& archive)
{
	archive(
		CEREAL_NVP(position),
		CEREAL_NVP(boneWeight),
		CEREAL_NVP(boneIndex),
		CEREAL_NVP(texcoord),
		CEREAL_NVP(normal),
		CEREAL_NVP(tangent)
	);
}

template<class Archive>
void Model::Bone::serialize(Archive& archive)
{
	archive(
		CEREAL_NVP(nodeIndex),
		CEREAL_NVP(offsetTransform)
	);
}

template<class Archive>
void Model::Mesh::serialize(Archive& archive)
{
	archive(
		CEREAL_NVP(vertices),
		CEREAL_NVP(indices),
		CEREAL_NVP(bones),
		CEREAL_NVP(nodeIndex),
		CEREAL_NVP(materialIndex)
	);
}

template<class Archive>
void Model::VectorKeyframe::serialize(Archive& archive)
{
	archive(
		CEREAL_NVP(seconds),
		CEREAL_NVP(value)
	);
}

template<class Archive>
void Model::QuaternionKeyframe::serialize(Archive& archive)
{
	archive(
		CEREAL_NVP(seconds),
		CEREAL_NVP(value)
	);
}

template<class Archive>
void Model::NodeAnim::serialize(Archive& archive)
{
	archive(
		CEREAL_NVP(positionKeyframes),
		CEREAL_NVP(rotationKeyframes),
		CEREAL_NVP(scaleKeyframes)
	);
}

template<class Archive>
void Model::Animation::serialize(Archive& archive)
{
	archive(
		CEREAL_NVP(name),
		CEREAL_NVP(secondsLength),
		CEREAL_NVP(nodeAnims)
	);
}

// コンストラクタ
Model::Model(ID3D11Device* device, const char* filename, float sampleRate)
{
	std::filesystem::path filepath(filename);
	std::filesystem::path dirpath(filepath.parent_path());

	std::filesystem::path extension = filepath.extension();

	// 独自形式のモデルファイルの存在確認
	filepath.replace_extension(".cereal");
	if (std::filesystem::exists(filepath))
	{
		// 独自形式のモデルファイルの読み込み
		Deserialize(filepath.string().c_str());
	}
	else if (extension == ".gltf" || extension == ".glb")
	{
		// 汎用モデルファイルの読み込み
		GLTFImporter importer(filename);

		// マテリアルデータ読み取り
		importer.LoadMaterials(materials, device);

		// ノードデータ読み取り
		importer.LoadNodes(nodes);

		// メッシュデータ読み取り
		importer.LoadMeshes(meshes, nodes);

		// アニメーションデータ読み取り
		importer.LoadAnimations(animations, nodes, sampleRate);

		// 独自形式のモデルファイルを保存
		//Serialize(filepath.string().c_str());
	}
	else
	{
		_ASSERT_EXPR_A(false, "found not model file");
	}

	// マテリアル構築
	for (Material& material : materials)
	{
		if (material.baseMap == nullptr)
		{
			if (material.baseTextureFileName.empty())
			{
				// ダミーテクスチャ作成
				HRESULT hr = GpuResourceUtils::CreateDummyTexture(device, 0xFFFFFFFF,
					material.baseMap.GetAddressOf());
				_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
			}
			else
			{
				// ベーステクスチャ読み込み
				std::filesystem::path diffuseTexturePath(dirpath / material.baseTextureFileName);
				HRESULT hr = GpuResourceUtils::LoadTexture(device, diffuseTexturePath.string().c_str(),
					material.baseMap.GetAddressOf());
				_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
			}
		}

		if (material.normalMap == nullptr)
		{
			if (material.normalTextureFileName.empty())
			{
				// 法線ダミーテクスチャ作成
				HRESULT hr = GpuResourceUtils::CreateDummyTexture(device, 0xFFFF7F7F,
					material.normalMap.GetAddressOf());
				_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
			}
			else
			{
				// 法線テクスチャ読み込み
				std::filesystem::path texturePath(dirpath / material.normalTextureFileName);
				HRESULT hr = GpuResourceUtils::LoadTexture(device, texturePath.string().c_str(),
					material.normalMap.GetAddressOf());
				_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
			}
		}
	}

	// ノード構築
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		Node& node = nodes.at(nodeIndex);

		// 親子関係を構築
		node.parent = node.parentIndex >= 0 ? &nodes.at(node.parentIndex) : nullptr;
		if (node.parent != nullptr)
		{
			node.parent->children.emplace_back(&node);
		}
	}

	// メッシュ構築
	for (Mesh& mesh : meshes)
	{
		// 参照マテリアル設定
		mesh.material = &materials.at(mesh.materialIndex);

		// 参照ノード設定
		mesh.node = &nodes.at(mesh.nodeIndex);

		// 頂点バッファ
		{
			D3D11_BUFFER_DESC bufferDesc = {};
			D3D11_SUBRESOURCE_DATA subresourceData = {};

			bufferDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * mesh.vertices.size());
			bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
			bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bufferDesc.CPUAccessFlags = 0;
			bufferDesc.MiscFlags = 0;
			bufferDesc.StructureByteStride = 0;
			subresourceData.pSysMem = mesh.vertices.data();
			subresourceData.SysMemPitch = 0;
			subresourceData.SysMemSlicePitch = 0;

			HRESULT hr = device->CreateBuffer(&bufferDesc, &subresourceData, mesh.vertexBuffer.GetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
		}

		// インデックスバッファ
		{
			D3D11_BUFFER_DESC bufferDesc = {};
			D3D11_SUBRESOURCE_DATA subresourceData = {};

			bufferDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * mesh.indices.size());
			bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
			bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			bufferDesc.CPUAccessFlags = 0;
			bufferDesc.MiscFlags = 0;
			bufferDesc.StructureByteStride = 0;
			subresourceData.pSysMem = mesh.indices.data();
			subresourceData.SysMemPitch = 0;
			subresourceData.SysMemSlicePitch = 0;
			HRESULT hr = device->CreateBuffer(&bufferDesc, &subresourceData, mesh.indexBuffer.GetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
		}

		// ボーン構築
		for (Bone& bone : mesh.bones)
		{
			// 参照ノード設定
			bone.node = &nodes.at(bone.nodeIndex);
		}
	}

	// 行列初期化
	DirectX::XMFLOAT4X4 worldTransform;
	DirectX::XMStoreFloat4x4(&worldTransform, DirectX::XMMatrixIdentity());
	UpdateTransform(worldTransform);
	ComputeBounds();
}

// アニメーション追加読み込み
void Model::AppendAnimations(const char* filename)
{
	std::filesystem::path filepath(filename);
	std::filesystem::path dirpath(filepath.parent_path());

	if (filepath.extension() == ".gltf" ||
		filepath.extension() == ".glb")
	{
		// 汎用モデルファイルの読み込み
		GLTFImporter importer(filename);

		// アニメーションデータ読み取り
		importer.LoadAnimations(animations, nodes);
	}
	else
	{
		_ASSERT_EXPR_A(false, "found not model file");
	}
}

// アニメーションインデックス取得
int Model::GetAnimationIndex(const char* name) const
{
	for (size_t animationIndex = 0; animationIndex < animations.size(); ++animationIndex)
	{
		if (animations.at(animationIndex).name == name)
		{
			return static_cast<int>(animationIndex);
		}
	}
	return -1;
}

// ノードインデックス取得
int Model::GetNodeIndex(const char* name) const
{
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		if (nodes.at(nodeIndex).name == name)
		{
			return static_cast<int>(nodeIndex);
		}
	}
	return -1;
}

// バウンディング情報計算
void Model::ComputeBounds() noexcept
{
	DirectX::XMFLOAT3 minModel{ FLT_MAX, FLT_MAX, FLT_MAX };
	DirectX::XMFLOAT3 maxModel{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (Mesh& mesh : meshes)
	{
		if (mesh.vertices.empty()) continue;

		DirectX::XMFLOAT3 minMesh{ FLT_MAX, FLT_MAX, FLT_MAX };
		DirectX::XMFLOAT3 maxMesh{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

		for (const auto& v : mesh.vertices)
		{
			minMesh.x = (std::min)(minMesh.x, v.position.x);
			minMesh.y = (std::min)(minMesh.y, v.position.y);
			minMesh.z = (std::min)(minMesh.z, v.position.z);

			maxMesh.x = (std::max)(maxMesh.x, v.position.x);
			maxMesh.y = (std::max)(maxMesh.y, v.position.y);
			maxMesh.z = (std::max)(maxMesh.z, v.position.z);
		}

		mesh.boundsCenter = {
			(minMesh.x + maxMesh.x) * 0.5f,
			(minMesh.y + maxMesh.y) * 0.5f,
			(minMesh.z + maxMesh.z) * 0.5f
		};
		mesh.boundsExtents = {
			(maxMesh.x - minMesh.x) * 0.5f,
			(maxMesh.y - minMesh.y) * 0.5f,
			(maxMesh.z - minMesh.z) * 0.5f
		};
		float maxDistSq = 0.0f;
		for (const auto& v : mesh.vertices)
		{
			const float dx = v.position.x - mesh.boundsCenter.x;
			const float dy = v.position.y - mesh.boundsCenter.y;
			const float dz = v.position.z - mesh.boundsCenter.z;
			maxDistSq = (std::max)(maxDistSq, dx * dx + dy * dy + dz * dz);
		}
		mesh.boundsRadius = std::sqrt(maxDistSq);

		// Skinned mesh animation safety margin (prevents limbs from clipping outside bounds)
		if (!mesh.bones.empty())
		{
			mesh.boundsRadius *= 1.4f;
			mesh.boundsExtents.x *= 1.4f;
			mesh.boundsExtents.y *= 1.4f;
			mesh.boundsExtents.z *= 1.4f;
		}

		minModel.x = (std::min)(minModel.x, minMesh.x);
		minModel.y = (std::min)(minModel.y, minMesh.y);
		minModel.z = (std::min)(minModel.z, minMesh.z);

		maxModel.x = (std::max)(maxModel.x, maxMesh.x);
		maxModel.y = (std::max)(maxModel.y, maxMesh.y);
		maxModel.z = (std::max)(maxModel.z, maxMesh.z);
	}

	m_boundsCenter = {
		(minModel.x + maxModel.x) * 0.5f,
		(minModel.y + maxModel.y) * 0.5f,
		(minModel.z + maxModel.z) * 0.5f
	};
	m_boundsExtents = {
		(maxModel.x - minModel.x) * 0.5f,
		(maxModel.y - minModel.y) * 0.5f,
		(maxModel.z - minModel.z) * 0.5f
	};

	float maxModelDistSq = 0.0f;
	for (const Mesh& mesh : meshes)
	{
		for (const auto& v : mesh.vertices)
		{
			const float dx = v.position.x - m_boundsCenter.x;
			const float dy = v.position.y - m_boundsCenter.y;
			const float dz = v.position.z - m_boundsCenter.z;
			maxModelDistSq = (std::max)(maxModelDistSq, dx * dx + dy * dy + dz * dz);
		}
	}
	m_boundsRadius = std::sqrt(maxModelDistSq);
	if (m_boundsRadius <= 0.0f) m_boundsRadius = 1.0f;
}

// トランスフォーム更新処理
void Model::UpdateTransform(const DirectX::XMFLOAT4X4& worldTransform)
{
	DirectX::XMMATRIX ParentWorldTransform = DirectX::XMLoadFloat4x4(&worldTransform);

	for (Node& node : nodes)
	{
		// Direct single-pass affine matrix composition: S * R * T without intermediate matrix multiplies
		DirectX::XMMATRIX LocalTransform{ DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&node.rotation)) };
		LocalTransform.r[0] = DirectX::XMVectorMultiply(LocalTransform.r[0], DirectX::XMVectorReplicate(node.scale.x));
		LocalTransform.r[1] = DirectX::XMVectorMultiply(LocalTransform.r[1], DirectX::XMVectorReplicate(node.scale.y));
		LocalTransform.r[2] = DirectX::XMVectorMultiply(LocalTransform.r[2], DirectX::XMVectorReplicate(node.scale.z));
		LocalTransform.r[3] = DirectX::XMVectorSet(node.position.x, node.position.y, node.position.z, 1.0f);

		// グローバル行列算出
		DirectX::XMMATRIX ParentGlobalTransform;
		if (node.parent != nullptr)
		{
			ParentGlobalTransform = DirectX::XMLoadFloat4x4(&node.parent->globalTransform);
		}
		else
		{
			ParentGlobalTransform = DirectX::XMMatrixIdentity();
		}
		DirectX::XMMATRIX GlobalTransform = LocalTransform * ParentGlobalTransform;

		// ワールド行列算出
		DirectX::XMMATRIX WorldTransform = GlobalTransform * ParentWorldTransform;

		// 計算結果を格納
		DirectX::XMStoreFloat4x4(&node.localTransform, LocalTransform);
		DirectX::XMStoreFloat4x4(&node.globalTransform, GlobalTransform);
		DirectX::XMStoreFloat4x4(&node.worldTransform, WorldTransform);
	}
}

namespace
{
	template <typename KeyframeType>
	[[nodiscard]] std::size_t FindKeyframeSegment(
		const std::vector<KeyframeType>& keyframes,
		float time,
		uint32_t* hint = nullptr) noexcept
	{
		const std::size_t count{ keyframes.size() };
		if (count <= 2) return 0;

		// Temporal O(1) Fast Path: Check if playback is within current or immediate next segment
		if (hint && *hint < static_cast<uint32_t>(count - 1))
		{
			const std::size_t currentIdx{ static_cast<std::size_t>(*hint) };

			// Time is still within the cached segment
			if (time >= keyframes[currentIdx].seconds && time < keyframes[currentIdx + 1].seconds)
			{
				return currentIdx;
			}

			// Time has monotonically advanced to the immediately following segment
			if (currentIdx + 2 < count &&
				time >= keyframes[currentIdx + 1].seconds &&
				time < keyframes[currentIdx + 2].seconds)
			{
				*hint = static_cast<uint32_t>(currentIdx + 1);
				return currentIdx + 1;
			}
		}

		// Fallback O(log K): Binary search on animation loop wrap, scrub, or large jump
		auto it{ std::upper_bound(keyframes.begin(), keyframes.end(), time,
			[](float t, const KeyframeType& k) noexcept {
				return t < k.seconds;
			}) };

		std::size_t resultIdx{ 0 };
		if (it == keyframes.begin())
		{
			resultIdx = 0;
		}
		else
		{
			const std::size_t idx{ static_cast<std::size_t>(std::distance(keyframes.begin(), it)) - 1 };
			resultIdx = (idx < count - 1) ? idx : count - 2;
		}

		if (hint)
		{
			*hint = static_cast<uint32_t>(resultIdx);
		}
		return resultIdx;
	}
}

void Model::ComputeAnimation(int animationIndex, int nodeIndex, float time, NodePose& nodePose, TrackCursors* cursor) const
{
	if (animationIndex < 0 || static_cast<std::size_t>(animationIndex) >= animations.size()) return;
	const Animation& animation{ animations[animationIndex] };
	if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= animation.nodeAnims.size()) return;
	const NodeAnim& nodeAnim{ animation.nodeAnims[nodeIndex] };

	// Position track evaluation
	if (!nodeAnim.positionKeyframes.empty())
	{
		if (nodeAnim.positionKeyframes.size() == 1 || time <= nodeAnim.positionKeyframes.front().seconds)
		{
			nodePose.position = nodeAnim.positionKeyframes.front().value;
			if (cursor) cursor->pos = 0;
		}
		else if (time >= nodeAnim.positionKeyframes.back().seconds)
		{
			nodePose.position = nodeAnim.positionKeyframes.back().value;
			if (cursor && nodeAnim.positionKeyframes.size() >= 2)
			{
				cursor->pos = static_cast<uint32_t>(nodeAnim.positionKeyframes.size() - 2);
			}
		}
		else
		{
			const std::size_t idx{ FindKeyframeSegment(nodeAnim.positionKeyframes, time, cursor ? &cursor->pos : nullptr) };
			const auto& k0{ nodeAnim.positionKeyframes[idx] };
			const auto& k1{ nodeAnim.positionKeyframes[idx + 1] };
			const float duration{ k1.seconds - k0.seconds };
			const float rate{ (duration > 0.00001f) ? std::clamp((time - k0.seconds) / duration, 0.0f, 1.0f) : 0.0f };

			const DirectX::XMVECTOR v0{ DirectX::XMLoadFloat3(&k0.value) };
			const DirectX::XMVECTOR v1{ DirectX::XMLoadFloat3(&k1.value) };
			DirectX::XMStoreFloat3(&nodePose.position, DirectX::XMVectorLerp(v0, v1, rate));
		}
	}
	else if (static_cast<std::size_t>(nodeIndex) < nodes.size())
	{
		nodePose.position = nodes[nodeIndex].position;
	}

	// Rotation track evaluation
	if (!nodeAnim.rotationKeyframes.empty())
	{
		if (nodeAnim.rotationKeyframes.size() == 1 || time <= nodeAnim.rotationKeyframes.front().seconds)
		{
			nodePose.rotation = nodeAnim.rotationKeyframes.front().value;
			if (cursor) cursor->rot = 0;
		}
		else if (time >= nodeAnim.rotationKeyframes.back().seconds)
		{
			nodePose.rotation = nodeAnim.rotationKeyframes.back().value;
			if (cursor && nodeAnim.rotationKeyframes.size() >= 2)
			{
				cursor->rot = static_cast<uint32_t>(nodeAnim.rotationKeyframes.size() - 2);
			}
		}
		else
		{
			const std::size_t idx{ FindKeyframeSegment(nodeAnim.rotationKeyframes, time, cursor ? &cursor->rot : nullptr) };
			const auto& k0{ nodeAnim.rotationKeyframes[idx] };
			const auto& k1{ nodeAnim.rotationKeyframes[idx + 1] };
			const float duration{ k1.seconds - k0.seconds };
			const float rate{ (duration > 0.00001f) ? std::clamp((time - k0.seconds) / duration, 0.0f, 1.0f) : 0.0f };

			const DirectX::XMVECTOR q0{ DirectX::XMLoadFloat4(&k0.value) };
			const DirectX::XMVECTOR q1{ DirectX::XMLoadFloat4(&k1.value) };
			DirectX::XMStoreFloat4(&nodePose.rotation, DirectX::XMQuaternionSlerp(q0, q1, rate));
		}
	}
	else if (static_cast<std::size_t>(nodeIndex) < nodes.size())
	{
		nodePose.rotation = nodes[nodeIndex].rotation;
	}

	// Scale track evaluation
	if (!nodeAnim.scaleKeyframes.empty())
	{
		if (nodeAnim.scaleKeyframes.size() == 1 || time <= nodeAnim.scaleKeyframes.front().seconds)
		{
			nodePose.scale = nodeAnim.scaleKeyframes.front().value;
			if (cursor) cursor->scale = 0;
		}
		else if (time >= nodeAnim.scaleKeyframes.back().seconds)
		{
			nodePose.scale = nodeAnim.scaleKeyframes.back().value;
			if (cursor && nodeAnim.scaleKeyframes.size() >= 2)
			{
				cursor->scale = static_cast<uint32_t>(nodeAnim.scaleKeyframes.size() - 2);
			}
		}
		else
		{
			const std::size_t idx{ FindKeyframeSegment(nodeAnim.scaleKeyframes, time, cursor ? &cursor->scale : nullptr) };
			const auto& k0{ nodeAnim.scaleKeyframes[idx] };
			const auto& k1{ nodeAnim.scaleKeyframes[idx + 1] };
			const float duration{ k1.seconds - k0.seconds };
			const float rate{ (duration > 0.00001f) ? std::clamp((time - k0.seconds) / duration, 0.0f, 1.0f) : 0.0f };

			const DirectX::XMVECTOR v0{ DirectX::XMLoadFloat3(&k0.value) };
			const DirectX::XMVECTOR v1{ DirectX::XMLoadFloat3(&k1.value) };
			DirectX::XMStoreFloat3(&nodePose.scale, DirectX::XMVectorLerp(v0, v1, rate));
		}
	}
	else if (static_cast<std::size_t>(nodeIndex) < nodes.size())
	{
		nodePose.scale = nodes[nodeIndex].scale;
	}
}

// アニメーション計算
void Model::ComputeAnimation(int animationIndex, float time, std::vector<NodePose>& nodePoses, TrackCursors* cursors) const
{
	if (animationIndex < 0 || static_cast<std::size_t>(animationIndex) >= animations.size()) return;
	if (nodePoses.size() != nodes.size())
	{
		nodePoses.resize(nodes.size());
	}
	const Animation& anim{ animations[animationIndex] };
	const std::size_t animNodeCount{ (std::min)(anim.nodeAnims.size(), nodePoses.size()) };
	for (size_t nodeIndex = 0; nodeIndex < animNodeCount; ++nodeIndex)
	{
		ComputeAnimation(animationIndex, static_cast<int>(nodeIndex), time, nodePoses[nodeIndex], cursors ? &cursors[nodeIndex] : nullptr);
	}
}

// ノードポーズ設定
void Model::SetNodePoses(const std::vector<NodePose>& nodePoses)
{
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		const NodePose& pose = nodePoses.at(nodeIndex);
		Node& node = nodes.at(nodeIndex);

		node.position = pose.position;
		node.rotation = pose.rotation;
		node.scale = pose.scale;
	}
}

// ノードポーズ取得
void Model::GetNodePoses(std::vector<NodePose>& nodePoses) const
{
	if (nodePoses.size() != nodes.size())
	{
		nodePoses.resize(nodes.size());
	}
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
	{
		const Node& node = nodes.at(nodeIndex);
		NodePose& pose = nodePoses.at(nodeIndex);

		pose.position = node.position;
		pose.rotation = node.rotation;
		pose.scale = node.scale;
	}
}

// シリアライズ
void Model::Serialize(const char* filename)
{
	std::ofstream ostream(filename, std::ios::binary);
	if (ostream.is_open())
	{
		cereal::BinaryOutputArchive archive(ostream);

		try
		{
			archive(
				CEREAL_NVP(nodes),
				CEREAL_NVP(materials),
				CEREAL_NVP(meshes),
				CEREAL_NVP(animations)
			);
		}
		catch (...)
		{
			_ASSERT_EXPR_A(false, "Model serialize failed.");
		}
	}
}

// デシリアライズ
void Model::Deserialize(const char* filename)
{
	std::ifstream istream(filename, std::ios::binary);
	if (istream.is_open())
	{
		cereal::BinaryInputArchive archive(istream);

		try
		{
			archive(
				CEREAL_NVP(nodes),
				CEREAL_NVP(materials),
				CEREAL_NVP(meshes),
				CEREAL_NVP(animations)
			);
			ComputeBounds();
		}
		catch (...)
		{
			_ASSERT_EXPR_A(false, "Model deserialize failed.");
		}
	}
	else
	{
		_ASSERT_EXPR_A(false, "Model File not found.");
	}
}
