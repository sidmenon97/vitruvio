// Copyright 2019 - 2020 Esri. All Rights Reserved.

#pragma once

#include "MaterialConversion.h"

#include "PRTTypes.h"
#include "RuleAttributes.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageChannelsDetection.h"
#include "ImageCore/Public/ImageCore.h"
#include "VitruvioTypes.h"

#include <map>

DEFINE_LOG_CATEGORY(LogMaterialConversion);

namespace
{

constexpr double BLACK_COLOR_THRESHOLD = 0.02;
constexpr double WHITE_COLOR_THRESHOLD = 1.0 - BLACK_COLOR_THRESHOLD;
constexpr double OPACITY_THRESHOLD = 0.98;

const FString CE_DEFAULT_SHADER_NAME = TEXT("CityEngineShader");
const FString CE_PBR_SHADER_NAME = TEXT("CityEnginePBRShader");

struct FTextureSettings
{
	bool SRGB;
	TextureCompressionSettings Compression;
};

struct FTextureData
{
	UTexture2D* Texture = nullptr;
	uint32 NumChannels = 0;
};

template <typename T, typename F>
void CountOpacityMapPixels(const T* SrcColors, int32 SizeX, int32 SizeY, uint32& BlackPixels, uint32& WhitePixels, F Accessor)
{
	BlackPixels = 0;
	WhitePixels = 0;

	const T* LastColor = SrcColors + (SizeX * SizeY);
	while (SrcColors < LastColor)
	{
		const float Value = Accessor(SrcColors);

		if (Value < BLACK_COLOR_THRESHOLD)
		{
			BlackPixels++;
		}
		else if (Value > WHITE_COLOR_THRESHOLD)
		{
			WhitePixels++;
		}
		++SrcColors;
	}
}

void CountOpacityMapPixels(const FColor* SrcColors, bool UseAlphaChannel, int32 SizeX, int32 SizeY, uint32& BlackPixels, uint32& WhitePixels)
{
	return CountOpacityMapPixels(SrcColors, SizeX, SizeY, BlackPixels, WhitePixels,
								 [UseAlphaChannel](const FColor* Color) { return static_cast<float>(UseAlphaChannel ? Color->A : Color->R) / 0xFF; });
}

void CountOpacityMapPixels(const uint8* SrcColors, int32 SizeX, int32 SizeY, uint32& BlackPixels, uint32& WhitePixels)
{
	return CountOpacityMapPixels(SrcColors, SizeX, SizeY, BlackPixels, WhitePixels,
								 [](const uint8* Color) { return static_cast<float>(*Color) / 0xFF; });
}

void CountOpacityMapPixels(const uint16* SrcColors, int32 SizeX, int32 SizeY, uint32& BlackPixels, uint32& WhitePixels)
{
	return CountOpacityMapPixels(SrcColors, SizeX, SizeY, BlackPixels, WhitePixels,
								 [](const uint16* Color) { return static_cast<float>(*Color) / 0xFFFF; });
}

ERGBFormat GetRequestedFormat(ERGBFormat Format)
{
	// We handle textures similarly to Unreal handles non power of two images (which will not be DXT compressed) and always use
	// the BGRA format (even for grayscale textures).
	switch (Format)
	{
	case ERGBFormat::RGBA:
	case ERGBFormat::BGRA:
	case ERGBFormat::Gray: return ERGBFormat::BGRA;
	default: return ERGBFormat::Invalid;
	}
}

EPixelFormat PixelFormatFromRGB(ERGBFormat Format, int32 BitDepth)
{
	check(BitDepth == 8 || BitDepth == 16) check(Format != ERGBFormat::RGBA)

		switch (Format)
	{
	case ERGBFormat::BGRA: return PF_B8G8R8A8;
	case ERGBFormat::Gray: return BitDepth == 8 ? PF_G8 : PF_G16;
	default: return PF_Unknown;
	}
}

FTextureSettings GetTextureSettings(const FString& Key, ERGBFormat Format)
{
	if (Key == L"normalMap")
	{
		return {false, TC_Normalmap};
	}
	if (Key == L"roughnessMap" || Key == L"metallicMap")
	{
		return {false, TC_Masks};
	}
	return {Format == ERGBFormat::Gray ? false : true, TC_Default};
}

UTexture2D* CreateTexture(UObject* Outer, const TArray64<uint8>& Data, int32 SizeX, int32 SizeY, ERGBFormat Format, int32 BitDepth,
						  const FString& TextureKey, const FName& BaseName)
{
	const EPixelFormat PixelFormat = PixelFormatFromRGB(Format, BitDepth);
	const FTextureSettings Settings = GetTextureSettings(TextureKey, Format);

	const FName TextureName = MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), BaseName);
	UTexture2D* NewTexture = NewObject<UTexture2D>(Outer, TextureName, RF_Transient);

	NewTexture->PlatformData = new FTexturePlatformData();
	NewTexture->PlatformData->SizeX = SizeX;
	NewTexture->PlatformData->SizeY = SizeY;
	NewTexture->PlatformData->PixelFormat = PixelFormat;
	NewTexture->CompressionSettings = Settings.Compression;
	NewTexture->SRGB = Settings.SRGB;

	// Allocate first mipmap and upload the pixel data
	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	NewTexture->PlatformData->Mips.Add(Mip);
	Mip->SizeX = SizeX;
	Mip->SizeY = SizeY;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* TextureData = Mip->BulkData.Realloc(CalculateImageBytes(SizeX, SizeY, 0, PixelFormat));
	FMemory::Memcpy(TextureData, Data.GetData(), Data.Num());
	Mip->BulkData.Unlock();

	NewTexture->UpdateResource();
	return NewTexture;
}

FTextureData LoadTextureFromDisk(UObject* Outer, const FString& ImagePath, const FString& TextureKey)
{
	static IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	if (!FPaths::FileExists(ImagePath))
	{
		UE_LOG(LogMaterialConversion, Error, TEXT("File not found: %s"), *ImagePath);
		return {};
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *ImagePath))
	{
		UE_LOG(LogMaterialConversion, Error, TEXT("Failed to load file: %s"), *ImagePath);
		return {};
	}
	const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		UE_LOG(LogMaterialConversion, Error, TEXT("Unrecognized image file format: %s"), *ImagePath);
		return {};
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (!ImageWrapper.IsValid())
	{
		UE_LOG(LogMaterialConversion, Error, TEXT("Failed to create image wrapper for file: %s"), *ImagePath);
		return {};
	}

	// Unfortunately using the IImageWrapperModule to load textures will always result in images with alpha channels even if
	// the original texture does not contain an alpha channel. Since we have to check the existence of alpha channels to determine
	// the blend mode we need to extract real number of channels manually.
	const uint32 NumChannels = Vitruvio::DetectChannels(ImageFormat, FileData.GetData(), FileData.Num());

	// Decompress the image data
	TArray64<uint8> RawData;
	ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num());
	const ERGBFormat Format = GetRequestedFormat(ImageWrapper->GetFormat());
	ImageWrapper->GetRaw(Format, ImageWrapper->GetBitDepth(), RawData);

	// Create the texture and upload the uncompressed image data
	const FString TextureBaseName = TEXT("T_") + FPaths::GetBaseFilename(ImagePath);
	UTexture2D* Texture = CreateTexture(Outer, RawData, ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), Format, ImageWrapper->GetBitDepth(),
										TextureKey, FName(*TextureBaseName));

	return {Texture, NumChannels};
}

EBlendMode ChooseBlendModeFromOpacityMap(const FTextureData& OpacityMapData, bool UseAlphaAsOpacity)
{
	const UTexture2D* OpacityMap = OpacityMapData.Texture;
	const EPixelFormat PixelFormat = OpacityMap->GetPixelFormat();
	check(PixelFormat == PF_B8G8R8A8 || PixelFormat == PF_G8 || PixelFormat == PF_G16);

	uint32 BlackPixels = 0;
	uint32 WhitePixels = 0;

	switch (PixelFormat)
	{
	case PF_B8G8R8A8:
	{
		const FColor* ImageData = reinterpret_cast<const FColor*>(OpacityMap->PlatformData->Mips[0].BulkData.LockReadOnly());

		// Now count the black and white pixels of the appropriate opacity map channel to determine the opacity mode
		CountOpacityMapPixels(ImageData, UseAlphaAsOpacity, OpacityMap->GetSizeX(), OpacityMap->GetSizeY(), BlackPixels, WhitePixels);
		break;
	}
	case PF_G8:
	{
		const uint8* ImageData = reinterpret_cast<const uint8*>(OpacityMap->PlatformData->Mips[0].BulkData.LockReadOnly());
		CountOpacityMapPixels(ImageData, OpacityMap->GetSizeX(), OpacityMap->GetSizeY(), BlackPixels, WhitePixels);
		break;
	}
	case PF_G16:
	{
		const uint16* ImageData = reinterpret_cast<const uint16*>(OpacityMap->PlatformData->Mips[0].BulkData.LockReadOnly());
		CountOpacityMapPixels(ImageData, OpacityMap->GetSizeX(), OpacityMap->GetSizeY(), BlackPixels, WhitePixels);
		break;
	}
	default: check(0)
	}

	OpacityMap->PlatformData->Mips[0].BulkData.Unlock();

	const uint32 TotalPixels = OpacityMap->GetSizeX() * OpacityMap->GetSizeY();
	if (WhitePixels >= TotalPixels * OPACITY_THRESHOLD)
	{
		return BLEND_Opaque;
	}
	if (WhitePixels + BlackPixels >= TotalPixels * OPACITY_THRESHOLD)
	{
		return BLEND_Masked;
	}
	return BLEND_Translucent;
}

EBlendMode ChooseBlendMode(const FTextureData& OpacityMapData, double Opacity, EBlendMode BlendMode, bool UseAlphaAsOpacity)
{
	if (Opacity < OPACITY_THRESHOLD)
	{
		return BLEND_Translucent;
	}
	else if (BlendMode == BLEND_Masked)
	{
		return BLEND_Masked;
	}
	else if (BlendMode == BLEND_Translucent && OpacityMapData.Texture)
	{
		// OpacityMap exists and opacitymap.mode is blend (which is the default value) so we need to check the content of the OpacityMap
		// to really decide which material we need for Unreal
		return ChooseBlendModeFromOpacityMap(OpacityMapData, UseAlphaAsOpacity);
	}
	else
	{
		return BLEND_Opaque;
	}
}

EBlendMode GetBlendMode(const FString& OpacityMapMode)
{
	if (OpacityMapMode == "mask")
	{
		return BLEND_Masked;
	}
	else if (OpacityMapMode == "blend")
	{
		return BLEND_Translucent;
	}
	return BLEND_Opaque;
}

UMaterialInterface* GetMaterialByBlendMode(EBlendMode Mode, UMaterialInterface* Opaque, UMaterialInterface* Masked, UMaterialInterface* Translucent)
{
	switch (Mode)
	{
	case BLEND_Translucent: return Translucent;
	case BLEND_Masked: return Masked;
	default: return Opaque;
	}
}

} // namespace

namespace Vitruvio
{
UMaterialInstanceDynamic* GameThread_CreateMaterialInstance(UObject* Outer, UMaterialInterface* OpaqueParent, UMaterialInterface* MaskedParent,
															UMaterialInterface* TranslucentParent,
															const FMaterialAttributeContainer& MaterialContainer)
{
	check(IsInGameThread());

	TMap<FString, TFuture<FTextureData>> TextureProperties;

	// Load Textures Asynchronously
	for (const auto& TextureProperty : MaterialContainer.TextureProperties)
	{
		// Load textures async in thread pool
		// clang-format off
		TFuture<FTextureData> Result = Async(EAsyncExecution::ThreadPool, [Outer, TextureProperty]() -> FTextureData {
            QUICK_SCOPE_CYCLE_COUNTER(STAT_MaterialConversion_LoadTexture);
			if (TextureProperty.Value.IsEmpty())
			{
                return {};
            }
            const FTextureData TextureData = LoadTextureFromDisk(Outer, TextureProperty.Value, TextureProperty.Key);
            return TextureData;
        });
		// clang-format on

		TextureProperties.Add(TextureProperty.Key, MoveTemp(Result));
	}

	const float Opacity = MaterialContainer.ScalarProperties["opacity"];
	const FTextureData OpacityMapData = TextureProperties.Contains("opacityMap") ? TextureProperties["opacityMap"].Get() : FTextureData{};
	const bool UseAlphaAsOpacity = OpacityMapData.Texture && OpacityMapData.NumChannels == 4;
	const EBlendMode ChosenBlendMode = ChooseBlendMode(OpacityMapData, Opacity, GetBlendMode(MaterialContainer.BlendMode), UseAlphaAsOpacity);

	const FString Shader = MaterialContainer.StringProperties["shader"];
	UMaterialInterface* Parent = nullptr;

	if (!Shader.IsEmpty() && Shader != CE_DEFAULT_SHADER_NAME && Shader != CE_PBR_SHADER_NAME)
	{
		const FString FileName = FPaths::GetBaseFilename(Shader);
		const FString ParentMaterialPath = Shader + TEXT(".") + FileName;
		Parent = LoadObject<UMaterial>(Outer, *ParentMaterialPath);
	}
	if (!Parent)
	{
		Parent = GetMaterialByBlendMode(ChosenBlendMode, OpaqueParent, MaskedParent, TranslucentParent);
	}

	UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(Parent, Outer);

	MaterialInstance->SetScalarParameterValue(FName(TEXT("opacitySource")), UseAlphaAsOpacity);

	for (const TPair<FString, TFuture<FTextureData>>& TextureFuture : TextureProperties)
	{
		const auto Result = TextureFuture.Value.Get();
		MaterialInstance->SetTextureParameterValue(FName(TextureFuture.Key), Result.Texture);
	}
	for (const TPair<FString, double>& ScalarProperty : MaterialContainer.ScalarProperties)
	{
		MaterialInstance->SetScalarParameterValue(FName(ScalarProperty.Key), ScalarProperty.Value);
	}
	for (const TPair<FString, FLinearColor>& ColorProperty : MaterialContainer.ColorProperties)
	{
		MaterialInstance->SetVectorParameterValue(FName(ColorProperty.Key), ColorProperty.Value);
	}

	return MaterialInstance;
}
} // namespace Vitruvio
