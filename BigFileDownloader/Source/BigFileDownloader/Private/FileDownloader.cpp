// Fill out your copyright notice in the Description page of Project Settings.


#include "FileDownloader.h"



void UFileDownloader::initDownloader(FString fileURL, FString savePath, bool isForceDownload , int32 chunkSize)
{
	FString FileName = getFileNameFromURL(fileURL);
	_savedDir		  = FPaths::ProjectSavedDir() / savePath;
	Download.fileURL  = fileURL;
	Download.savePath = _savedDir / FileName;
	Download.tmpPath  = _savedDir / FileName + _tempExt;
	_chunkSizeMB	  = chunkSize > 0 ? chunkSize * 1024 * 1024 : 2 * 1024 * 1024;
	_isForceDownload  = isForceDownload;
	// UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] SavePath: %s, TmpPath: %s"), *Download.savePath, *Download.tmpPath);
	changeTaskState(ETaskState::WAIT);
}


FString UFileDownloader::getFileNameFromURL(FString URL)
{
	if (URL.IsEmpty()) return "";

	int32 LastSlashIndex;
	if (URL.FindLastChar('/', LastSlashIndex))
	{
		return URL.RightChop(LastSlashIndex + 1);
	}

	return URL;
}


void UFileDownloader::changeTaskState(ETaskState NewState)
{
	CurrentState = NewState;
	OnTaskStateChange.Broadcast(CurrentState);
	bool bIsCompleted = (NewState == ETaskState::COMPLETED);
	bool bIsError = (NewState == ETaskState::ERROR);
	OnProgressChange.Broadcast(Download.Percent, Download.currentSize, Download.totalSize, bIsCompleted, bIsError);
}

void UFileDownloader::Initialize(FSubsystemCollectionBase& Collection)
{
	if (PlatformFile == nullptr)
	{
		PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();
		if (PlatformFile->GetLowerLevel())
		{
			PlatformFile = PlatformFile->GetLowerLevel();
		}
	}
}

void UFileDownloader::Deinitialize()
{
	PlatformFile = nullptr;
}


void UFileDownloader::startDownload()
{
	if (Download.fileURL.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] The download link is empty."))
		return;
	}

	FHttpRequestRef curl = FHttpModule::Get().CreateRequest();
	curl->SetURL(*Download.fileURL);
	curl->SetVerb(TEXT("HEAD"));
	curl->OnProcessRequestComplete().BindUObject(this, &UFileDownloader::OnGetHeadCompleted);
	curl->ProcessRequest();
}


void UFileDownloader::stopDownload()
{
	changeTaskState(ETaskState::STOP);
}

void UFileDownloader::clearOldFile()
{
	if( PlatformFile == nullptr ) return;


	if (PlatformFile->FileExists(*Download.savePath))
	{
		if (!PlatformFile->DeleteFile(*Download.savePath))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] File delete error: %s"), *Download.savePath);
		}
	}
}


void UFileDownloader::OnGetHeadCompleted(FHttpRequestPtr Request, FHttpResponsePtr Response, bool success)
{

	

	if (Response.IsValid())
	{
		int32 ResponseCode = Response->GetResponseCode();
		if (ResponseCode == 200)
		{
			int64 ContentLength = Response->GetContentLength();
			if (ContentLength <= 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] Invalid Content Length: %d"), ContentLength);
				changeTaskState(ETaskState::ERROR);
			}
			else
			{
				loadJsonFile();
				if (Download.currentSize <= 0 || _isForceDownload || Download.totalSize != ContentLength)
				{
					Download.currentSize = 0;
					Download.totalSize	 = ContentLength;
					changeTaskState(ETaskState::DOWNLOADING);
					clearOldFile();
					writeJsonFile();
					downloadFile();
				}
				else if (Download.totalSize > 0 && Download.currentSize > 0 && Download.currentSize == Download.totalSize && Download.currentSize == ContentLength)
				{
					changeTaskState(ETaskState::COMPLETED);
				}
				else if (Download.totalSize > 0 && Download.currentSize < Download.totalSize)
				{
					changeTaskState(ETaskState::DOWNLOADING);
					downloadFile();
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] No action."));
				}

			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] Invalid Response Code: %d"), ResponseCode);
			changeTaskState(ETaskState::ERROR);
		}
	}
	else
	{
		changeTaskState(ETaskState::ERROR);
		UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] Response not valid."));
	}
}


void UFileDownloader::loadJsonFile()
{
	if (PlatformFile == nullptr || Download.fileURL.IsEmpty()) return;

	FString FileContent;
	FFileStruct LoadedStruct;


	if (PlatformFile->FileExists(*Download.tmpPath))
	{
		if (FFileHelper::LoadFileToString(FileContent, *Download.tmpPath))
		{
			if (FJsonObjectConverter::JsonObjectStringToUStruct<FFileStruct>(FileContent, &LoadedStruct, 0, 0))
			{
				Download = LoadedStruct;
			}
		}
	}
	else
	{
		Download.currentSize = 0;
		Download.totalSize = 0;
	}
}

void UFileDownloader::writeJsonFile()
{
	if (PlatformFile == nullptr || Download.fileURL.IsEmpty()) return;

	
	if (!PlatformFile->DirectoryExists(*_savedDir))
	{
		if (!PlatformFile->CreateDirectoryTree(*_savedDir))
		{
			UE_LOG(LogTemp, Warning, TEXT("Directory create failed. Path: %s"), *_savedDir);
		}
	}

	FString OutputString;
	if (FJsonObjectConverter::UStructToJsonObjectString<FFileStruct>(Download, OutputString, 0, 0))
	{
		IFileHandle* FileHandle = PlatformFile->OpenWrite(*Download.tmpPath);
		if (FileHandle)
		{

			FileHandle->Write((const uint8*)TCHAR_TO_UTF8(*OutputString), OutputString.Len());
			FileHandle->Flush();
			delete FileHandle;
		}
	}

}

void UFileDownloader::downloadFile()
{
	int64 StartPos = Download.currentSize;
	int64 EndPos   = FMath::Min(StartPos + _chunkSizeMB - 1, Download.totalSize);

	if (EndPos >= Download.totalSize) EndPos = Download.totalSize;

	if (StartPos >= EndPos)
	{
		changeTaskState(ETaskState::COMPLETED);
		return;
	}

	FString RangeStr = FString::Printf(TEXT("bytes=%lld-%lld"), StartPos, EndPos);
	FHttpRequestRef curl = FHttpModule::Get().CreateRequest();
	curl->SetVerb(TEXT("GET"));
	curl->SetURL(*Download.fileURL);
	curl->SetHeader(TEXT("Range"), RangeStr);
	curl->OnProcessRequestComplete().BindUObject(this, &UFileDownloader::OnGetRangeCompleted);
	curl->ProcessRequest();
}



void UFileDownloader::OnGetRangeCompleted(FHttpRequestPtr Request, FHttpResponsePtr Response, bool success)
{
	if (CurrentState == ETaskState::STOP) return;

	if (Response.IsValid())
	{
		int32 ResponseCode = Response->GetResponseCode();
		if ( ResponseCode == 200 || ResponseCode == 206 )
		{
			TArray<uint8> DataBuffer = Response->GetContent();
			IFileHandle* FileHandle  = PlatformFile->OpenWrite(*Download.savePath, true, true);
			if (FileHandle->Write(DataBuffer.GetData(), DataBuffer.Num()))
			{
				Download.currentSize += DataBuffer.Num();
				Download.Percent	  = ((float)Download.currentSize / (float)Download.totalSize) * 100.0f;
				writeJsonFile();
				FileHandle->Flush();
				delete FileHandle;
			}

			if (Download.currentSize < Download.totalSize)
			{
				changeTaskState(ETaskState::DOWNLOADING);
				downloadFile();
			}
			else
			{
				Download.Percent = 100.0f;
				writeJsonFile();
				changeTaskState(ETaskState::COMPLETED);
			}
		}
		else
		{
			changeTaskState(ETaskState::ERROR);
			UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] Response code is not valid: %d"), ResponseCode);
		}
	}
	else
	{
		changeTaskState(ETaskState::ERROR);
		UE_LOG(LogTemp, Warning, TEXT("[FileDownloader] Response is not valid. Range request."));
	}
}