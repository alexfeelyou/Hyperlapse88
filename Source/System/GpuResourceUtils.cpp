#include <filesystem>
#include <fstream>
#include <vector>
#include <wrl.h>
#include <DirectXTex.h>
#include "Misc.h"
#include "GpuResourceUtils.h"

namespace
{
	// Centralized, safe file reading leveraging RAII
	[[nodiscard]] std::vector<uint8_t> ReadFileToBuffer(std::string_view filename)
	{
		std::ifstream file{ filename.data(), std::ios::binary | std::ios::ate };
		_ASSERT_EXPR_A(file.is_open(), "Shader File not found");

		const std::streamsize size{ file.tellg() };
		file.seekg(0, std::ios::beg);

		std::vector<uint8_t> buffer(static_cast<std::size_t>(size));
		file.read(reinterpret_cast<char*>(buffer.data()), size);
		return buffer;
	}
}

// 頂点シェーダー読み込み
HRESULT GpuResourceUtils::LoadVertexShader(
	ID3D11Device* device, const char* filename,
	const D3D11_INPUT_ELEMENT_DESC inputElementDescs[], UINT inputElementCount,
	ID3D11InputLayout** inputLayout, ID3D11VertexShader** vertexShader)
{
	const std::vector<uint8_t> data{ ReadFileToBuffer(filename) };

	HRESULT hr{ device->CreateVertexShader(data.data(), data.size(), nullptr, vertexShader) };
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	if (inputLayout)
	{
		hr = device->CreateInputLayout(inputElementDescs, inputElementCount, data.data(), data.size(), inputLayout);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}
	return hr;
}

// ピクセルシェーダー読み込み
HRESULT GpuResourceUtils::LoadPixelShader(ID3D11Device* device, const char* filename, ID3D11PixelShader** pixelShader)
{
	const std::vector<uint8_t> data{ ReadFileToBuffer(filename) };
	const HRESULT hr{ device->CreatePixelShader(data.data(), data.size(), nullptr, pixelShader) };
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	return hr;
}

// テクスチャ読み込み
HRESULT GpuResourceUtils::LoadTexture(
	ID3D11Device* device,
	const char* filename,
	ID3D11ShaderResourceView** shaderResourceView,
	D3D11_TEXTURE2D_DESC* texture2dDesc)
{
	// 拡張子を取得
	std::filesystem::path filepath(filename);
	std::string extension = filepath.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), tolower);	// 小文字化

	// ワイド文字に変換
	std::wstring wfilename = filepath.wstring();

	// フォーマット毎に画像読み込み処理
	HRESULT hr;
	DirectX::TexMetadata metadata;
	DirectX::ScratchImage scratch_image;
	if (extension == ".tga")
	{
		hr = DirectX::GetMetadataFromTGAFile(wfilename.c_str(), metadata);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		hr = DirectX::LoadFromTGAFile(wfilename.c_str(), &metadata, scratch_image);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}
	else if (extension == ".dds")
	{
		hr = DirectX::GetMetadataFromDDSFile(wfilename.c_str(), DirectX::DDS_FLAGS_NONE, metadata);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		hr = DirectX::LoadFromDDSFile(wfilename.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratch_image);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}
	else if (extension == ".hdr")
	{
		hr = DirectX::GetMetadataFromHDRFile(wfilename.c_str(), metadata);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		hr = DirectX::LoadFromHDRFile(wfilename.c_str(), &metadata, scratch_image);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}
	else
	{
		hr = DirectX::GetMetadataFromWICFile(wfilename.c_str(), DirectX::WIC_FLAGS_NONE, metadata);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

		hr = DirectX::LoadFromWICFile(wfilename.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, scratch_image);
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
	}

	if (metadata.mipLevels == 1)
	{
		DirectX::ScratchImage mipChain;
		// Generate a full mipmap chain using box filtering
		hr = DirectX::GenerateMipMaps(scratch_image.GetImages(), scratch_image.GetImageCount(),
			scratch_image.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
		if (SUCCEEDED(hr))
		{
			scratch_image = std::move(mipChain);
			metadata = scratch_image.GetMetadata();
		}
	}

	// シェーダーリソースビュー作成
	hr = DirectX::CreateShaderResourceView(device, scratch_image.GetImages(), scratch_image.GetImageCount(),
		metadata, shaderResourceView);
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	// テクスチャ情報取得
	if (texture2dDesc != nullptr)
	{
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;
		(*shaderResourceView)->GetResource(resource.GetAddressOf());

		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
		hr = resource->QueryInterface<ID3D11Texture2D>(texture2d.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
		texture2d->GetDesc(texture2dDesc);
	}
	return hr;
}

// テクスチャ読み込み
HRESULT GpuResourceUtils::LoadTexture(
	ID3D11Device* device,
	const void* data,
	size_t size,
	ID3D11ShaderResourceView** shaderResourceView,
	D3D11_TEXTURE2D_DESC* texture2dDesc)
{
	// フォーマット毎に画像読み込み処理
	HRESULT hr = E_FAIL;
	DirectX::TexMetadata metadata;
	DirectX::ScratchImage scratch_image;

	// .tga
	{
		hr = DirectX::GetMetadataFromTGAMemory(data, size, metadata);
		if (SUCCEEDED(hr))
		{
			hr = DirectX::LoadFromTGAMemory(data, size, &metadata, scratch_image);
		}
	}
	// .dds
	if (FAILED(hr))
	{
		hr = DirectX::GetMetadataFromDDSMemory(data, size, DirectX::DDS_FLAGS_NONE, metadata);
		if (SUCCEEDED(hr))
		{
			hr = DirectX::LoadFromDDSMemory(data, size, DirectX::DDS_FLAGS_NONE, &metadata, scratch_image);
		}
	}
	// .hdr
	if (FAILED(hr))
	{
		hr = DirectX::GetMetadataFromHDRMemory(data, size, metadata);
		if (SUCCEEDED(hr))
		{
			hr = DirectX::LoadFromHDRMemory(data, size, &metadata, scratch_image);
		}
	}
	if (FAILED(hr))
	{
		hr = DirectX::GetMetadataFromWICMemory(data, size, DirectX::WIC_FLAGS_NONE, metadata);
		if (SUCCEEDED(hr))
		{
			hr = DirectX::LoadFromWICMemory(data, size, DirectX::WIC_FLAGS_NONE, &metadata, scratch_image);
		}
	}
	if (FAILED(hr))
	{
		return hr;
	}

	if (metadata.mipLevels == 1)
	{
		DirectX::ScratchImage mipChain;
		// Generate a full mipmap chain using box filtering
		hr = DirectX::GenerateMipMaps(scratch_image.GetImages(), scratch_image.GetImageCount(),
			scratch_image.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
		if (SUCCEEDED(hr))
		{
			scratch_image = std::move(mipChain);
			metadata = scratch_image.GetMetadata();
		}
	}

	// シェーダーリソースビュー作成
	hr = DirectX::CreateShaderResourceView(device, scratch_image.GetImages(), scratch_image.GetImageCount(),
		metadata, shaderResourceView);
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	// テクスチャ情報取得
	if (texture2dDesc != nullptr)
	{
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;
		(*shaderResourceView)->GetResource(resource.GetAddressOf());

		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
		hr = resource->QueryInterface<ID3D11Texture2D>(texture2d.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
		texture2d->GetDesc(texture2dDesc);
	}
	return hr;
}

// ダミーテクスチャ作成
HRESULT GpuResourceUtils::CreateDummyTexture(
	ID3D11Device* device,
	UINT color,
	ID3D11ShaderResourceView** shaderResourceView,
	D3D11_TEXTURE2D_DESC* texture2dDesc)
{
	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Width = 1;
	desc.Height = 1;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA data{};
	data.pSysMem = &color;
	data.SysMemPitch = desc.Width;

	Microsoft::WRL::ComPtr<ID3D11Texture2D>	texture;
	HRESULT hr = device->CreateTexture2D(&desc, &data, texture.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	hr = device->CreateShaderResourceView(texture.Get(), nullptr, shaderResourceView);
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	// テクスチャ情報取得
	if (texture2dDesc != nullptr)
	{
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;
		(*shaderResourceView)->GetResource(resource.GetAddressOf());

		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
		hr = resource->QueryInterface<ID3D11Texture2D>(texture2d.GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
		texture2d->GetDesc(texture2dDesc);
	}

	return hr;
}

// 定数バッファ作成
HRESULT GpuResourceUtils::CreateConstantBuffer(
	ID3D11Device* device,
	UINT bufferSize,
	ID3D11Buffer** constantBuffer)
{
	D3D11_BUFFER_DESC desc{};
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	desc.ByteWidth = (bufferSize + 15) & ~15;

	desc.StructureByteStride = 0;

	HRESULT hr = device->CreateBuffer(&desc, 0, constantBuffer);
	_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

	return hr;
}

// キューブマップ読み込み
HRESULT GpuResourceUtils::LoadCubemap(ID3D11Device* device, const std::array<std::string, 6>& filenames, ID3D11ShaderResourceView** shaderResourceView)
{
	DirectX::ScratchImage images[6];
	D3D11_SUBRESOURCE_DATA subData[6]{};

	// Decode all 6 images using DirectXTex
	for (int i = 0; i < 6; ++i)
	{
		std::filesystem::path filepath(filenames[i]);
		std::wstring wfilename = filepath.wstring();

		HRESULT hr = DirectX::LoadFromWICFile(wfilename.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, images[i]);
		if (FAILED(hr)) return hr;

		const DirectX::Image* img = images[i].GetImage(0, 0, 0);
		subData[i].pSysMem = img->pixels;                     
		subData[i].SysMemPitch = (UINT)img->rowPitch;         // Width * Bytes per pixel
		subData[i].SysMemSlicePitch = (UINT)img->slicePitch;
	}

	// Use the dimensions from the first image to define the texture
	const DirectX::Image* baseImg = images[0].GetImage(0, 0, 0);

	D3D11_TEXTURE2D_DESC texDesc{};
	texDesc.Width = (UINT)baseImg->width;
	texDesc.Height = (UINT)baseImg->height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 6;
	texDesc.Format = baseImg->format;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> cubemapTex;
	HRESULT hr = device->CreateTexture2D(&texDesc, subData, cubemapTex.GetAddressOf());
	if (FAILED(hr)) return hr;

	// Create the Shader Resource View
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MipLevels = 1;
	srvDesc.TextureCube.MostDetailedMip = 0;

	return device->CreateShaderResourceView(cubemapTex.Get(), &srvDesc, shaderResourceView);
}