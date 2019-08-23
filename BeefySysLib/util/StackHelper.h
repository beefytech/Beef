#pragma once

#include "../Common.h"

NS_BF_BEGIN

class StackHelper
{
public:
	StackHelper();
	
	bool CanStackExpand(int wantBytes = 32*1024);	
	bool Execute(const std::function<void()>& func); // Can fail if the job thread overflows stack - must check result
};



NS_BF_END