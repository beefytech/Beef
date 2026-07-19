#pragma once

#include "Common.h"

NS_BF_BEGIN;

class RenderState;
class RenderDevice;
class RenderWindow;
class DrawLayer;
class RenderCmd;

class Renderable
{
public:

public:	
	virtual ~Renderable() {}
	
	virtual void CommandQueued(RenderCmd* renderCmd, DrawLayer* drawLayer) {}
	virtual void Render(RenderCmd* renderCmd, RenderDevice* renderDevice, RenderWindow* renderWindow) = 0;
};

NS_BF_END;
