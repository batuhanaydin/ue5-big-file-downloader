// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Http.h"
#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Subsystems/WorldSubsystem.h"
#include "Async/Async.h"
#include "JsonObjectConverter.h"
#include "FileDownloader.generated.h"



USTRUCT(BlueprintType)
struct BIGFILEDOWNLOADER_API FFileStruct
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FileDownloader|Data")
	FString fileURL;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FileDownloader|Data")
	FString savePath;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FileDownloader|Data")
	FString tmpPath;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FileDownloader|Data")
	float Percent = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FileDownloader|Data")
	int64 totalSize = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FileDownloader|Data")
	int64 currentSize = 0;


};

UENUM(BlueprintType)
enum ETaskState : uint8
{
	WAIT		= 0 UMETA(DisplayName = "Wait"),
	DOWNLOADING = 1 UMETA(DisplayName = "Downloading"),
	COMPLETED	= 2 UMETA(DisplayName = "Completed"),
	STOP		= 3 UMETA(DisplayName = "Stop"),
	ERROR		= 4 UMETA(DisplayName = "Error")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTaskStateChanged, ETaskState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnDownloadProgressChanged, float, Percent, int64, CurrentSize, int64, TotalSize, bool, onComplete, bool, onFailed);

UCLASS()

class BIGFILEDOWNLOADER_API UFileDownloader : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable)
	void initDownloader(FString fileURL, FString savePath, bool isForceDownload = false, int32 chunkSizeMB = 2 );

	UPROPERTY(BlueprintAssignable, Category = "FileDownloader|Events")
	FOnTaskStateChanged OnTaskStateChange;

	UPROPERTY(BlueprintAssignable, Category = "FileDownloader|Events")
	FOnDownloadProgressChanged OnProgressChange;

	UFUNCTION(BlueprintCallable)
	void startDownload();

	UFUNCTION(BlueprintCallable)
	void stopDownload();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:

	UPROPERTY(BlueprintReadOnly)
	FFileStruct Download;

	UPROPERTY(BlueprintReadOnly)
	int32 _chunkSizeMB;

	UPROPERTY(BlueprintReadOnly)
	bool _isForceDownload = false;

	UPROPERTY(BlueprintReadOnly)
	FString _tempExt = TEXT(".cache");

	UPROPERTY(BlueprintReadOnly)
	TEnumAsByte<ETaskState> CurrentState = ETaskState::WAIT;

	FString getFileNameFromURL(FString URL);
	void loadJsonFile();
	void writeJsonFile();
	void downloadFile();
	void clearOldFile();
	void OnGetHeadCompleted(FHttpRequestPtr Request, FHttpResponsePtr Response, bool success);
	void OnGetRangeCompleted(FHttpRequestPtr Request, FHttpResponsePtr Response, bool success);
	void changeTaskState(ETaskState NewState);
	IPlatformFile* PlatformFile = nullptr;
	FString _savedDir;
};