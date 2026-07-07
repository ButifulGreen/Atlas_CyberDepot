// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 로봇 오케스트레이션(존 예약/파트너 매칭/이동 완료)이 조용히 멈추는 지점을 진단하기 위한 전용 카테고리.
// LogTemp와 분리해 아웃풋 로그에서 필터링할 수 있게 한다.
DECLARE_LOG_CATEGORY_EXTERN(LogFactoryDispatch, Log, All);
